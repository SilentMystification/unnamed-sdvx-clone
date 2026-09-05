function clamp(x, min, max) 
    if x < min then
        x = min
    end
    if x > max then
        x = max
    end

    return x
end

function smootherstep(edge0, edge1, x) 
    -- Scale, and clamp x to 0..1 range
    x = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    -- Evaluate polynomial
    return x * x * x * (x * (x * 6 - 15) + 10)
end
  
function to_range(val, start, stop)
    return start + (stop - start) * val
end

Animation = {
    start = 0,
    stop = 0,
    progress = 0,
    duration = 1,
    smoothStart = false
}

function Animation:new(o)
    o = o or {}
    setmetatable(o, self)
    self.__index = self
    return o
end

function Animation:restart(start, stop, duration)
    self.progress = 0
    self.start = start
    self.stop = stop
    self.duration = duration
end

function Animation:tick(deltaTime)
    self.progress = math.min(1, self.progress + deltaTime / self.duration)
    if self.progress == 1 then return self.stop end
    if self.smoothStart then
        return to_range(smootherstep(0, 1, self.progress), self.start, self.stop)
    else
        return to_range(smootherstep(-1, 1, self.progress) * 2 - 1, self.start, self.stop)
    end
end

local yScale = Animation:new()
local diagWidthMin = 600
local diagHeight = 400
local tabStroke = {start=0, stop=1}
local tabStrokeAnimation = {start=Animation:new(), stop=Animation:new()}
local settingsStrokeAnimation = {x=Animation:new(), y=Animation:new()}
local prevTab = -1
local prevSettingStroke = {x=0, y=0}
local settingStroke = {x=0, y=0}
local prevVis = false

-- Per-index (Select/Rename/In-Measure/In-Beat/Out-Measure/Out-Beat/Delete or
-- Add-drill row) highlight animation state for the Drills grid - keyed the
-- same way as tab.settings itself, i.e. by absolute position in the current
-- tab's flat settings array.
local drillHighlights = {}

local function getDrillHighlight(idx)
    local h = drillHighlights[idx]
    if not h then
        h = { anim = Animation:new(), wasCurrent = false }
        drillHighlights[idx] = h
    end
    return h
end

-- Eases the given index's highlight toward 1 while current, 0 otherwise.
-- Restarts from whatever value it's currently at on every current/not-current
-- transition (rather than a raw, continuously-nudged timer) so a fast Up/Down
-- can never leave it mid-decay and cause the next activation to jump most of
-- the way there in a single frame.
local function tickDrillHighlight(idx, isCurrent, dt, duration)
    duration = duration or 0.15
    local h = getDrillHighlight(idx)
    if isCurrent ~= h.wasCurrent then
        local current = h.anim:tick(0)
        h.anim:restart(current, isCurrent and 1 or 0, duration)
        h.wasCurrent = isCurrent
    end
    return h.anim:tick(dt)
end

-- Draws a horizontal underline instead of a filled highlight box, to match
-- the rest of the dialog's style - the generic list underlines the current
-- row's text (settingStroke/settingsStrokeAnimation below) rather than
-- filling a box behind it, so the grid's per-cell/per-row indicators do the
-- same: a 2px stroke line, width-animated the same way the boxes were.
-- gfx.StrokeColor requires an integer alpha - every call site here computes
-- it from a 0..1 animation value (e.g. 255 * rowHighlight), which is a float,
-- so this must round it. (A previous version of this used gfx.FillColor the
-- same way without rounding, which threw a Lua error - "number has no
-- integer representation" - on every single highlight draw call, every
-- frame; the engine caught and logged each one to disk, which was the actual
-- source of severe lag, and the throw aborted the rest of that frame's
-- drawing partway through, which is why rows below it looked like they
-- vanished. Not repeating that mistake here.)
local function drawUnderline(x, y, w, r, g, b, a)
    gfx.BeginPath()
    gfx.MoveTo(x, y)
    gfx.LineTo(x + w, y)
    gfx.StrokeWidth(2)
    gfx.StrokeColor(r, g, b, math.floor((a or 255) + 0.5))
    gfx.Stroke()
