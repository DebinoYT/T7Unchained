#include <std_include.hpp>

#include "components/asset_limits.hpp"
#include "game/active_mod_path.hpp"
#include "game/game.hpp"

namespace lua
{
	struct lua_State;
}

namespace
{
	class boot_diagnostic
	{
	public:
		bool open()
		{
			if (file_ != INVALID_HANDLE_VALUE)
				return true;

			wchar_t temp_directory[32768]{};
			const auto length = GetTempPathW(static_cast<DWORD>(std::size(temp_directory)),
				temp_directory);
			if (!length || length >= std::size(temp_directory))
			{
				OutputDebugStringA("[T7Unchained] Could not resolve Windows temporary directory.\n");
				return false;
			}

			wchar_t file_path[32768]{};
			const auto path_length = ::swprintf_s(file_path, std::size(file_path),
				L"%sT7Unchained_boot_%lu_%llu.log", temp_directory,
				static_cast<unsigned long>(GetCurrentProcessId()),
				static_cast<unsigned long long>(GetTickCount64()));
			if (path_length < 0 || static_cast<std::size_t>(path_length) >= std::size(file_path))
			{
				OutputDebugStringA("[T7Unchained] Temporary boot diagnostic path is too long.\n");
				return false;
			}

			file_ = CreateFileW(file_path, FILE_APPEND_DATA,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file_ == INVALID_HANDLE_VALUE)
			{
				const auto error = GetLastError();
				char message[128]{};
				sprintf_s(message, "[T7Unchained] Could not create temporary boot diagnostic (Win32 error %lu).\n",
					static_cast<unsigned long>(error));
				OutputDebugStringA(message);
				return false;
			}

			write("Initialization entry reached.");
			write("Process ID: " + std::to_string(GetCurrentProcessId()) + ".");
			write("Temporary diagnostic file created.");
			return true;
		}

		~boot_diagnostic()
		{
			if (file_ != INVALID_HANDLE_VALUE)
				CloseHandle(file_);
		}

		void write(const std::string& message)
		{
			const auto line = std::string("[T7Unchained] ") + message + "\r\n";
			OutputDebugStringA(line.c_str());
			if (file_ == INVALID_HANDLE_VALUE)
				return;

			DWORD written{};
			const auto length = static_cast<DWORD>(line.size());
			if (!WriteFile(file_, line.data(), length, &written, nullptr) || written != length)
			{
				OutputDebugStringA("[T7Unchained] Failed writing temporary boot diagnostic.\n");
				return;
			}
			if (!FlushFileBuffers(file_))
				OutputDebugStringA("[T7Unchained] Failed flushing temporary boot diagnostic.\n");
		}

		void write_paths(const game::active_mod_path::paths& paths)
		{
			write(L"DLL: " + paths.dll_file.wstring());
			write(L"Zone: " + paths.zone_directory.wstring());
			write(L"Mod root: " + paths.mod_directory.wstring());
			write(L"Config: " + paths.config_file.wstring());
			write(L"Offsets: " + paths.offsets_file.wstring());
			write(L"Log: " + paths.log_file.wstring());
		}

		void write(const std::wstring& message)
		{
			const auto line = L"[T7Unchained] " + message + L"\r\n";
			OutputDebugStringW(line.c_str());
			if (file_ == INVALID_HANDLE_VALUE)
				return;

			std::array<char, 8192> utf8{};
			const auto length = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1,
				utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
			if (!length)
			{
				OutputDebugStringA("[T7Unchained] Failed encoding path in boot diagnostic.\n");
				return;
			}

			DWORD written{};
			const auto bytes = static_cast<DWORD>(length - 1);
			if (!WriteFile(file_, utf8.data(), bytes, &written, nullptr) || written != bytes)
			{
				OutputDebugStringA("[T7Unchained] Failed writing path to boot diagnostic.\n");
				return;
			}
			if (!FlushFileBuffers(file_))
				OutputDebugStringA("[T7Unchained] Failed flushing temporary boot diagnostic.\n");
		}

	private:
		HANDLE file_{INVALID_HANDLE_VALUE};
	};

