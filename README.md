# T7Unchained

T7Unchained is an open-source Call of Duty: Black Ops III engine extension
project focused on expanding engine limits, extending asset pools, and
enabling greater flexibility for custom content and total conversions.

> **Status: experimental.** The current release extends the `attachment` and
> `attachmentunique` asset pools at runtime, from a DLL loaded by a mod's Lua
> UI code. In testing on the September 2026 Steam executable, the engine
> allocated entries beyond the stock capacity of both pools. Other pools,
> other executable builds, and the engine's global XAsset entry limit are not
> yet supported or verified. See [Known limitations](#known-limitations).

T7Unchained began as a fork of
[T7Overcharged](https://github.com/JariKCoding/T7Overcharged) and is licensed
under the GNU GPL v3. It keeps T7Overcharged's exported `init` entry point and
BO3 build detection, and drops its Lua, UI, console, network, Discord,
gameplay-patch and hot-reload features. See [CREDITS.md](CREDITS.md).

## Supported pools

| Pool | Stock capacity (September 2026 build) |
| --- | ---: |
| `attachment` | 128 |
| `attachmentunique` | 2148 (`0x864`) |

These are the capacities observed at runtime. The log records the actual
value on every start, and that logged value is authoritative.

Only these two pools are currently supported. Other pool names are logged and
ignored; their engine-side limits have not been investigated. The template's
`weapon=0` and `xmodel=0` entries are accepted as no-op requests; positive
capacities for them are unsupported and will be logged.

## Installation

T7Unchained must be installed in the `zone` folder of the mod that loads it.
The DLL derives every path from its own location; no BO3 active-mod
structure offsets are used, and there is no fallback to the working
directory.

```text
Black Ops III/
└── mods/
    └── <mod>/
        ├── logs/
        │   └── T7Unchained.log      (created by the DLL)
        └── zone/
            ├── T7Unchained.dll
            ├── T7Unchained.ini      (from T7Unchained.ini in this repository)
            └── offsets.txt          (from offsets.txt in this repository)
```

The mod's Lua UI code loads the DLL and calls its `init` export. See
[examples/lua/t7unchained_loader.lua](examples/lua/t7unchained_loader.lua).
If the DLL is not inside a `mods/<mod>/zone` folder, it records the error and
does nothing else; this means Steam Workshop install folders are not
currently supported. Placing the DLL in the folder does not, by itself, prove
that BO3 activated that mod.

### Upgrading from T7Overclocked

The DLL is now named `T7Unchained.dll`, so update the mod's Lua loader to the
new file name when you deploy it. For this release only, T7Unchained still
accepts the old configuration names:

- If `T7Unchained.ini` is absent, `T7Overclocked.ini` is read instead.
- If the `[T7Unchained]` section is absent, `[T7Overclocked]` is used instead.

Each legacy name in use logs a deprecation warning. Only one file and one
settings section are ever applied: when both names exist, the T7Unchained name
wins and the legacy one is ignored with a warning. The log file is now
`T7Unchained.log`.

## Configuration

```ini
[T7Unchained]
Enabled=true
LogLevel=info
DiagnosticOnly=false
MonitorIntervalMs=2000

[AssetPools]
attachment=512
attachmentunique=4096
```

| Key | Default | Meaning |
| --- | --- | --- |
| `Enabled` | `true` | `false` disables all pool changes. |
| `LogLevel` | `info` | `debug`, `info`, `warning` or `error`. |
| `DiagnosticOnly` | `false` | `true` validates and logs the pool table and target descriptors without changing memory or installing hooks. |
| `MonitorIntervalMs` | `2000` | How often a read-only monitor checks the extended pools; `0` disables it, otherwise at least `100`. |

Under `[AssetPools]`, a value of `0` leaves the pool unchanged and a positive
value is the requested capacity. A request at or below the runtime capacity
never shrinks a pool. Invalid values and unsupported pool names are logged.

### offsets.txt

`offsets.txt` maps names to relative virtual addresses (RVAs) for each
supported executable build, as `<name>.<build>=0x...`. The current method
needs only `DB_XAssetPool`. The shipped values were carried over from the
T7Overcharged build table, and only the `september_2026` value has been
exercised in game. On every start the log reports how many references in the
running game code point into the configured table; a near-zero count means
the address is wrong for that executable. `DB_AssetPoolInit` is needed only if
the DLL loads before the pools exist, which does not happen when it is loaded
from Lua.

## Logs

- `mods/<mod>/logs/T7Unchained.log`: the main log. It records the resolved
  paths, the configuration in use, each pool's original, requested and final
  capacity with the outcome, and, from the monitor, when the engine allocates
  entries beyond the original capacity.
- `%TEMP%\T7Unchained_boot_<pid>_<tick>.log`: an early startup trace, written
  before the main log exists. Check it when the main log is missing.

Neither log needs a BO3 console.

## How pools are extended

BO3 allocates pool entries by popping `XAssetPool::freeHead` and releases them
by pushing back onto it. Live assets are referenced by pointer, so the
existing storage is never moved or copied. For each request above the runtime
capacity, the DLL allocates a separate block of `requested - capacity`
entries, chains them, and links the chain onto the tail of the existing free
list with an atomic compare-exchange. It then raises `itemCount` to the
requested capacity. Stock entries are still handed out first; the new entries
are only used once the stock storage is exhausted, which is where the engine
would otherwise fail. This is the technique DutyCore uses from its Lua `init`,
with free-list validation and an atomic tail link added. No game code is
hooked or patched.

Before extending, the DLL checks that the descriptor's `itemSize` matches the
pool (`0x220` attachment, `0x840` attachmentunique), that it is not a
singleton, and that every free-list link stays inside the pool's storage.

## Known limitations

- **Experimental.** Allocation beyond stock capacity has been observed for
  `attachment` and `attachmentunique` on the September 2026 build. Whether
  every engine system then handles those assets correctly has not been
  established.
- **Non-contiguous entries.** Extension entries lie outside the pool's
  original storage. Engine code that walks that storage by index up to
  `itemCount`, or derives an index from an entry's address, would mishandle
  them. The log lists code that references the target descriptors directly.
- **Global XAsset entry limit.** Every loaded asset also needs a record in
  the engine's shared XAsset entry table. Large mods can exhaust it and fail
  with `Could not allocate asset - increase XASSET_ENTRY_POOL_SIZE`.
  T7Unchained does not change that limit, and raising per-type pools can let
  loading reach it.
- **Executable builds.** Build detection recognises the March 2023,
  February 2026 and September 2026 executables, but only September 2026 has
  been tested. The function code is encrypted on disk, so offsets cannot be
  verified statically.

## Building and contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for prerequisites, building, tests and
reporting results.

## License

T7Unchained is licensed under the GNU General Public License v3.0, inherited
from T7Overcharged. See [LICENSE](LICENSE). It comes with no warranty.
Third-party components keep their own licenses; see [CREDITS.md](CREDITS.md).
