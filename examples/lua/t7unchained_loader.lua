-- Minimal T7Unchained loader for a mod's LUI code.
--
-- Adapted from the T7Overcharged loader. T7Unchained has no Lua API: init()
-- only reads the configuration and extends the asset pools. Discord RPC, hot
-- reload and the external console from T7Overcharged are not available.
--
-- The DLL must sit in the mod's zone folder (mods/<mod>/zone/T7Unchained.dll).
-- It reads T7Unchained.ini and offsets.txt from that folder and writes
-- mods/<mod>/logs/T7Unchained.log. It refuses to run from any folder that is
-- not mods/<mod>/zone, so Workshop install folders are not supported.
--
-- Call once from the mod's UI startup, for example:
--   InitializeT7Unchained([[.\mods\my_mod\]])

EnableGlobals()

function InitializeT7Unchained(modFolder)
    if T7UnchainedLoaded then return true end

    local dllPath = modFolder .. [[zone\T7Unchained.dll]]
    local ok, failure = pcall(function()
        local init = require("package").loadlib(dllPath, "init")
        if not init then
            return "could not load " .. dllPath
        end
        init()
        return nil
    end)

    if not ok or failure then
        Engine.ComError(Enum.errorCode.ERROR_UI, "T7Unchained: " .. tostring(failure))
        return false
    end

    T7UnchainedLoaded = true
    return true
end
