#pragma once

#include <filesystem>
#include <string>

#include "config_names.hpp"

namespace game::active_mod_path
{
	struct paths
	{
		std::filesystem::path dll_file;
		std::filesystem::path mod_directory;
		std::filesystem::path zone_directory;
		std::filesystem::path config_file;
		config_names::config_source config_source{};
		std::filesystem::path offsets_file;
		std::filesystem::path log_file;
	};

	bool resolve(HMODULE module, paths& result, std::string& error);
}
