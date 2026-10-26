#pragma once

// Configuration names, including the T7Overclocked names that remain accepted
// as deprecated fallbacks for one release. Standard library only so the
// selection rules can be tested offline.

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace game::config_names
{
	inline constexpr wchar_t config_file[] = L"T7Unchained.ini";
	inline constexpr wchar_t legacy_config_file[] = L"T7Overclocked.ini";

	// Section names as compared by the parser (lower case).
	inline constexpr char settings_section[] = "t7unchained";
	inline constexpr char legacy_settings_section[] = "t7overclocked";

	enum class config_source
	{
		current,        // T7Unchained.ini
		legacy,         // T7Overclocked.ini, because T7Unchained.ini is absent
		legacy_ignored, // T7Unchained.ini, with an unused T7Overclocked.ini beside it
		missing         // neither exists; T7Unchained.ini is reported as missing
	};

	struct config_selection
	{
		std::filesystem::path file;
		config_source source{};
	};

	// Exactly one file is ever read, so two configurations are never merged.
	inline config_selection select_config_file(const std::filesystem::path& zone_directory)
	{
		std::error_code error;
		const auto current = zone_directory / config_file;
		const auto legacy = zone_directory / legacy_config_file;
		const auto has_current = std::filesystem::is_regular_file(current, error);
		const auto has_legacy = std::filesystem::is_regular_file(legacy, error);

		if (has_current)
			return {current, has_legacy ? config_source::legacy_ignored : config_source::current};
		if (has_legacy)
			return {legacy, config_source::legacy};
		return {current, config_source::missing};
	}

	// Chooses which section supplies the general settings from the lower-cased
	// section names in a file. [T7Unchained] wins; [T7Overclocked] is used only
	// when [T7Unchained] is absent. Returns an empty string if neither exists.
	inline std::string select_settings_section(const std::vector<std::string>& sections)
	{
		const auto has = [&sections](const char* name)
		{
			return std::find(sections.begin(), sections.end(), name) != sections.end();
		};
		if (has(settings_section))
			return settings_section;
		if (has(legacy_settings_section))
			return legacy_settings_section;
		return {};
	}
}
