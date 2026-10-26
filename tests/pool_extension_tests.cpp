// Offline tests for the free-list extension in components/pool_extension.hpp.
// The engine allocator is modelled as BO3 performs it: pop freeHead on alloc,
// push onto freeHead on release, and overwrite the entry once allocated.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "components/pool_extension.hpp"
#include "tests.hpp"

namespace
{
	// Stands in for the asset name pointer the engine writes into an allocated entry.
	const char asset_name[] = "asset";
	auto* const allocated_marker = reinterpret_cast<game::AssetLink*>(const_cast<char*>(asset_name));

	struct fake_pool
	{
		game::XAssetPool pool{};
		pool_extension::layout bounds{};
	};

	// DB_AssetPoolInit's result: contiguous storage chained in address order.
	fake_pool make_pool(const std::size_t count, const std::uint32_t item_size)
	{
		fake_pool result{};
		auto* storage = pool_extension::allocate_block(count, item_size);
		result.pool.pool = storage;
		result.pool.itemSize = item_size;
		result.pool.itemCount = static_cast<std::int32_t>(count);
		result.pool.freeHead = pool_extension::link_block(storage, count, item_size);
		result.bounds = {reinterpret_cast<std::uintptr_t>(storage), count, item_size, {}};
		return result;
	}

	game::AssetLink* engine_alloc(game::XAssetPool& pool)
	{
		auto head = pool_extension::link_ref(pool.freeHead);
		auto* entry = head.load(std::memory_order_relaxed);
		if (!entry)
			return nullptr;
		head.store(pool_extension::link_ref(entry->next).load(std::memory_order_relaxed),
			std::memory_order_relaxed);
		++pool.itemAllocCount;
		pool_extension::link_ref(entry->next).store(allocated_marker, std::memory_order_relaxed);
		return entry;
	}

	void engine_release(game::XAssetPool& pool, game::AssetLink* entry)
	{
		auto head = pool_extension::link_ref(pool.freeHead);
		pool_extension::link_ref(entry->next).store(head.load(std::memory_order_relaxed),
			std::memory_order_relaxed);
		head.store(entry, std::memory_order_relaxed);
		--pool.itemAllocCount;
	}

	// What extend_pool does after validation.
	pool_extension::append_status extend(fake_pool& fake, const std::uint32_t requested,
		game::AssetLink** first_out = nullptr)
	{
		const auto count = pool_extension::extension_count(fake.pool.itemCount, requested);
		auto* storage = pool_extension::allocate_block(count, fake.pool.itemSize);
		auto* first = pool_extension::link_block(storage, count, fake.pool.itemSize);
		fake.bounds.blocks.push_back({storage, count});
		const auto status = pool_extension::append(fake.pool, fake.bounds, first);
		if (status == pool_extension::append_status::appended ||
			status == pool_extension::append_status::appended_to_empty)
			fake.pool.itemCount += static_cast<std::int32_t>(count);
		else
			fake.bounds.blocks.pop_back();
		if (first_out)
			*first_out = first;
		return status;
	}

	bool in_storage(const fake_pool& fake, const void* entry)
	{
		const auto value = reinterpret_cast<std::uintptr_t>(entry);
		return value >= fake.bounds.storage &&
			value < fake.bounds.storage + fake.bounds.storage_count * fake.bounds.item_size;
	}

	// Descriptor layout shared by BOIII, DutyCore and weapon_tech for this engine.
	static_assert(sizeof(game::XAssetPool) == 0x20);
	static_assert(offsetof(game::XAssetPool, pool) == 0x00);
	static_assert(offsetof(game::XAssetPool, itemSize) == 0x08);
	static_assert(offsetof(game::XAssetPool, itemCount) == 0x0C);
	static_assert(offsetof(game::XAssetPool, isSingleton) == 0x10);
	static_assert(offsetof(game::XAssetPool, itemAllocCount) == 0x14);
	static_assert(offsetof(game::XAssetPool, freeHead) == 0x18);
	static_assert(game::ASSET_TYPE_ATTACHMENT * sizeof(game::XAssetPool) == 0x3E0);
	static_assert(game::ASSET_TYPE_ATTACHMENT_UNIQUE * sizeof(game::XAssetPool) == 0x400);

