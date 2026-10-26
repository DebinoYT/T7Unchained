#pragma once

// Non-relocating asset-pool extension.
//
// BO3 hands out pool entries by popping XAssetPool::freeHead and returns them by
// pushing onto it; every live asset is referenced by pointer, so the existing
// storage must never move. Extra capacity is therefore added as a separate block
// whose entries are chained onto the tail of the existing free list. The stock
// entries keep being used first, and the block is only reached once the stock
// storage is exhausted (the point at which the engine would otherwise fail).
//
// The engine pops and pushes the head without atomics. Linking at the tail with
// a compare-exchange from nullptr cannot corrupt the list: a tail that the engine
// has already claimed and overwritten fails the exchange, and a tail claimed but
// not yet overwritten at worst drops the block, which verify() then reports.
// This file has no Windows dependencies so it can be tested offline.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#include "game/structs.hpp"

namespace pool_extension
{
	constexpr std::size_t block_alignment = 64;

	struct block
	{
		std::byte* base{};
		std::size_t count{};
	};

	// The memory a pool's free list may legitimately point into.
	struct layout
	{
		std::uintptr_t storage{};
		std::size_t storage_count{};
		std::size_t item_size{};
		std::vector<block> blocks;

		bool contains_slot(const void* address) const
		{
			const auto value = reinterpret_cast<std::uintptr_t>(address);
			if (!value || !item_size)
				return false;

			const auto in_range = [this, value](const std::uintptr_t start, const std::size_t count)
			{
				return start && value >= start && (value - start) / item_size < count &&
					(value - start) % item_size == 0;
			};

			if (in_range(storage, storage_count))
				return true;
			for (const auto& extension : blocks)
			{
				if (in_range(reinterpret_cast<std::uintptr_t>(extension.base), extension.count))
					return true;
			}
			return false;
		}

		std::size_t total_slots() const
		{
			auto total = storage_count;
			for (const auto& extension : blocks)
				total += extension.count;
			return total;
		}
	};

	enum class append_status
	{
		appended,
		appended_to_empty,
		invalid_free_list,
		contended
	};

	inline const char* to_string(const append_status status)
	{
		switch (status)
		{
		case append_status::appended: return "linked after the free-list tail";
		case append_status::appended_to_empty: return "installed as the head of an exhausted free list";
		case append_status::invalid_free_list: return "free list contains a link outside the pool";
		case append_status::contended: return "free list kept changing during the append";
		default: return "unknown";
		}
	}

	struct walk_result
	{
		bool valid{};
		game::AssetLink* tail{};
		std::size_t length{};
		bool found_target{};
	};

	inline std::atomic_ref<game::AssetLink*> link_ref(game::AssetLink*& link)
	{
		return std::atomic_ref<game::AssetLink*>(link);
	}

	// Requested entries beyond the current capacity, or 0 when no growth is needed.
	inline std::size_t extension_count(const std::int32_t current_capacity, const std::uint32_t requested)
	{
		if (current_capacity < 0 || requested <= static_cast<std::uint32_t>(current_capacity) ||
			requested > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)()))
			return 0;
		return requested - static_cast<std::uint32_t>(current_capacity);
	}

	// Zeroed storage for count entries, or nullptr. Never freed: the engine keeps
	// pointers to these entries for the rest of the process.
	inline std::byte* allocate_block(const std::size_t count, const std::size_t item_size)
	{
		if (!count || item_size < sizeof(game::AssetLink) ||
			count > (std::numeric_limits<std::size_t>::max)() / item_size)
			return nullptr;

		const auto bytes = count * item_size;
		auto* storage = static_cast<std::byte*>(::operator new(bytes,
			std::align_val_t{block_alignment}, std::nothrow));
		if (storage)
			std::memset(storage, 0, bytes);
		return storage;
	}

	// Chains every entry of a block in address order; the last entry ends the list.
	inline game::AssetLink* link_block(std::byte* base, const std::size_t count, const std::size_t item_size)
	{
		if (!base || !count)
			return nullptr;
		for (std::size_t index = 0; index < count; ++index)
		{
			auto* current = reinterpret_cast<game::AssetLink*>(base + index * item_size);
			current->next = index + 1 < count ?
				reinterpret_cast<game::AssetLink*>(base + (index + 1) * item_size) : nullptr;
		}
		return reinterpret_cast<game::AssetLink*>(base);
	}

	inline walk_result walk(game::AssetLink* head, const layout& bounds, const void* target = nullptr)
	{
		walk_result result{};
		const auto limit = bounds.total_slots();
		auto* node = head;
		while (node)
		{
			if (!bounds.contains_slot(node) || result.length >= limit)
				return result;

			++result.length;
			result.tail = node;
			result.found_target |= node == target;
			node = link_ref(node->next).load();
		}
		result.valid = true;
		return result;
	}

	inline walk_result walk(game::XAssetPool& pool, const layout& bounds, const void* target = nullptr)
	{
		return walk(link_ref(pool.freeHead).load(), bounds, target);
	}

	// Links first (the head of an already linked block that bounds must include)
	// onto the end of the pool's free list.
	inline append_status append(game::XAssetPool& pool, const layout& bounds, game::AssetLink* first,
		const int attempts = 16)
	{
		auto failure = append_status::contended;
		auto head = link_ref(pool.freeHead);
		for (int attempt = 0; attempt < attempts; ++attempt)
		{
			auto* current = head.load();
			if (!current)
			{
				game::AssetLink* expected = nullptr;
				if (head.compare_exchange_strong(expected, first))
					return append_status::appended_to_empty;
				continue;
			}

			const auto result = walk(current, bounds);
			if (!result.valid)
			{
				failure = append_status::invalid_free_list;
				continue;
			}

			game::AssetLink* expected = nullptr;
			if (link_ref(result.tail->next).compare_exchange_strong(expected, first))
				return append_status::appended;
			failure = append_status::contended;
		}
		return failure;
	}
}