end

-- Draws one numeric cell (In-Measure/In-Beat/Out-Measure/Out-Beat) of a drill
-- row. A free function taking plain arguments - not a per-row table of
-- per-cell tables - so the grid's hot per-frame render path allocates
-- nothing per cell (creating ~5 short-lived tables per visible row, every
-- frame, was enough Lua GC churn on its own to show up as visible stutter).
local function drawDrillCell(settings, currentSetting, deltaTime, idx, cx, cellW, underlineY, textY)
    local cs = settings[idx]
    local isCurrent = idx == currentSetting
    local highlight = tickDrillHighlight(idx, isCurrent, deltaTime, 0.15)
    if highlight > 0.01 then
        drawUnderline(cx, underlineY, cellW * highlight, 255, 127, 0, 255 * highlight)
    end

    if cs.isEditing then
        -- Still red while editing an invalid value, so the "in >= out"
        -- warning doesn't disappear just because you started typing.
        if cs.invalid then
            gfx.FillColor(255, 60, 60)
        else
            gfx.FillColor(255, 200, 0)
        end
        gfx.Text(tostring(cs.value) .. "_", cx + cellW / 2, textY)
    elseif cs.invalid then
        -- In >= out (measure or beat, whichever pair is the culprit) - red
        -- until the drill's points are fixed.
        gfx.FillColor(255, 60, 60)
        gfx.Text(tostring(cs.value), cx + cellW / 2, textY)
    elseif isCurrent then
        gfx.FillColor(255, 200, 0)
        gfx.Text(tostring(cs.value), cx + cellW / 2, textY)
    else
        gfx.FillColor(255, 255, 255)
        gfx.Text(tostring(cs.value), cx + cellW / 2, textY)
    end
end