	void test_capacity_calculation()
	{
		CHECK(pool_extension::extension_count(128, 512) == 384);
		CHECK(pool_extension::extension_count(1024, 4096) == 3072);
		CHECK(pool_extension::extension_count(0x864, 4096) == 4096 - 0x864);
		CHECK(pool_extension::extension_count(512, 512) == 0);
		CHECK(pool_extension::extension_count(600, 512) == 0);
		CHECK(pool_extension::extension_count(-1, 512) == 0);
		CHECK(pool_extension::extension_count(0, 0x80000000u) == 0);
	}

	void test_block_allocation_and_linking()
	{
		CHECK(pool_extension::allocate_block(0, 0x220) == nullptr);
		CHECK(pool_extension::allocate_block(4, 4) == nullptr);
		CHECK(pool_extension::allocate_block((std::numeric_limits<std::size_t>::max)() / 2, 0x220) == nullptr);

		constexpr std::size_t count = 5;
		constexpr std::size_t size = 0x220;
		auto* storage = pool_extension::allocate_block(count, size);
		CHECK(storage != nullptr);
		CHECK(reinterpret_cast<std::uintptr_t>(storage) % pool_extension::block_alignment == 0);
		CHECK(std::all_of(storage, storage + count * size, [](const std::byte value) { return value == std::byte{}; }));

		auto* first = pool_extension::link_block(storage, count, size);
		CHECK(first == reinterpret_cast<game::AssetLink*>(storage));
		std::size_t length = 0;
		for (auto* node = first; node; node = node->next)
		{
			CHECK(node == reinterpret_cast<game::AssetLink*>(storage + length * size));
			++length;
		}
		CHECK(length == count);
	}

	// attachment: stock 128, 93 live entries with a fragmented free list, request 512.
	void test_extend_partially_used_pool()
	{
		auto fake = make_pool(128, 0x220);
		std::vector<game::AssetLink*> live;
		for (int index = 0; index < 100; ++index)
			live.push_back(engine_alloc(fake.pool));
		for (const int index : {3, 17, 18, 40, 41, 77, 99})
			engine_release(fake.pool, live[index]);
		CHECK(fake.pool.itemAllocCount == 93);

		const auto before = pool_extension::walk(fake.pool, fake.bounds);
		CHECK(before.valid && before.length == 35);

		CHECK(extend(fake, 512) == pool_extension::append_status::appended);
		CHECK(fake.pool.itemCount == 512);
		const auto after = pool_extension::walk(fake.pool, fake.bounds);
		CHECK(after.valid && after.length == 35 + 384);

		std::set<game::AssetLink*> handed_out;
		std::size_t stock_first = 0;
		bool extension_seen = false;
		while (auto* entry = engine_alloc(fake.pool))
		{
			CHECK(fake.bounds.contains_slot(entry));
			CHECK(handed_out.insert(entry).second);
			if (in_storage(fake, entry))
			{
				CHECK(!extension_seen);
				++stock_first;
			}
			else
			{
				extension_seen = true;
			}
		}
		CHECK(stock_first == 35);
		CHECK(handed_out.size() == 419);
		CHECK(fake.pool.itemAllocCount == 512);
		CHECK(fake.pool.freeHead == nullptr);

		// Released extension entries are reused normally.
		engine_release(fake.pool, *handed_out.rbegin());
		CHECK(engine_alloc(fake.pool) == *handed_out.rbegin());
	}

	// attachmentunique: stock 1024 fully used (the engine's failure point), request 4096.
	void test_extend_exhausted_pool()
	{
		auto fake = make_pool(1024, 0x840);
		while (engine_alloc(fake.pool))
		{
		}
		CHECK(fake.pool.freeHead == nullptr && fake.pool.itemAllocCount == 1024);

		game::AssetLink* first{};
		CHECK(extend(fake, 4096, &first) == pool_extension::append_status::appended_to_empty);
		CHECK(fake.pool.itemCount == 4096);
		CHECK(fake.pool.freeHead == first);

		std::size_t allocated = 0;
		while (auto* entry = engine_alloc(fake.pool))
		{
			CHECK(!in_storage(fake, entry) && fake.bounds.contains_slot(entry));
			++allocated;
		}
		CHECK(allocated == 3072);
		CHECK(fake.pool.itemAllocCount == 4096);
	}

