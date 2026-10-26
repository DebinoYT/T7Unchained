#include <std_include.hpp>

#include "active_mod_path.hpp"
#include "config_names.hpp"

namespace game::active_mod_path
{
	namespace
	{
		bool get_module_path(const HMODULE module, std::filesystem::path& path, std::string& error)
		{
			if (!module)
			{
				error = "The loaded DLL module handle is null.";
				return false;
			}

			std::array<wchar_t, 32768> buffer{};
			SetLastError(ERROR_SUCCESS);
			const auto length = GetModuleFileNameW(module, buffer.data(),
				static_cast<DWORD>(buffer.size()));
			if (length == 0)
			{
				error = "GetModuleFileNameW failed with Win32 error " +
					std::to_string(GetLastError()) + ".";
				return false;
			}
			if (length >= buffer.size())
			{
				error = "The loaded DLL path exceeds the supported path buffer.";
				return false;
			}

			path = std::filesystem::path(buffer.data(), buffer.data() + length);
			if (!path.is_absolute())
			{
				error = "GetModuleFileNameW returned a non-absolute DLL path.";
				return false;
			}
			return true;
		}
	}

	bool resolve(const HMODULE module, paths& result, std::string& error)
	{
		error.clear();
		result = {};
		if (!get_module_path(module, result.dll_file, error))
			return false;

		std::error_code filesystem_error;
		result.dll_file = std::filesystem::weakly_canonical(result.dll_file, filesystem_error);
		if (filesystem_error)
		{
			error = "Could not normalize the loaded DLL path: " + filesystem_error.message() + ".";
			return false;
		}

		result.zone_directory = result.dll_file.parent_path();
		auto zone_name = result.zone_directory.filename().wstring();
		std::transform(zone_name.begin(), zone_name.end(), zone_name.begin(),
			[](const wchar_t character)
			{
				return static_cast<wchar_t>(std::towlower(character));
			});
		if (zone_name != L"zone")
		{
			error = "The DLL is not installed in a directory named 'zone'. DLL path: " +
				result.dll_file.string() + ".";
			return false;
		}

		if (!std::filesystem::is_directory(result.zone_directory, filesystem_error))
		{
			error = "The DLL's zone directory is unavailable: " +
				result.zone_directory.string();
			if (filesystem_error)
				error += " (" + filesystem_error.message() + ")";
			return false;
		}

		result.mod_directory = result.zone_directory.parent_path();
		if (result.mod_directory.empty() ||
			!std::filesystem::is_directory(result.mod_directory, filesystem_error) ||
			filesystem_error)
		{
			error = "The parent mod directory could not be validated: " +
				result.mod_directory.string();
			if (filesystem_error)
				error += " (" + filesystem_error.message() + ")";
			return false;
		}

		auto mods_directory = result.mod_directory.parent_path();
		auto mods_name = mods_directory.filename().wstring();
		std::transform(mods_name.begin(), mods_name.end(), mods_name.begin(),
			[](const wchar_t character)
			{
				return static_cast<wchar_t>(std::towlower(character));
			});
		if (mods_name != L"mods")
		{
			error = "The DLL is not installed under a mods/<mod>/zone directory: " +
				result.dll_file.string() + ".";
			return false;
		}
		if (!std::filesystem::is_directory(mods_directory, filesystem_error) || filesystem_error)
		{
			error = "The parent mods directory could not be validated: " +
				mods_directory.string();
			if (filesystem_error)
				error += " (" + filesystem_error.message() + ")";
			return false;
		}

		const auto config = config_names::select_config_file(result.zone_directory);
		result.config_file = config.file;
		result.config_source = config.source;
		result.offsets_file = result.zone_directory / L"offsets.txt";
		result.log_file = result.mod_directory / L"logs" / L"T7Unchained.log";
		return true;
	}
}
