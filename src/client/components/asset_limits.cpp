#include <std_include.hpp>

#include "asset_limits.hpp"
#include "pool_extension.hpp"
#include "game/config_names.hpp"
#include "game/game.hpp"
#include "game/structs.hpp"

namespace asset_limits
{
	namespace
	{
		struct pool_request
		{
			game::XAssetType type;
			const char* name;
			std::uint32_t stock_size;
			std::uint32_t item_size;
			std::uint32_t requested_size{};
			std::int32_t runtime_original{};
			std::int32_t final_size{};
			bool extended{};
			pool_extension::layout bounds{};
		};

		constexpr std::size_t pool_count = 2;
		constexpr std::size_t descriptor_count = 0x67;

		// Entry sizes match DB_GetXAssetTypeSize for these types (0x220 and 0x840);
		// a different runtime itemSize means the descriptor is not the expected pool.
		std::array<pool_request, pool_count> requests{{
			{game::ASSET_TYPE_ATTACHMENT, "attachment", 0x80, 0x220},
			{game::ASSET_TYPE_ATTACHMENT_UNIQUE, "attachmentunique", 0x400, 0x840}
		}};

		void* asset_pool_init_original{};
		std::uintptr_t asset_pool_init_address{};
		std::uintptr_t asset_pools_address{};
		bool enabled = true;
		bool diagnostics_only{};
		std::uint32_t monitor_interval_ms = 2000;
		std::string log_level = "info";
		bool initialized_once{};
		bool minhook_initialized{};
		bool monitor_started{};
		std::filesystem::path offsets_file_path;

		std::string trim(std::string value)
		{
			const auto is_space = [](const unsigned char character)
			{
				return std::isspace(character) != 0;
			};

			while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
				value.erase(value.begin());
			while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
				value.pop_back();
			return value;
		}

		std::string lower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
			return value;
		}

		std::optional<std::uint32_t> parse_size(const std::string& value)
		{
			const auto text = trim(value);
			if (text.empty() || text.front() == '-')
				return std::nullopt;

			try
			{
				std::size_t consumed{};
				const auto hexadecimal = text.size() > 2 && text[0] == '0' &&
					(text[1] == 'x' || text[1] == 'X');
				const auto parsed = std::stoull(text, &consumed, hexadecimal ? 16 : 10);
				if (consumed != text.size() ||
					parsed > static_cast<unsigned long long>((std::numeric_limits<std::int32_t>::max)()))
					return std::nullopt;
				return static_cast<std::uint32_t>(parsed);
			}
			catch (const std::invalid_argument&)
			{
				return std::nullopt;
			}
			catch (const std::out_of_range&)
			{
				return std::nullopt;
			}
		}

		bool parse_bool(const std::string& value, bool& result)
		{
			const auto normalized = lower(trim(value));
			if (normalized == "true" || normalized == "1" || normalized == "yes")
			{
				result = true;
				return true;
			}
			if (normalized == "false" || normalized == "0" || normalized == "no")
			{
				result = false;
				return true;
			}
			return false;
		}

		void log(const std::string& message, const std::string& level = "info")
		{
			const auto priority = [](const std::string& name)
			{
				if (name == "debug") return 0;
				if (name == "info") return 1;
				if (name == "warning") return 2;
				return 3;
			};
			if (priority(level) < priority(log_level))
				return;
			game::minlog.WriteLine((std::string("[T7Unchained] ") + message).c_str());
		}

		std::string build_key(const char* address_name)
		{
			return std::string(address_name) + "." + game::offsets::build_id();
		}

