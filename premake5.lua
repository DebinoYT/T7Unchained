dependencies = {
	basePath = "./deps"
}

function dependencies.load()
	local scripts = os.matchfiles(path.join(dependencies.basePath, "premake/*.lua"))
	for _, script in pairs(scripts) do
		local module = script:gsub("%.lua$", "")
		require(module)
	end
end

newoption {
	trigger = "copy-to",
	description = "Copy the built DLL to this directory.",
	value = "PATH"
}

dependencies.load()

workspace "T7Unchained"
startproject "T7Unchained"
location "./build"
objdir "%{wks.location}/obj"
targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}"

configurations {"Debug", "Release"}
language "C++"
cppdialect "C++20"
architecture "x86_64"
platforms "x64"
systemversion "latest"
symbols "On"
staticruntime "On"
editandcontinue "Off"
warnings "Extra"
characterset "ASCII"

if os.getenv("CI") then
	defines {"CI"}
end

flags {"NoIncrementalLink", "NoMinimalRebuild", "MultiProcessorCompile", "No64BitChecks"}

filter "platforms:x64"
	defines {"_WINDOWS", "WIN32"}
filter {}

filter "configurations:Release"
	optimize "Size"
	buildoptions {"/GL"}
	linkoptions {"/IGNORE:4702", "/LTCG"}
	defines {"NDEBUG"}
filter {}

filter "configurations:Debug"
	optimize "Debug"
	defines {"DEBUG", "_DEBUG"}
filter {}

project "T7Unchained"
kind "SharedLib"
language "C++"
targetname "T7Unchained"

pchheader "std_include.hpp"
pchsource "src/client/std_include.cpp"

linkoptions {"/IGNORE:4254", "/IGNORE:4244", "/DYNAMICBASE:NO", "/SAFESEH:NO", "/LARGEADDRESSAWARE", "/LAST:.main", "/PDBCompress"}

files {
	"./src/client/main.cpp",
	"./src/client/std_include.cpp",
	"./src/client/std_include.hpp",
	"./src/client/components/asset_limits.cpp",
	"./src/client/components/asset_limits.hpp",
	"./src/client/components/pool_extension.hpp",
	"./src/client/game/active_mod_path.cpp",
	"./src/client/game/active_mod_path.hpp",
	"./src/client/game/config_names.hpp",
	"./src/client/game/game.cpp",
	"./src/client/game/game.hpp",
	"./src/client/game/offsets.cpp",
	"./src/client/game/offsets.hpp",
	"./src/client/game/structs.hpp"
}

includedirs {"./src/client", "./src/common", "%{prj.location}/src"}
resincludedirs {"$(ProjectDir)src"}

minhook.import()

if _OPTIONS["copy-to"] then
	postbuildcommands {"copy /y \"$(TargetPath)\" \"" .. _OPTIONS["copy-to"] .. "\""}
end

project "T7Unchained.Tests"
kind "ConsoleApp"
language "C++"
targetname "T7Unchained.Tests"

files {
	"./tests/pool_extension_tests.cpp",
	"./tests/config_names_tests.cpp",
	"./tests/tests.cpp",
	"./tests/tests.hpp",
	"./src/client/game/config_names.hpp",
	"./src/client/components/pool_extension.hpp",
	"./src/client/game/structs.hpp"
}

includedirs {"./src/client"}

group "Dependencies"
minhook.project()
