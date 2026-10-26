# Credits

## Upstream project

T7Unchained is derived from **T7Overcharged** and keeps its GPLv3 license and
commit history.

- [T7Overcharged](https://github.com/JariKCoding/T7Overcharged) by
  JariKCoding, maintained by [Scroptss](https://github.com/Scroptss/T7Overcharged),
  with contributions from Michael Akopyan (iAmThatMichael) and Tex
  (ExotikSquidline).
- T7Overcharged credited [XLabsProject](https://github.com/XLabsProject) for
  its project setup and [Approved](https://github.com/approved) for its asset
  limit extension.

## Third-party components

| Component | Use | License |
| --- | --- | --- |
| [MinHook](https://github.com/TsudaKageyu/minhook) | Git submodule in `deps/minhook`; links into the DLL | BSD 2-Clause (`deps/minhook/LICENSE.txt`) |
| [Premake](https://premake.github.io/) | `tools/premake5.exe`, build generation only | BSD 3-Clause |

## References

These projects informed the current implementation. None of their source code
is included in this repository.

- [BOIII](https://github.com/Ezz-lol/boiii-free) (GPLv3): cross-checking the
  `XAssetPool` descriptor layout, pool entry sizes, stock pool capacities, and
  candidate offsets.
- DutyCore: the technique of extending a live pool by linking a separately
  allocated block onto its free list, called from a Lua-loaded DLL. DutyCore
  publishes no license, so none of its code is used or redistributed.
- weapon_tech by Blakintosh (MIT): an independent description of the
  `XAssetPool` layout.