		bool read_rva(const std::string& key, std::uintptr_t& rva, const bool required = true)
		{
			std::ifstream input(offsets_file_path);
			if (!input)
			{
				if (required)
					log("Asset pool expansion disabled: offsets.txt could not be opened.", "error");
				return false;
			}

			std::string line;
			while (std::getline(input, line))
			{
				line = trim(line);
				if (line.empty() || line.front() == '#' || line.front() == ';')
					continue;

				const auto separator = line.find('=');
				if (separator == std::string::npos || trim(line.substr(0, separator)) != key)
					continue;

				const auto value = trim(line.substr(separator + 1));
				if (value.size() < 3 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
					break;

				try
				{
					std::size_t consumed{};
					const auto parsed = std::stoull(value, &consumed, 16);
					if (consumed != value.size() || parsed == 0)
						break;
					rva = static_cast<std::uintptr_t>(parsed);
					return true;
				}
				catch (const std::invalid_argument&)
				{
					break;
				}
				catch (const std::out_of_range&)
				{
					break;
				}
			}

			if (required)
				log(std::string("Asset pool expansion disabled: missing or invalid offset ") + key + ".", "error");
			return false;
		}

		bool resolve_rva(const std::uintptr_t rva, const std::size_t bytes,
			const bool require_executable, const bool require_writable,
			std::uintptr_t& address)
		{
			const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
			if (!module || !dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
				return false;

			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE ||
				rva >= nt->OptionalHeader.SizeOfImage ||
				bytes > nt->OptionalHeader.SizeOfImage - rva)
				return false;

			address = module + rva;
			MEMORY_BASIC_INFORMATION memory{};
			if (!VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) ||
				memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
				return false;

			const auto region_base = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
			if (address < region_base)
				return false;
			const auto region_offset = address - region_base;
			if (region_offset > memory.RegionSize || bytes > memory.RegionSize - region_offset)
				return false;

			const auto executable = (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
				PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
			const auto writable = (memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
				PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
			return (!require_executable || executable) && (!require_writable || writable);
		}

		bool load_offsets()
		{
			std::uintptr_t pool_rva{};
			if (!read_rva(build_key("DB_XAssetPool"), pool_rva) ||
				!resolve_rva(pool_rva, sizeof(game::XAssetPool) * descriptor_count, false, true,
					asset_pools_address) ||
				asset_pools_address % alignof(game::XAssetPool) != 0)
			{
				log("Asset pool expansion disabled: DB_XAssetPool offset is not a valid mapped address.", "error");
				return false;
			}

			log(std::string("Loaded DB_XAssetPool offset from ") + offsets_file_path.string() + ".");
			return true;
		}

		bool initialize_minhook()
		{
			if (minhook_initialized)
				return true;
			const auto status = MH_Initialize();
			if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
			{
				log("Asset pool expansion disabled: MinHook initialization failed.", "error");
				return false;
			}
			minhook_initialized = true;
			return true;
		}

		game::XAssetPool* get_pool(const game::XAssetType type)
		{
			if (!asset_pools_address)
				return nullptr;
			return reinterpret_cast<game::XAssetPool*>(asset_pools_address) + static_cast<int>(type);
		}

		bool is_readable_range(std::uintptr_t address, std::size_t bytes)
		{
			if (!address || !bytes ||
				address > (std::numeric_limits<std::uintptr_t>::max)() - bytes)
				return false;

			while (bytes)
			{
				MEMORY_BASIC_INFORMATION memory{};
				if (!VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) ||
					memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
					return false;

				const auto protection = memory.Protect & 0xFF;
				if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
					protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
					protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
					return false;

				const auto region_end = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) +
					memory.RegionSize;
				if (address >= region_end)
					return false;

				const auto available = region_end - address;
				const auto consumed = (std::min)(available, static_cast<std::uintptr_t>(bytes));
				address += consumed;
				bytes -= static_cast<std::size_t>(consumed);
			}

			return true;
		}

		bool guarded_copy(void* destination, const void* source, const std::size_t bytes)
		{
			__try
			{
				std::memcpy(destination, source, bytes);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		std::string format_address(const std::uintptr_t address)
		{
			char buffer[2 + sizeof(address) * 2 + 1]{};
			sprintf_s(buffer, "0x%llX", static_cast<unsigned long long>(address));
			return buffer;
		}

		struct reference_counts
		{
			std::size_t table{};
			std::size_t target[pool_count]{};
			std::uint32_t first_target_rva[pool_count][4]{};
			std::size_t scanned_bytes{};
			bool faulted{};
		};

		// Counts 32-bit RIP-relative displacements in [start, end) that resolve into the
		// pool table, assuming the displacement ends the instruction (true for the
		// mov/lea forms the engine uses to address it). Only PODs here because of __try.
		void count_references(const std::uintptr_t module, const std::uintptr_t start,
			const std::uintptr_t end, const std::uintptr_t table, reference_counts& counts)
		{
			const auto table_end = table + sizeof(game::XAssetPool) * descriptor_count;
			__try
			{
				for (auto position = start; position + 4 <= end; ++position)
				{
					const auto displacement = *reinterpret_cast<const std::int32_t*>(position);
					const auto target = position + 4 + static_cast<std::intptr_t>(displacement);
					if (target < table || target >= table_end)
						continue;

					++counts.table;
					const auto index = (target - table) / sizeof(game::XAssetPool);
					for (std::size_t request = 0; request < pool_count; ++request)
					{
						if (index != static_cast<std::size_t>(requests[request].type))
							continue;
						const auto found = counts.target[request]++;
						if (found < std::size(counts.first_target_rva[request]))
							counts.first_target_rva[request][found] =
								static_cast<std::uint32_t>(position - module);
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				counts.faulted = true;
			}
		}

		// Evidence for the DB_XAssetPool RVA that only exists in the running, decrypted
		// image: correct engine code addresses the table directly from many sites.
		void scan_table_references()
		{
			const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
			const auto* section = IMAGE_FIRST_SECTION(nt);
			const IMAGE_SECTION_HEADER* code{};
			for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index)
			{
				if (section[index].Characteristics & IMAGE_SCN_MEM_EXECUTE)
				{
					code = &section[index];
					break;
				}
			}
			if (!code)
			{
				log("Code-reference scan skipped: no executable section found.", "warning");
				return;
			}

			reference_counts counts{};
			auto position = module + code->VirtualAddress;
			const auto end = position + code->Misc.VirtualSize;
			while (position < end && !counts.faulted)
			{
				MEMORY_BASIC_INFORMATION memory{};
				if (!VirtualQuery(reinterpret_cast<const void*>(position), &memory, sizeof(memory)))
					break;
				const auto region_end = (std::min)(end,
					reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize);
				if (is_readable_range(position, static_cast<std::size_t>(region_end - position)))
				{
					count_references(module, position, region_end, asset_pools_address, counts);
					counts.scanned_bytes += static_cast<std::size_t>(region_end - position);
				}
				position = region_end;
			}

			log("Code-reference scan: " + std::to_string(counts.table) +
				" RIP-relative references into the DB_XAssetPool table across " +
				std::to_string(counts.scanned_bytes) + " bytes of decrypted code" +
				(counts.faulted ? " (scan stopped at a fault)" : "") + ".");
			if (counts.table < 8)
				log("Code-reference scan found almost no references to the configured DB_XAssetPool "
					"address; the RVA is probably wrong for this executable.", "warning");

			for (std::size_t index = 0; index < pool_count; ++index)
			{
				std::string sites;
				const auto listed = (std::min)(counts.target[index], std::size(counts.first_target_rva[index]));
				for (std::size_t site = 0; site < listed; ++site)
					sites += (site ? "," : "") + format_address(counts.first_target_rva[index][site]);
				log(std::string("Code-reference scan: ") + requests[index].name + " descriptor has " +
					std::to_string(counts.target[index]) + " direct references" +
					(sites.empty() ? "" : " (first RVAs " + sites + ")") +
					". Direct references are type-specific code that may assume contiguous storage.",
					counts.target[index] ? "warning" : "info");
			}
		}

		bool diagnose_pool_table()
		{
			constexpr std::size_t table_size = sizeof(game::XAssetPool) * descriptor_count;
			std::array<game::XAssetPool, descriptor_count> descriptors{};
			const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
			std::uintptr_t checked_table_address{};

			log("Pool diagnostic: runtime module base=" + format_address(module) +
				", resolved pool-table address=" + format_address(asset_pools_address) + ".");

			if (!module || asset_pools_address < module ||
				!resolve_rva(asset_pools_address - module, table_size, false, false,
					checked_table_address) ||
				checked_table_address != asset_pools_address ||
				!is_readable_range(asset_pools_address, table_size))
			{
				log("Pool diagnostic failed: candidate pool table is outside the readable mapped module range.",
					"error");
				return false;
			}

			if (!guarded_copy(descriptors.data(),
				reinterpret_cast<const void*>(asset_pools_address), table_size))
			{
				log("Pool diagnostic failed: guarded read of the candidate pool table raised an exception.",
					"error");
				return false;
			}

			std::size_t validated{};
			std::string invalid_indices;
			for (std::size_t index = 0; index < descriptors.size(); ++index)
			{
				const auto& descriptor = descriptors[index];
				bool valid = descriptor.itemCount >= 0 && descriptor.itemAllocCount >= 0 &&
					descriptor.itemAllocCount <= descriptor.itemCount;

				if (descriptor.itemCount > 0)
				{
					const auto entry_size = static_cast<std::size_t>(descriptor.itemSize);
					const auto capacity = static_cast<std::size_t>(descriptor.itemCount);
					valid = valid && descriptor.pool && entry_size >= sizeof(game::AssetLink) &&
						capacity <= (std::numeric_limits<std::size_t>::max)() / entry_size;

					if (valid)
					{
						const auto allocation_bytes = capacity * entry_size;
						const auto pool_address = reinterpret_cast<std::uintptr_t>(descriptor.pool);
						valid = pool_address <=
							(std::numeric_limits<std::uintptr_t>::max)() - allocation_bytes &&
							is_readable_range(pool_address, 1);

						if (valid && descriptor.freeHead)
						{
							const auto free_head = reinterpret_cast<std::uintptr_t>(descriptor.freeHead);
							valid = free_head >= pool_address &&
								free_head - pool_address < allocation_bytes &&
								(free_head - pool_address) % entry_size == 0;
						}
					}
				}
				else
				{
					valid = valid && descriptor.itemAllocCount == 0;
				}

				if (valid)
				{
					++validated;
				}
				else
				{
					if (!invalid_indices.empty())
						invalid_indices += ",";
					invalid_indices += std::to_string(index);
				}
			}

			log("Pool diagnostic: validated " + std::to_string(validated) + "/" +
				std::to_string(descriptors.size()) + " descriptors.");
			if (!invalid_indices.empty())
				log("Pool diagnostic validation failed for descriptor indices: " +
					invalid_indices + ".", "warning");

			for (const auto& request : requests)
			{
				const auto index = static_cast<std::size_t>(request.type);
				const auto& descriptor = descriptors[index];
				const auto initialized = descriptor.pool && descriptor.itemSize > 0 &&
					descriptor.itemCount > 0;
				log(std::string("Pool diagnostic: ") + request.name + " index=" +
					std::to_string(index) + ", requested=" + std::to_string(request.requested_size) +
					", initialized=" + (initialized ? "true" : "false") +
					", itemSize=" + std::to_string(descriptor.itemSize) +
					" (expected " + std::to_string(request.item_size) + ")" +
					", capacity=" + std::to_string(descriptor.itemCount) +
					", allocated=" + std::to_string(descriptor.itemAllocCount) +
					", pool=" + format_address(reinterpret_cast<std::uintptr_t>(descriptor.pool)) +
					", freeHead=" + format_address(reinterpret_cast<std::uintptr_t>(descriptor.freeHead)) + ".");
			}
			return validated == descriptors.size();
		}

		bool extend_pool(pool_request& request)
		{
			if (!request.requested_size)
				return true;

			auto* pool = get_pool(request.type);
			if (!pool)
			{
				log(std::string(request.name) + ": pool metadata is unavailable.", "error");
				return false;
			}

			const auto original_size = std::atomic_ref<std::int32_t>(pool->itemCount).load();
			request.runtime_original = original_size;
			request.final_size = original_size;
			log(std::string(request.name) + ": original capacity=" + std::to_string(original_size) +
				", requested=" + std::to_string(request.requested_size) +
				", allocated=" + std::to_string(pool->itemAllocCount) +
				", itemSize=" + std::to_string(pool->itemSize) +
				", storage=" + format_address(reinterpret_cast<std::uintptr_t>(pool->pool)) +
				", freeHead=" + format_address(reinterpret_cast<std::uintptr_t>(pool->freeHead)) + ".");

			const auto report = [&request](const char* outcome, const std::string& level)
			{
				log(std::string(request.name) + ": outcome=" + outcome +
					"; original=" + std::to_string(request.runtime_original) +
					", requested=" + std::to_string(request.requested_size) +
					", final=" + std::to_string(request.final_size) + ".", level);
			};

			if (original_size <= 0 || !pool->pool)
			{
				report("not initialized; unchanged", "error");
				return false;
			}
			if (pool->itemSize != request.item_size)
			{
				report("unexpected itemSize, descriptor does not match this pool; unchanged", "error");
				return false;
			}
			if (pool->isSingleton[0])
			{
				report("singleton pool; unchanged", "error");
				return false;
			}

			const auto count = pool_extension::extension_count(original_size, request.requested_size);
			if (!count)
			{
				report("unchanged, capacity already meets the request", "info");
				return true;
			}

			request.bounds = {reinterpret_cast<std::uintptr_t>(pool->pool),
				static_cast<std::size_t>(original_size), pool->itemSize, {}};
			const auto before = pool_extension::walk(*pool, request.bounds);
			if (!before.valid)
			{
				report("free list failed validation before extension; unchanged", "error");
				return false;
			}

			auto* storage = pool_extension::allocate_block(count, request.bounds.item_size);
			if (!storage)
			{
				report("allocation failed; unchanged", "error");
				return false;
			}
			auto* first = pool_extension::link_block(storage, count, request.bounds.item_size);
			request.bounds.blocks.push_back({storage, count});

			const auto status = pool_extension::append(*pool, request.bounds, first);
			if (status != pool_extension::append_status::appended &&
				status != pool_extension::append_status::appended_to_empty)
			{
				// Never linked, so nothing in the engine can reference the block.
				request.bounds.blocks.clear();
				::operator delete(storage, std::align_val_t{pool_extension::block_alignment});
				log(std::string(request.name) + ": append failed: " + pool_extension::to_string(status) + ".",
					"error");
				report("free-list append failed; unchanged", "error");
				return false;
			}

			request.final_size = original_size + static_cast<std::int32_t>(count);
			std::atomic_ref<std::int32_t>(pool->itemCount).store(request.final_size);
			request.extended = true;

			const auto after = pool_extension::walk(*pool, request.bounds, first);
			log(std::string(request.name) + ": appended " + std::to_string(count) + " entries at " +
				format_address(reinterpret_cast<std::uintptr_t>(storage)) + " (" +
				std::to_string(count * request.bounds.item_size) + " bytes), " +
				pool_extension::to_string(status) + "; free entries before=" +
				std::to_string(before.length) + ", after=" + std::to_string(after.length) + ".");
			if (!after.valid || !after.found_target)
				log(std::string(request.name) + ": the new block was not observed on the free list after the "
					"append; the engine may have consumed or overwritten its link concurrently.", "warning");

			report(after.valid && after.found_target ? "extended" : "extended, unconfirmed", "info");
			return true;
		}

		bool apply_requests()
		{
			bool all_applied = true;
			for (auto& request : requests)
				all_applied = extend_pool(request) && all_applied;
			return all_applied;
		}

		// Read-only polling of the extended descriptors, so the log shows when the engine
		// actually hands out entries beyond the original capacity.
		void monitor_pools(const std::uint32_t interval_ms)
		{
			std::array<std::int32_t, pool_count> high_water{};
			std::array<bool, pool_count> active{};
			std::array<bool, pool_count> exhausted_reported{};
			for (std::size_t index = 0; index < pool_count; ++index)
				active[index] = requests[index].extended;

			while (std::any_of(active.begin(), active.end(), [](const bool value) { return value; }))
			{
				Sleep(interval_ms);
				for (std::size_t index = 0; index < pool_count; ++index)
				{
					if (!active[index])
						continue;
					const auto& request = requests[index];
					auto* pool = get_pool(request.type);
					const auto capacity = std::atomic_ref<std::int32_t>(pool->itemCount).load();
					const auto allocated = std::atomic_ref<std::int32_t>(pool->itemAllocCount).load();
					auto* free_head = pool_extension::link_ref(pool->freeHead).load();

					if (capacity != request.final_size)
					{
						log(std::string(request.name) + ": capacity changed from " +
							std::to_string(request.final_size) + " to " + std::to_string(capacity) +
							" outside T7Unchained; the pool may have been re-initialized. Monitoring stopped.",
							"warning");
						active[index] = false;
						continue;
					}

					if (allocated > high_water[index] && allocated > request.runtime_original)
					{
						log(std::string(request.name) + ": " + std::to_string(allocated) +
							" entries allocated, beyond the original capacity " +
							std::to_string(request.runtime_original) + " (final capacity " +
							std::to_string(capacity) + "); extension entries are in use.");
					}
					high_water[index] = (std::max)(high_water[index], allocated);

					if (!free_head && !exhausted_reported[index])
					{
						exhausted_reported[index] = true;
						log(std::string(request.name) + ": free list is empty with " +
							std::to_string(allocated) + "/" + std::to_string(capacity) +
							" entries allocated.", "warning");
					}
				}
			}
		}

		void finish_extension()
		{
			if (std::none_of(requests.begin(), requests.end(),
				[](const pool_request& request) { return request.extended; }))
				return;

			// The engine now points into memory this module allocated and the monitor runs
			// this module's code; keep it loaded if Lua releases its library handle.
			HMODULE self{};
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
				reinterpret_cast<LPCWSTR>(reinterpret_cast<const void*>(&finish_extension)), &self))
				log("Could not pin T7Unchained.dll in memory; pool monitoring is disabled.", "warning");
			else if (monitor_interval_ms && !monitor_started)
			{
				monitor_started = true;
				std::thread(monitor_pools, monitor_interval_ms).detach();
				log("Pool monitor started; interval=" + std::to_string(monitor_interval_ms) + "ms.");
			}
		}

		void __fastcall asset_pool_init_stub()
		{
			reinterpret_cast<void(__fastcall*)()>(asset_pool_init_original)();
			log("DB_AssetPoolInit completed; applying configured asset-pool requests.");
			apply_requests();
			finish_extension();
		}

		bool load_config(const std::filesystem::path& config_file)
		{
			std::ifstream input(config_file);
			if (!input)
			{
				log(std::string("Configuration file could not be opened: ") + config_file.string() +
					". Asset pools will remain unchanged.", "error");
				return false;
			}
			log(std::string("Reading configuration from ") + config_file.string() + ".");

			std::vector<std::string> lines;
			for (std::string line; std::getline(input, line);)
				lines.push_back(trim(line));

			const auto section_name = [](const std::string& line) -> std::optional<std::string>
			{
				if (line.size() < 3 || line.front() != '[' || line.back() != ']')
					return std::nullopt;
				return lower(trim(line.substr(1, line.size() - 2)));
			};

			// Only one settings section is applied, so a file carrying both names is never merged.
			std::vector<std::string> sections;
			for (const auto& line : lines)
			{
				if (const auto name = section_name(line))
					sections.push_back(*name);
			}
			const auto settings = game::config_names::select_settings_section(sections);
			const auto ignored_settings = settings == game::config_names::settings_section ?
				game::config_names::legacy_settings_section : game::config_names::settings_section;
			if (settings == game::config_names::legacy_settings_section)
				log("Deprecated: the [T7Overclocked] section is being used because [T7Unchained] is absent. "
					"Rename it to [T7Unchained]; the legacy name will stop being read in a future release.",
					"warning");
			else if (settings == game::config_names::settings_section &&
				std::find(sections.begin(), sections.end(), ignored_settings) != sections.end())
				log("Both [T7Unchained] and the deprecated [T7Overclocked] sections exist; only "
					"[T7Unchained] is applied.", "warning");

			std::string section;
			for (const auto& line : lines)
			{
				if (line.empty() || line.front() == '#' || line.front() == ';')
					continue;

				if (line.front() == '[' || line.back() == ']')
				{
					const auto name = section_name(line);
					if (!name)
					{
						log(std::string("Malformed configuration section: ") + line + ".", "warning");
						continue;
					}
					section = *name;
					continue;
				}

				const auto separator = line.find('=');
				if (separator == std::string::npos)
				{
					log(std::string("Malformed configuration entry: ") + line + ".", "warning");
					continue;
				}

				const auto key = lower(trim(line.substr(0, separator)));
				const auto value = trim(line.substr(separator + 1));
				if (!settings.empty() && section == settings)
				{
					if (key == "enabled")
					{
						if (!parse_bool(value, enabled))
						{
							enabled = false;
							log("Invalid [T7Unchained] Enabled value; expected true or false.", "warning");
						}
					}
					else if (key == "loglevel")
					{
						log_level = lower(value);
						if (log_level != "debug" && log_level != "info" &&
							log_level != "warning" && log_level != "error")
						{
							log("Invalid LogLevel; using info.", "warning");
							log_level = "info";
						}
					}
					else if (key == "diagnosticonly")
					{
						if (!parse_bool(value, diagnostics_only))
						{
							diagnostics_only = true;
							log("Invalid DiagnosticOnly value; enabling diagnostic-only safety mode.",
								"warning");
						}
					}
					else if (key == "monitorintervalms")
					{
						const auto parsed = parse_size(value);
						if (!parsed || (*parsed && *parsed < 100))
							log("Invalid MonitorIntervalMs; expected 0 (off) or at least 100. Using " +
								std::to_string(monitor_interval_ms) + ".", "warning");
						else
							monitor_interval_ms = *parsed;
					}
					else
					{
						log(std::string("Unsupported [T7Unchained] key: ") + key + ".", "warning");
					}
				}
				else if (section == ignored_settings)
				{
					continue;
				}
				else if (section == "assetpools")
				{
					const auto request = std::find_if(requests.begin(), requests.end(),
						[&key](const pool_request& pool)
						{
							return key == pool.name;
						});
					if (request == requests.end())
					{
						const auto parsed = parse_size(value);
						if (!parsed)
							log(std::string("Invalid capacity for ") + key + ": " + value + ".", "warning");
						if (parsed && *parsed == 0 && (key == "weapon" || key == "xmodel"))
						{
							log(std::string("Configuration: ") + key +
								" is unsupported but requests no change (capacity=0).");
							continue;
						}
						log(std::string("Unsupported asset pool '") + key +
							"'; this build currently supports attachment and attachmentunique only.",
							"warning");
						continue;
					}

					const auto parsed = parse_size(value);
					if (!parsed)
					{
						log(std::string("Invalid capacity for ") + key + ": " + value + ".", "warning");
						continue;
					}
					request->requested_size = *parsed;
					log(std::string("Configuration: ") + key + " requested capacity=" +
						std::to_string(request->requested_size) + ".");
				}
				else
				{
					log(std::string("Unsupported configuration section: ") + section + ".", "warning");
				}
			}

			return true;
		}
	}

	bool initialize(const std::filesystem::path& config_file,
		const std::filesystem::path& offsets_file)
	{
		if (initialized_once)
			return true;
		initialized_once = true;

		offsets_file_path = offsets_file;
		if (!load_config(config_file))
			return false;
		if (!enabled)
		{
			log("Asset pool expansion disabled by configuration.");
			return true;
		}

		bool has_request = false;
		for (const auto& request : requests)
			has_request |= request.requested_size != 0;
		if (!has_request)
		{
			log("No asset-pool expansions requested.");
			return true;
		}
		if (!load_offsets())
			return false;

		const auto table_valid = diagnose_pool_table();
		scan_table_references();
		if (diagnostics_only)
		{
			log("DiagnosticOnly=true; no pool changes or hooks were attempted.");
			return table_valid;
		}

		bool initialized = true;
		for (const auto& request : requests)
		{
			if (request.requested_size == 0)
				continue;
			const auto* pool = get_pool(request.type);
			initialized &= pool && pool->itemCount > 0 && pool->pool;
		}

		if (initialized)
		{
			// The normal case for a Lua-loaded DLL: zones are loaded and live assets point
			// into the stock storage, so capacity is added without moving anything.
			log("Asset pools are already initialized; extending free lists in place.");
			const auto applied = apply_requests();
			finish_extension();
			return applied;
		}

		std::uintptr_t init_rva{};
		if (!read_rva(build_key("DB_AssetPoolInit"), init_rva) ||
			!resolve_rva(init_rva, 1, true, false, asset_pool_init_address))
		{
			log("Asset pool expansion disabled: pools are not initialized and DB_AssetPoolInit is not "
				"configured or is not executable.", "error");
			return false;
		}

		if (!initialize_minhook())
			return false;

		auto status = MH_CreateHook(reinterpret_cast<void*>(asset_pool_init_address),
			reinterpret_cast<void*>(&asset_pool_init_stub), &asset_pool_init_original);
		if (status != MH_OK)
		{
			log("Asset pool expansion disabled: MinHook could not create the DB_AssetPoolInit hook.",
				"error");
			return false;
		}
		status = MH_EnableHook(reinterpret_cast<void*>(asset_pool_init_address));
		if (status != MH_OK)
		{
			MH_RemoveHook(reinterpret_cast<void*>(asset_pool_init_address));
			asset_pool_init_original = nullptr;
			log("Asset pool expansion disabled: MinHook could not enable the DB_AssetPoolInit hook.",
				"error");
			return false;
		}

		log(std::string("Installed DB_AssetPoolInit hook for ") +
			game::offsets::build_name() + ".");
		return true;
	}
}
