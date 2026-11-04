-- This file is part of the OSCQuery Automation Plugin —
-- https://github.com/pob31/OSCQuery-Automation-plugin
--
-- Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle
--
-- The OSCQuery Automation Plugin is free software: you can redistribute it
-- and/or modify it under the terms of the GNU General Public License as
-- published by the Free Software Foundation, either version 3 of the License,
-- or (at your option) any later version. It is distributed in the hope that it
-- will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
-- of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
-- Public License (LICENSE, at the repository root) for more details.
--
-- SPDX-License-Identifier: GPL-3.0-or-later

--[[
  SPIKE, PHASE 1: drives "OQA Spike 8" in REAPER and writes down what REAPER
  shows. Throwaway, with the spike.

    reaper -new -nosplash -cfgfile <copy of reaper.ini> phase1-bench.lua

  Reads the plugin format to load ("VST3" or "AU") from
  ~/.oqa-spike-phase1/format.txt. Talks to the plugin through
  ~/.oqa-spike-phase1/command.txt and ack.txt (SpikeProcessor.h). Writes
  results-<os>-<format>.txt there, one "key<TAB>value" per line, and
  phase.txt before each pause so screenshots can be taken from outside.
  Saves the project, reopens it, and quits REAPER.
]]

local sep = package.config:sub(1, 1)
local home = os.getenv("HOME") or os.getenv("USERPROFILE")
local dir = home .. sep .. ".oqa-spike-phase1" .. sep

local function readFile(path)
  local f = io.open(path, "r")
  if not f then return nil end
  local s = f:read("*a")
  f:close()
  return s
end

local function writeFile(path, text)
  local f = assert(io.open(path, "w"))
  f:write(text)
  f:close()
end

local function replaceFile(path, text)
  writeFile(path .. ".tmp", text)
  os.remove(path)
  os.rename(path .. ".tmp", path)
end

local format = ((readFile(dir .. "format.txt") or "VST3"):gsub("%s+", ""))
local osName = reaper.GetOS()
local results = {}

local function log(key, value)
  results[#results + 1] = key .. "\t" .. tostring(value)
end

log("reaper", reaper.GetAppVersion())
log("os", osName)
log("format", format)

-- The command channel ---------------------------------------------------------------
local seq = math.floor(reaper.time_precise() * 1000) % 1000000

local function command(lines)
  seq = seq + 1
  replaceFile(dir .. "command.txt", "seq " .. seq .. "\n" .. table.concat(lines, "\n") .. "\n")
end

local function acked()
  local ack = readFile(dir .. "ack.txt")
  return ack ~= nil and ack:match("^seq (%d+)") == tostring(seq)
end

-- What REAPER shows ----------------------------------------------------------------
local track, fx
local envelopes = {}   -- slot number -> envelope, created while unbound

local function snapshot(label)
  local n = reaper.TrackFX_GetNumParams(track, fx)
  log(label .. ".numParams", n)

  for p = 0, n - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, fx, p, "")
    log(label .. ".param" .. p, name)
  end

  for slot, env in pairs(envelopes) do
    if reaper.ValidatePtr(env, "TrackEnvelope*") then
      local _, name = reaper.GetEnvelopeName(env, "")
      log(label .. ".envelope.slot" .. slot, name .. " | points=" .. reaper.CountEnvelopePoints(env))
    else
      log(label .. ".envelope.slot" .. slot, "(gone)")
    end
  end

  local lanes = reaper.CountTrackEnvelopes(track)
  log(label .. ".lanes", lanes)
  for e = 0, lanes - 1 do
    local env = reaper.GetTrackEnvelope(track, e)
    local _, name = reaper.GetEnvelopeName(env, "")
    log(label .. ".lane" .. e, name .. " | points=" .. reaper.CountEnvelopePoints(env))
  end

  local ack = readFile(dir .. "ack.txt") or ""
  log(label .. ".pluginSays", (ack:gsub("\n", " ; ")))
end

local function redraw()
  reaper.TrackList_AdjustWindows(false)
  reaper.UpdateArrange()
  reaper.UpdateTimeline()
end

local function phase(name)
  redraw()
  replaceFile(dir .. "phase.txt", name .. "\n")
end

-- The sequence ---------------------------------------------------------------------
local steps = {}

