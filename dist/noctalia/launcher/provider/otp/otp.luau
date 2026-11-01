--!nonstrict
-- Peeble launcher_plugin("/otp") port for Noctalia.
-- Lists `awbus list-otp`, ranks with noctalia.fuzzyScore, runs `awbus otp code <name>`.

local function iconFor(name: string): string
  local prefix = name:match("^([^/]+)")
  if prefix == nil or prefix == "" then
    return "key"
  end
  return string.lower(prefix)
end

local function rowsFor(query: string, names: { string }): { any }
  local text = noctalia.string.trim(query)
  local rows = {}
  for _, name in names do
    local score = 0
    if text ~= "" then
      local matched = noctalia.fuzzyScore(text, name)
      if matched == nil then
        continue
      end
      score = matched
    end
    table.insert(rows, {
      id = name,
      title = name,
      subtitle = "Generate OTP Code for " .. name,
      icon = iconFor(name),
      score = score,
    })
  end
  return rows
end

local function parseNames(stdout: string): { string }
  local names = {}
  for line in string.gmatch(stdout, "[^\r\n]+") do
    local name = noctalia.string.trim(line)
    if name ~= "" then
      table.insert(names, name)
    end
  end
  return names
end

function onQuery(query)
  if not noctalia.commandExists("awbus") then
    launcher.setResults(query, {
      {
        id = "",
        title = "awbus not found",
        subtitle = "Install awbus and ensure it is on PATH",
        glyph = "alert-triangle",
      },
    })
    return
  end

  noctalia.runAsync({ "awbus", "list-otp" }, function(result)
    if result.exitCode ~= 0 then
      launcher.setResults(query, {
        {
          id = "",
          title = "awbus list-otp failed",
          subtitle = noctalia.string.trim(result.stderr),
          glyph = "alert-triangle",
        },
      })
      return
    end
    launcher.setResults(query, rowsFor(query, parseNames(result.stdout)))
  end)
end

function onActivate(id)
  if id == nil or id == "" then
    return
  end
  -- Same as peeble: spawn("awbus otp code " + name). awbus copies the code.
  noctalia.runAsync({ "awbus", "otp", "code", id })
end