	void report_startup(boot_diagnostic& diagnostic, const std::string& message,
		const bool normal_log_open)
	{
		const auto line = std::string("[T7Unchained] ") + message;
		diagnostic.write(message);
		if (normal_log_open)
			game::minlog.WriteLine(line.c_str());
	}

	void initialize_from_mod_directory(boot_diagnostic& diagnostic,
		const game::active_mod_path::paths& paths)
	{
		std::string log_open_error;
		try
		{
			if (!game::minlog.Open(paths.log_file, &log_open_error))
			{
				report_startup(diagnostic,
					std::string("Could not create the active mod log: ") +
					(log_open_error.empty() ? "file open failed" : log_open_error), false);
				return;
			}
		}
		catch (const std::exception& exception)
		{
			report_startup(diagnostic,
				std::string("Active mod log initialization failed: ") + exception.what(), false);
			return;
		}

		try
		{
			report_startup(diagnostic, "DLL: " + paths.dll_file.string(), true);
			report_startup(diagnostic, "Zone: " + paths.zone_directory.string(), true);
			report_startup(diagnostic, "Mod root: " + paths.mod_directory.string(), true);
			report_startup(diagnostic, "Config: " + paths.config_file.string(), true);
			report_startup(diagnostic, "Offsets: " + paths.offsets_file.string(), true);
			report_startup(diagnostic, "Log: " + paths.log_file.string(), true);
			report_startup(diagnostic,
				std::string("BO3 build: ") + game::offsets::build_name(), true);
			if (paths.config_source == game::config_names::config_source::legacy)
				report_startup(diagnostic, "Deprecated: T7Unchained.ini was not found, so the legacy "
					"T7Overclocked.ini is being used. Rename it to T7Unchained.ini; the legacy name "
					"will stop being read in a future release.", true);
			else if (paths.config_source == game::config_names::config_source::legacy_ignored)
				report_startup(diagnostic, "Both T7Unchained.ini and the deprecated T7Overclocked.ini "
					"exist; only T7Unchained.ini is read. Delete T7Overclocked.ini.", true);
			report_startup(diagnostic, "Loading configuration and asset-pool offsets.", true);

			if (!asset_limits::initialize(paths.config_file, paths.offsets_file))
				report_startup(diagnostic,
					"Asset-pool initialization did not complete; see preceding messages.", true);
			else
				report_startup(diagnostic, "Initialization sequence completed.", true);
		}
		catch (const std::exception& exception)
		{
			report_startup(diagnostic,
				std::string("Initialization threw an exception: ") + exception.what(), true);
		}
	}
}

extern "C"
{
	int __declspec(dllexport) init(lua::lua_State*)
	{
		static boot_diagnostic diagnostic;
		if (!diagnostic.open())
			OutputDebugStringA("[T7Unchained] Initialization entered, but boot diagnostic creation failed.\n");

		diagnostic.write("Checking the BO3 executable build.");
		const auto build = game::offsets::active_build();
		if (build == game::offsets::build::unknown)
		{
			report_startup(diagnostic,
				"Unsupported Black Ops 3 executable; asset-pool hooks were not installed.", false);
			return 1;
		}

		diagnostic.write("Supported BO3 build detected; resolving the DLL installation path.");
		HMODULE own_module{};
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(reinterpret_cast<const void*>(&init)), &own_module))
		{
			diagnostic.write("Could not identify the loaded T7Unchained DLL module (Win32 error " +
				std::to_string(GetLastError()) + ").");
			return 1;
		}

		game::active_mod_path::paths paths;
		std::string path_error;
		try
		{
			if (!game::active_mod_path::resolve(own_module, paths, path_error))
			{
				if (!paths.dll_file.empty())
					diagnostic.write(L"DLL: " + paths.dll_file.wstring());
				diagnostic.write("DLL path-based mod resolution failed: " + path_error);
				return 1;
			}
			diagnostic.write_paths(paths);
			initialize_from_mod_directory(diagnostic, paths);
		}
		catch (const std::exception& exception)
		{
			diagnostic.write("DLL path initialization failed: " + std::string(exception.what()));
		}
		return 1;
	}
}