-- `action` runs once; the step ends when `seconds` have passed AND `untilFn` holds
-- (or after 15 s, logged as a timeout).
local function step(action, seconds, untilFn)
  steps[#steps + 1] = { action = action, seconds = seconds or 0, untilFn = untilFn }
end

local function bindStep(label, lines, extra)
  step(function() command(lines) end, 1.5, acked)
  step(function()
    snapshot(label)
    phase(label)
    if extra then extra() end
  end, 2.5)
end

step(function()
  reaper.Main_OnCommand(40859, 0)            -- New project tab
  reaper.InsertTrackAtIndex(0, true)
  track = reaper.GetTrack(0, 0)
  fx = reaper.TrackFX_AddByName(track, format .. ":OQA Spike 8", false, -1)
  log("fxIndex", fx)
end, 2.0)

local abort = false

step(function()
  if fx < 0 then
    -- Nothing else can run; skip to finish() so REAPER still quits.
    log("error", "plugin not found as " .. format .. ":OQA Spike 8")
    abort = true
    return
  end
  for _, slot in ipairs({ 1, 2, 5 }) do
    local env = reaper.GetFXEnvelope(track, fx, slot - 1, true)
    envelopes[slot] = env
    reaper.InsertEnvelopePoint(env, 0.0, 0.2, 0, 0, false, true)
    reaper.InsertEnvelopePoint(env, 2.0, 0.8, 0, 0, false, true)
    reaper.InsertEnvelopePoint(env, 4.0, 0.4, 0, 0, false, true)
    reaper.Envelope_SortPoints(env)
  end
  snapshot("1-unbound")
  phase("1-unbound")
end, 2.5)

bindStep("2-bound", { "bind 1 Device A /mixer/channel/1/gain" })

step(function() reaper.OnPlayButton() end, 1.0)
bindStep("3-renamed-while-playing", { "bind 1 Device A /mixer/channel/1/gain (renamed)" },
         function() reaper.OnStopButton() end)

bindStep("4-all-bound", { "bindall Device A /bank" })
bindStep("5-slot1-unbound", { "unbind 1" })
bindStep("6-slot1-rebound-elsewhere", { "bind 1 Device B /source/1/position x" })

-- Is the lane title read when the lane is made? A lane created now, after the bind.
step(function()
  envelopes[3] = reaper.GetFXEnvelope(track, fx, 2, true)
  reaper.InsertEnvelopePoint(envelopes[3], 1.0, 0.5, 0, 0, false, false)
  snapshot("6b-new-lane-after-bind")
  phase("6b-new-lane-after-bind")
end, 2.5)

-- Does re-applying a lane's own state refresh its title, short of a reload?
step(function()
  local env = envelopes[1]
  local ok, chunk = reaper.GetEnvelopeStateChunk(env, "", false)
  log("6c.chunkRead", ok)
  log("6c.chunkSet", reaper.SetEnvelopeStateChunk(env, chunk, false))
  snapshot("6c-chunk-reapplied")
  phase("6c-chunk-reapplied")
end, 2.5)

-- And setting the FX offline and online again.
step(function()
  reaper.TrackFX_SetOffline(track, fx, true)
end, 1.0)
step(function()
  reaper.TrackFX_SetOffline(track, fx, false)
end, 2.0)
step(function()
  snapshot("6d-fx-offline-online")
  phase("6d-fx-offline-online")
end, 2.5)

-- Session reload: save, reopen, look again.
local projectPath = dir .. "phase1-" .. format .. ".rpp"
step(function()
  reaper.Main_SaveProjectEx(0, projectPath, 0)
  reaper.Main_openProject("noprompt:" .. projectPath)
end, 3.0)

step(function()
  track = reaper.GetTrack(0, 0)
  fx = 0
  envelopes = {}
  snapshot("7-reloaded")
  phase("7-reloaded")
end, 2.5)

-- Runner ---------------------------------------------------------------------------
local index, started = 1, nil

local function finish()
  local name = "results-" .. osName .. "-" .. format .. ".txt"
  writeFile(dir .. name, table.concat(results, "\n") .. "\n")
  phase("done")
  reaper.Main_SaveProject(0, false)
  reaper.Main_OnCommand(40004, 0)            -- File: Quit REAPER
end

local function run()
  local s = steps[index]
  if s == nil or abort then
    finish()
    return
  end

  local now = reaper.time_precise()
  if started == nil then
    started = now
    local ok, err = pcall(s.action)
    if not ok then
      log("error.step" .. index, err)
      abort = true      -- finish() at the next tick, so REAPER quits whatever went wrong
    end
  end

  local elapsed = now - started
  local ready = elapsed >= s.seconds and (s.untilFn == nil or s.untilFn())
  if ready or elapsed > 15 then
    if not ready then log("timeout.step" .. index, elapsed) end
    index, started = index + 1, nil
  end

  reaper.defer(run)
end

os.remove(dir .. "phase.txt")
reaper.defer(run)