-- The Drills tab (a saved list of in/out practice loop points) lays its rows
-- out as a compact grid - one line per drill, header once at the top -
-- instead of the generic one-row-per-setting list below, for the same reason
-- as Nightfall's equivalent (see that skin's SettingsWindow.lua): drill rows
-- are dynamic (added/removed, values change live), and the C++ side
-- (PracticeModeSettingsDialog::m_CreateDrillsTab) always lays them out as a
-- fixed, deterministic pattern of 7 settings per drill (Select, Rename,
-- In-Measure, In-Beat, Out-Measure, Out-Beat, Delete) followed by exactly one
-- trailing "Add drill..." button - so drills can be grouped and rendered by
-- position instead of one row per setting.
---@param deltaTime number
---@param tab table
---@param diagWidth number
---@param availableHeight number vertical space left below the tab bar for this tab's content
local function drawDrillsGrid(deltaTime, tab, diagWidth, availableHeight)
    local settings = tab.settings
    local total = #settings
    local groupSize = 7
    local numDrills = math.floor((total - 1) / groupSize)
    local w = diagWidth - 10

    local nameX, nameW = 0, 130
    local inMX, inBX = 135, 205
    local outMX, outBX = 290, 360
    local cellW = 65
    local deleteX = w - 5
    local rowH = 32
    local headerH = 20

    gfx.TextAlign(gfx.TEXT_ALIGN_LEFT + gfx.TEXT_ALIGN_MIDDLE)
    gfx.FontSize(14)
    gfx.FillColor(180, 180, 180)
    gfx.Text("NAME", nameX, headerH / 2)
    gfx.TextAlign(gfx.TEXT_ALIGN_CENTER + gfx.TEXT_ALIGN_MIDDLE)
    gfx.Text("IN M", inMX + cellW / 2, headerH / 2)
    gfx.Text("IN B", inBX + cellW / 2, headerH / 2)
    gfx.Text("OUT M", outMX + cellW / 2, headerH / 2)
    gfx.Text("OUT B", outBX + cellW / 2, headerH / 2)
    gfx.FillColor(255, 255, 255)

    -- Header stays fixed; only the drill rows below it scroll, keeping the
    -- current selection (roughly centered) in view - same reasoning as the
    -- generic list's scrolling below, just counting whole drill rows instead
    -- of individual settings.
    local gridListHeight = availableHeight - headerH
    local maxVisibleRows = math.max(1, math.floor(gridListHeight / rowH))
    local totalVisualRows = numDrills + 1 -- + trailing Add-drill row
    local maxScrollRow = math.max(0, totalVisualRows - maxVisibleRows)

    local currentSetting = SettingsDiag.currentSetting
    local currentVisualRow
    if currentSetting > numDrills * groupSize then
        currentVisualRow = numDrills -- 0-indexed: lands on the Add-drill row
    else
        currentVisualRow = math.floor((currentSetting - 1) / groupSize)
    end

    local scrollRow = clamp(currentVisualRow - math.floor(maxVisibleRows / 2), 0, maxScrollRow)

    gfx.Scissor(0, headerH, diagWidth - 10, gridListHeight)
    gfx.Save()
    gfx.Translate(0, headerH)
    -- Set once for the whole scrolled body instead of per-row - a FontSize
    -- (or TextAlign) change is a real state change on the renderer, and
    -- switching it several times per row for ~8 visible rows adds up.
    gfx.FontSize(20)

    for row = 1, numDrills do
        local rowIndex0 = row - 1
        if rowIndex0 < scrollRow or rowIndex0 >= scrollRow + maxVisibleRows then
            goto continueDrillRow
        end

        local base = (row - 1) * groupSize
        local selectIdx = base + 1
        local renameIdx = base + 2
        local inMIdx = base + 3
        local inBIdx = base + 4
        local outMIdx = base + 5
        local outBIdx = base + 6
        local deleteIdx = base + 7

        local selectSetting = settings[selectIdx]
        local renameSetting = settings[renameIdx]

        local rowY = (rowIndex0 - scrollRow) * rowH
        local rowMidY = rowY + rowH / 2

        local isRenameCurrent = renameIdx == currentSetting
        local isDeleteCurrent = deleteIdx == currentSetting

        -- All underlines for this row sit at the same y, near its bottom
        -- edge - matching the generic list, which underlines just below the
        -- current row's text rather than filling a box behind it.
        local underlineY = rowY + rowH - 3

        local rowHighlight = tickDrillHighlight(selectIdx, selectIdx == currentSetting, deltaTime, 0.2)
        if rowHighlight > 0.01 then
            drawUnderline(-5, underlineY, (w + 5) * rowHighlight, 255, 127, 0, 255 * rowHighlight)
        end

        local renameHighlight = tickDrillHighlight(renameIdx, isRenameCurrent, deltaTime, 0.15)
        if renameHighlight > 0.01 then
            drawUnderline(-2, underlineY, nameW * renameHighlight, 255, 127, 0, 255 * renameHighlight)
        end

        gfx.TextAlign(gfx.TEXT_ALIGN_LEFT + gfx.TEXT_ALIGN_MIDDLE)
        if renameSetting.isEditing then
            gfx.FillColor(255, 200, 0)
            gfx.Text(renameSetting.value .. "_", nameX, rowMidY)
        elseif isRenameCurrent then
            gfx.FillColor(255, 200, 0)
            gfx.Text(renameSetting.value, nameX, rowMidY)
        else
            gfx.FillColor(255, 255, 255)
            gfx.Text(renameSetting.value, nameX, rowMidY)
        end

        gfx.TextAlign(gfx.TEXT_ALIGN_CENTER + gfx.TEXT_ALIGN_MIDDLE)
        drawDrillCell(settings, currentSetting, deltaTime, inMIdx, inMX, cellW, underlineY, rowMidY)
        drawDrillCell(settings, currentSetting, deltaTime, inBIdx, inBX, cellW, underlineY, rowMidY)
        drawDrillCell(settings, currentSetting, deltaTime, outMIdx, outMX, cellW, underlineY, rowMidY)
        drawDrillCell(settings, currentSetting, deltaTime, outBIdx, outBX, cellW, underlineY, rowMidY)

        -- Row invalid (in >= out) - swap DELETE for an INVALID indicator,
        -- except while hovering the delete option itself, so it's still
        -- possible to delete an invalid drill rather than getting stuck with it.
        local deleteLabel = "DELETE"
        local deleteR, deleteG, deleteB = 200, 200, 200
        if isDeleteCurrent then
            deleteR, deleteG, deleteB = 255, 60, 60
        elseif selectSetting.invalid then
            deleteLabel = "INVALID"
            deleteR, deleteG, deleteB = 255, 60, 60
        end

        gfx.TextAlign(gfx.TEXT_ALIGN_RIGHT + gfx.TEXT_ALIGN_MIDDLE)
        gfx.FillColor(deleteR, deleteG, deleteB)
        gfx.Text(deleteLabel, deleteX, rowMidY)

        ::continueDrillRow::
    end

    -- Trailing "Add drill..." row. Its label toggles between an enabled and a
    -- disabled/explanatory string depending on whether an in/out range is set
    -- (see PracticeModeSettingsDialog::m_CreateDrillsTab).
    local addIdx = total
    local addSetting = settings[addIdx]
    local addRowIndex0 = numDrills
    if addRowIndex0 >= scrollRow and addRowIndex0 < scrollRow + maxVisibleRows then
        local addBlockY = (addRowIndex0 - scrollRow) * rowH
        local addMidY = addBlockY + rowH / 2
        local isAddEnabled = addSetting.name ~= "Set an In and Out position to save as a drill"

        local addHighlight = tickDrillHighlight(addIdx, addIdx == currentSetting, deltaTime, 0.2)
        if isAddEnabled and addHighlight > 0.01 then
            drawUnderline(-5, addBlockY + rowH - 3, (w + 5) * addHighlight, 255, 127, 0, 255 * addHighlight)
        end

        gfx.TextAlign(gfx.TEXT_ALIGN_LEFT + gfx.TEXT_ALIGN_MIDDLE)
        gfx.FillColor(isAddEnabled and 255 or 100, isAddEnabled and 255 or 100, isAddEnabled and 255 or 100)
        gfx.Text(addSetting.name, 0, addMidY)
    end

    gfx.Restore()
    gfx.ResetScissor()
    gfx.FillColor(255, 255, 255)
