// Offline tests for the T7Unchained / deprecated T7Overclocked name selection
// in game/config_names.hpp.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "game/config_names.hpp"
#include "tests.hpp"

namespace
{
	using game::config_names::config_source;

	struct temporary_zone
	{
		std::filesystem::path directory;

		temporary_zone()
		{
			directory = std::filesystem::temp_directory_path() /
				("t7unchained_tests_" + std::to_string(
					std::chrono::steady_clock::now().time_since_epoch().count())) / "zone";
			std::filesystem::create_directories(directory);
		}

		~temporary_zone()
		{
			std::error_code error;
			std::filesystem::remove_all(directory.parent_path(), error);
		}

		void create(const wchar_t* name) const
		{
			std::ofstream(directory / name) << "[T7Unchained]\n";
		}
	};

	void test_config_file_selection()
	{
		{
			const temporary_zone zone;
			const auto selection = game::config_names::select_config_file(zone.directory);
			CHECK(selection.source == config_source::missing);
			CHECK(selection.file == zone.directory / L"T7Unchained.ini");
		}
		{
			const temporary_zone zone;
			zone.create(L"T7Unchained.ini");
			const auto selection = game::config_names::select_config_file(zone.directory);
			CHECK(selection.source == config_source::current);
			CHECK(selection.file == zone.directory / L"T7Unchained.ini");
		}
		{
			const temporary_zone zone;
			zone.create(L"T7Overclocked.ini");
			const auto selection = game::config_names::select_config_file(zone.directory);
			CHECK(selection.source == config_source::legacy);
			CHECK(selection.file == zone.directory / L"T7Overclocked.ini");
		}
		{
			const temporary_zone zone;
			zone.create(L"T7Unchained.ini");
			zone.create(L"T7Overclocked.ini");
			const auto selection = game::config_names::select_config_file(zone.directory);
			CHECK(selection.source == config_source::legacy_ignored);
			CHECK(selection.file == zone.directory / L"T7Unchained.ini");
		}
		{
			// A directory with the config name is not a configuration file.
			const temporary_zone zone;
			std::filesystem::create_directory(zone.directory / L"T7Unchained.ini");
			zone.create(L"T7Overclocked.ini");
			CHECK(game::config_names::select_config_file(zone.directory).source == config_source::legacy);
		}
	}

	void test_settings_section_selection()
	{
		using sections = std::vector<std::string>;
		CHECK(game::config_names::select_settings_section(sections{"t7unchained", "assetpools"}) == "t7unchained");
		CHECK(game::config_names::select_settings_section(sections{"t7overclocked", "assetpools"}) == "t7overclocked");
		CHECK(game::config_names::select_settings_section(sections{"t7overclocked", "t7unchained"}) == "t7unchained");
		CHECK(game::config_names::select_settings_section(sections{"t7unchained", "t7overclocked"}) == "t7unchained");
		CHECK(game::config_names::select_settings_section(sections{"assetpools"}).empty());
		CHECK(game::config_names::select_settings_section(sections{}).empty());
	}
}

void tests::run_config_names_tests()
{
	test_config_file_selection();
	test_settings_section_selection();
}