	void test_no_change_when_capacity_suffices()
	{
		auto fake = make_pool(0x864, 0x840);
		CHECK(pool_extension::extension_count(fake.pool.itemCount, 2048) == 0);
		CHECK(pool_extension::walk(fake.pool, fake.bounds).length == 0x864);
	}

	void test_refuses_corrupt_free_list()
	{
		auto fake = make_pool(16, 0x220);
		auto* storage = static_cast<std::byte*>(fake.pool.pool);
		reinterpret_cast<game::AssetLink*>(storage + 4 * 0x220)->next =
			reinterpret_cast<game::AssetLink*>(storage + 4 * 0x220 + 8);
		CHECK(!pool_extension::walk(fake.pool, fake.bounds).valid);
		CHECK(extend(fake, 64) == pool_extension::append_status::invalid_free_list);
		CHECK(fake.pool.itemCount == 16);
		CHECK(fake.bounds.blocks.empty());
	}

	void test_refuses_cyclic_free_list()
	{
		auto fake = make_pool(16, 0x220);
		auto* storage = static_cast<std::byte*>(fake.pool.pool);
		reinterpret_cast<game::AssetLink*>(storage + 15 * 0x220)->next = fake.pool.freeHead;
		CHECK(!pool_extension::walk(fake.pool, fake.bounds).valid);
		CHECK(extend(fake, 64) == pool_extension::append_status::invalid_free_list);
		CHECK(fake.pool.itemCount == 16);
	}

	// The engine keeps allocating and releasing on another thread while the block is
	// appended. Whatever interleaving occurs, the list must stay well formed and must
	// never contain an entry the engine considers allocated.
	void test_concurrent_engine_activity()
	{
		constexpr int rounds = 300;
		int dropped = 0;
		for (int round = 0; round < rounds; ++round)
		{
			auto fake = make_pool(64, 0x20);
			std::vector<game::AssetLink*> live;
			std::atomic<bool> start{false};
			std::atomic<bool> stop{false};

			std::thread engine([&]
			{
				std::mt19937 random(static_cast<unsigned>(round));
				while (!start.load())
				{
				}
				for (int step = 0; step < 4000 && !stop.load(); ++step)
				{
					if (live.empty() || random() % 3)
					{
						if (auto* entry = engine_alloc(fake.pool))
							live.push_back(entry);
					}
					else
					{
						const auto index = random() % live.size();
						engine_release(fake.pool, live[index]);
						live.erase(live.begin() + static_cast<std::ptrdiff_t>(index));
					}
				}
			});

			auto* storage = pool_extension::allocate_block(64, 0x20);
			auto* first = pool_extension::link_block(storage, 64, 0x20);
			fake.bounds.blocks.push_back({storage, 64});
			start.store(true);
			const auto status = pool_extension::append(fake.pool, fake.bounds, first);
			stop.store(true);
			engine.join();

			CHECK(status != pool_extension::append_status::invalid_free_list);
			const auto result = pool_extension::walk(fake.pool, fake.bounds);
			CHECK(result.valid);

			std::set<game::AssetLink*> free_entries;
			for (auto* node = fake.pool.freeHead; node; node = node->next)
				CHECK(free_entries.insert(node).second);
			for (auto* entry : live)
				CHECK(!free_entries.count(entry));
			CHECK(free_entries.size() + live.size() <= 128);
			if (free_entries.size() + live.size() < 128)
				++dropped;
		}
		std::printf("concurrent append: %d/%d rounds lost the block to a racing pop (no corruption)\n",
			dropped, rounds);
	}
}

void tests::run_pool_extension_tests()
{
	test_capacity_calculation();
	test_block_allocation_and_linking();
	test_extend_partially_used_pool();
	test_extend_exhausted_pool();
	test_no_change_when_capacity_suffices();
	test_refuses_corrupt_free_list();
	test_refuses_cyclic_free_list();
	test_concurrent_engine_activity();
}