end

function render(deltaTime, visible)
    if visible and not prevVis then
        yScale:restart(0, 1, 0.25)
    elseif not visible and prevVis then
        yScale:restart(1, 0, 0.25)
    end

    if not visible and yScale:tick(0) < 0.05 then return end

    local posX = SettingsDiag.posX or 0.5
    local posY = SettingsDiag.posY or 0.5
    local message_1 = "Press both FXs to open/close. Use the Start button to press buttons."
    local message_2 = "Use FX keys to navigate tabs. Use arrow keys to navigate and modify settings."

    -- Measure how wide the tab bar actually needs to be (more tabs, or longer
    -- names, than the dialog was originally sized for would otherwise spill
    -- text past the background rect's right edge - e.g. a trailing tab like
    -- "Settings" hanging off the side). TextBounds measures in local/logical
    -- units independent of the current transform, so this is safe to do
    -- before any Translate/Scale is set up below - but TextAlign is
    -- persistent GL state left over from whatever rendered last, so it must
    -- be set explicitly here to match the real tab-bar draw loop below
    -- (TOP+LEFT), or TextBounds returns bounds for the wrong alignment mode
    -- and this measurement silently disagrees with the actual layout.
    gfx.TextAlign(gfx.TEXT_ALIGN_TOP + gfx.TEXT_ALIGN_LEFT)
    gfx.FontSize(35)
    local measuredTabWidth = 5
    for ti, tab in ipairs(SettingsDiag.tabs) do
        local xmin, ymin, xmax, ymax = gfx.TextBounds(measuredTabWidth, 5, tab.name)
        measuredTabWidth = xmax + 10
    end
    local diagWidth = math.max(diagWidthMin, measuredTabWidth + 5)

    resX, resY = game.GetResolution()
    local scale = resY / 1080
    gfx.ResetTransform()
    gfx.Translate(math.floor(diagWidth/2 + posX*(resX-diagWidth)), math.floor(diagHeight/2 + posY*(resY-diagHeight)))
    gfx.Scale(scale, scale)
    gfx.Scale(1.0, smootherstep(0, 1, yScale:tick(deltaTime)))
    gfx.BeginPath()
    gfx.Rect(-diagWidth/2, -diagHeight/2, diagWidth, diagHeight)
    gfx.FillColor(50,50,50)
    gfx.Fill()
    gfx.FillColor(255,255,255)
    
    gfx.FontSize(20)
    
    local m_xmin, m_ymin, m_xmax, m_ymax = gfx.TextBounds(0, 0, message_1)
    gfx.Text(message_1, diagWidth/2 - m_xmax, diagHeight/2 - m_ymax - 20)
    
    m_xmin, m_ymin, m_xmax, m_ymax = gfx.TextBounds(0, 0, message_2)
    gfx.Text(message_2, diagWidth/2 - m_xmax, diagHeight/2 - m_ymax)

    tabStroke.start = tabStrokeAnimation.start:tick(deltaTime)
    tabStroke.stop = tabStrokeAnimation.stop:tick(deltaTime)

    settingStroke.x = settingsStrokeAnimation.x:tick(deltaTime)
    settingStroke.y = settingsStrokeAnimation.y:tick(deltaTime)

    local tabBarHeight = 0
    local nextTabX = 5

    gfx.TextAlign(gfx.TEXT_ALIGN_TOP + gfx.TEXT_ALIGN_LEFT)
    gfx.FontSize(35)
    gfx.Save() --draw tab bar
    gfx.Translate(-diagWidth / 2, -diagHeight / 2)
    for ti, tab in ipairs(SettingsDiag.tabs) do
        local xmin,ymin, xmax,ymax = gfx.TextBounds(nextTabX, 5, tab.name)

        if ti == SettingsDiag.currentTab and SettingsDiag.currentTab ~= prevTab then 
            tabStrokeAnimation.start:restart(tabStroke.start, nextTabX, 0.1)
            tabStrokeAnimation.stop:restart(tabStroke.stop, xmax, 0.1)
        end
        tabBarHeight = math.max(tabBarHeight, ymax + 5)
        gfx.Text(tab.name, nextTabX, 5)
        nextTabX = xmax + 10
    end
    gfx.BeginPath()
    gfx.MoveTo(0, tabBarHeight)
    gfx.LineTo(diagWidth, tabBarHeight)
    gfx.StrokeWidth(2)
    gfx.StrokeColor(0,127,255)
    gfx.Stroke()
    gfx.BeginPath()
    gfx.MoveTo(tabStroke.start, tabBarHeight)
    gfx.LineTo(tabStroke.stop, tabBarHeight)
    gfx.StrokeColor(255, 127, 0)
    gfx.Stroke()
    gfx.Restore() --draw tab bar end

    gfx.FontSize(30)
    gfx.Save() --draw current tab
    gfx.Translate(-diagWidth / 2, -diagHeight / 2)
    gfx.Translate(5, tabBarHeight + 5)

    local settingHeight = 30
    local tab = SettingsDiag.tabs[SettingsDiag.currentTab]
    local totalRows = #tab.settings

    -- Space actually left in the dialog below the tab bar - a tab that can
    -- have arbitrarily many rows (e.g. Drills) can't run off the bottom, so
    -- both the grid and the generic list scroll to keep the current
    -- selection (roughly centered) in view instead of just drawing every row
    -- unconditionally downward.
    local listAreaHeight = diagHeight - (tabBarHeight + 5) - 10

    if tab.name == "Drills" then
        drawDrillsGrid(deltaTime, tab, diagWidth, listAreaHeight)
        goto afterTabContent
    end

    do
    local maxVisibleRows = math.max(1, math.floor(listAreaHeight / settingHeight))
    local maxScrollRow = math.max(0, totalRows - maxVisibleRows)
    local scrollRow = clamp(SettingsDiag.currentSetting - 1 - math.floor(maxVisibleRows / 2), 0, maxScrollRow)

    gfx.Scissor(0, 0, diagWidth - 10, listAreaHeight)

    gfx.BeginPath()
    gfx.MoveTo(0, settingStroke.y)
    gfx.LineTo(settingStroke.x, settingStroke.y)
    gfx.StrokeWidth(2)
    gfx.StrokeColor(255, 127, 0)
    gfx.Stroke()

    for si, setting in ipairs(tab.settings) do
        local rowIndex0 = si - 1
        if rowIndex0 < scrollRow or rowIndex0 >= scrollRow + maxVisibleRows then
            goto continueRow
        end
        gfx.Save()
        gfx.Translate(0, (rowIndex0 - scrollRow) * settingHeight)

        local disp = ""
        if setting.type == "enum" then
            disp = string.format("%s: %s", setting.name, setting.options[setting.value])
        elseif setting.type == "int" then
            disp = string.format("%s: %d", setting.name, setting.value)
        elseif setting.type == "string" then
            disp = string.format("%s: %s", setting.name, setting.value)
        elseif setting.type == "float" then
            disp = string.format("%s: %.2f", setting.name, setting.value)
            if setting.max == 1 and setting.min == 0 then --draw slider
                disp = setting.name .. ": "
                local xmin,ymin, xmax,ymax = gfx.TextBounds(0, 0, disp)
                local width = diagWidth - 20 - xmax
                gfx.BeginPath()
                gfx.MoveTo(xmax + 5, 20)
                gfx.LineTo(xmax + 5 + width, 20)
                gfx.StrokeColor(0,127,255)
                gfx.StrokeWidth(2)
                gfx.Stroke()
                gfx.BeginPath()
                gfx.MoveTo(xmax + 5, 20)
                gfx.LineTo(xmax + 5 + width * setting.value, 20)
                gfx.StrokeColor(255,127,0)
                gfx.StrokeWidth(2)
                gfx.Stroke() 
            end
        elseif setting.type == "button" then
            disp = string.format("%s", setting.name)
            local xmin, ymin, xmax,ymax = gfx.TextBounds(0, 0, disp)
            gfx.BeginPath()
            gfx.Rect(-2, 3, 4+xmax-xmin, 28)
            gfx.FillColor(0, 64, 128)
            if si == SettingsDiag.currentSetting then
                gfx.StrokeColor(255, 127, 0)
            else
                gfx.StrokeColor(0,127,255)
            end
            gfx.StrokeWidth(2)
            gfx.Fill()
            gfx.Stroke()
            gfx.FillColor(255,255,255)
        else
            disp = string.format("%s:", setting.name)
            local xmin,ymin, xmax,ymax = gfx.TextBounds(0, 0, disp)
            gfx.BeginPath()
            gfx.Rect(xmax + 5, 5, 20,20)
            gfx.FillColor(255, 127, 0, setting.value and 255 or 0)
            gfx.StrokeColor(0,127,255)
            gfx.StrokeWidth(2)
            gfx.Fill()
            gfx.Stroke()
            gfx.FillColor(255,255,255)
        end
        gfx.Text(disp, 0 ,0)
        if si == SettingsDiag.currentSetting then
            local setting_name = setting.name .. ":"
            if setting.type == "button" then
                setting_name = setting.name
            end
            local xmin,ymin, xmax,ymax = gfx.TextBounds(0, 0, setting_name)
            ymax = ymax + settingHeight * (rowIndex0 - scrollRow)
            if xmax ~= prevSettingStroke.x or ymax ~= prevSettingStroke.y then
                settingsStrokeAnimation.x:restart(settingStroke.x, xmax, 0.1)
                settingsStrokeAnimation.y:restart(settingStroke.y, ymax, 0.1)
            end

            prevSettingStroke.x = xmax
            prevSettingStroke.y = ymax
        end
        gfx.Restore()
        ::continueRow::
    end

    gfx.ResetScissor()
    end

    ::afterTabContent::
    gfx.Restore() --draw current tab end
    prevTab = SettingsDiag.currentTab
    prevVis = visible

end