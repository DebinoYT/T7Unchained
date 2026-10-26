# Contributing to T7Unchained

## Prerequisites

- Windows 10 or later, x64.
- Visual Studio 2022 or later with the **Desktop development with C++**
  workload, the MSVC v143 toolset and a Windows 10/11 SDK.
- Git, to fetch the MinHook submodule.
- A Steam copy of Black Ops III, for in-game testing only. Building and the
  offline tests do not need the game.

No Python or other build tools are required; Premake is included in
`tools/premake5.exe`.

## Building

```bat
git clone --recurse-submodules <repository-url> T7Unchained
cd T7Unchained
generate.bat
```

`generate.bat` initializes the submodules and generates
`build\T7Unchained.sln` (`tools\premake5.exe vs2022`). Open the solution in
Visual Studio, or build from a Developer Command Prompt:

```bat
msbuild build\T7Unchained.sln /p:Configuration=Release /p:Platform=x64
```

Output goes to `build\bin\x64\<Debug|Release>\`. The DLL is `T7Unchained.dll`.

The optional `--copy-to=<folder>` premake option adds a post-build step that
copies the DLL to that folder. It is off by default; only use it for a test
mod you are prepared to overwrite.

## Tests

```bat
build\bin\x64\Release\T7Unchained.Tests.exe
```

The offline tests need no game and cover:

- the `XAssetPool` descriptor layout (checked at compile time);
- capacity calculation, block allocation and free-list linking;
- extending partly used, fragmented and exhausted pools, rejecting corrupt or
  cyclic free lists, and a concurrent-allocator stress test;
- selection between `T7Unchained.ini` and the deprecated `T7Overclocked.ini`,
  and between the `[T7Unchained]` and `[T7Overclocked]` sections.

The process exits with a non-zero code if any check fails.

## Testing in game

1. Copy `T7Unchained.dll`, `T7Unchained.ini` and `offsets.txt` into
   `Black Ops III/mods/<mod>/zone/`.
2. Make the mod's Lua UI code load the DLL; see
   [examples/lua/t7unchained_loader.lua](examples/lua/t7unchained_loader.lua).
3. Launch the mod, then read `mods/<mod>/logs/T7Unchained.log`. If it is
   missing, check `%TEMP%\T7Unchained_boot_*.log`.

When reporting a result, include the log, the BO3 build it reports, your
`T7Unchained.ini`, and whether the game reached the point that previously
failed. A successful build or a successful extension line in the log is not
by itself evidence that the content works in game.

## Ground rules

- Do not commit game files (`.ff`, `.xpak`, executables, crash dumps),
  compiled binaries, or third-party source without a compatible license.
- Treat offsets as unverified until they have been confirmed against the
  running executable, and say which build you tested.
- Describe features as working only when there is in-game evidence for them.
