#pragma once

#include <filesystem>

namespace asset_limits
{
	bool initialize(const std::filesystem::path& config_file,
		const std::filesystem::path& offsets_file);
}
