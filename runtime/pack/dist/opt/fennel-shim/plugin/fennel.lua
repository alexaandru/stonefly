-- Tiny Lua bootstrap only (fennel-nvim/plugin/fennel.lua shape).
-- Does NOT require("fennel-shim") — never ship lua/fennel-shim alongside
-- fnl/fennel-shim: Lua package.path wins first. Load by path + fennel.dofile.
if rawget(_G, "_sf_fennel_shim_loaded") then
  return
end

local shim = sf.runtime_file("fnl/fennel-shim/init.fnl")
if not shim then
  error("fennel-shim: could not find fnl/fennel-shim/init.fnl on rtp")
end

local ok, fennel = pcall(require, "fennel")
if not ok then
  error("fennel-shim: require('fennel') failed: " .. tostring(fennel))
end

_G._sf_fennel_shim_loaded = true
fennel.dofile(shim)
