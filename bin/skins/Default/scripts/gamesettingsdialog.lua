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

-- Drills-grid equivalent of settingStroke above: one sliding underline instead
-- of Nightfall's per-cell wipe style.
local drillStroke = {x1=0, x2=0, y=0}
local drillStrokeAnimation = {x1=Animation:new(), x2=Animation:new(), y=Animation:new()}
local prevDrillStroke = {x1=0, x2=0, y=0}

-- gfx.StrokeColor needs an integer alpha - a float here previously threw a Lua
-- error every frame ("number has no integer representation"), which was the
-- real source of a severe lag bug.
local function drawUnderline(x, y, w, r, g, b, a)
    gfx.BeginPath()
    gfx.MoveTo(x, y)
    gfx.LineTo(x + w, y)
    gfx.StrokeWidth(2)
    gfx.StrokeColor(r, g, b, math.floor((a or 255) + 0.5))
    gfx.Stroke()
end

-- Free-running clock for the invalid-cell flash below (ticked in render()), so
-- every invalid cell flashes in lockstep instead of drifting.
local invalidFlashTime = 0

-- A clamped-at-commit value can look unremarkable despite being invalid, so
-- flash: value flashes red 4x/sec, then swaps to "Invalid", alternating 1x/sec.
local function drawInvalidCellText(value, cx, cellW, textY)
    local cyclePhase = invalidFlashTime % 1
    if cyclePhase >= 0.5 then
        gfx.FillColor(255, 60, 60)
        gfx.Text("Invalid", cx + cellW / 2, textY)
        return
    end

    local flashPhase = invalidFlashTime % 0.25
    if flashPhase < 0.125 then
        gfx.FillColor(255, 60, 60)
    else
        gfx.FillColor(255, 255, 255)
    end
    gfx.Text(tostring(value), cx + cellW / 2, textY)
end

-- Green-to-white fade for "this just changed to something good" (a row moved,
-- or a cell's value changed while staying valid). Created at rest on first
-- use; caller :restart(1, 0, duration)s it once the change is detected.
local function getFlashAnim(store, key)
    local a = store[key]
    if not a then
        a = Animation:new()
        store[key] = a
    end
    return a
end

local function flashFadeColor(anim, deltaTime)
    local t = anim:tick(deltaTime)
    local c = 255 - math.floor(t * 255 + 0.5)
    return c, 255, c
end

-- Keyed by trackingId (a drill's stable identity), not array position, since
-- drills resort by start point.
local rowMoveAnimations = {} -- trackingId -> Animation, for the Name column
local cellValidAnimations = {} -- trackingId.."_"..field -> Animation
local lastRowPosition = {} -- trackingId -> row, to detect a move
local lastCellValue = {} -- trackingId.."_"..field -> value, to detect a change

-- Draws one numeric cell of a drill row. Plain arguments, not a per-row table
-- of per-cell tables - avoids per-frame allocation that showed up as stutter.
local function drawDrillCell(settings, currentSetting, idx, cx, cellW, textY, fieldKey, deltaTime, isCurrentRow, isRowArmed)
    local cs = settings[idx]
    local isCurrent = idx == currentSetting

    if isRowArmed then
        gfx.FillColor(255, 60, 60)
        gfx.Text(cs.value == 0 and "" or tostring(cs.value), cx + cellW / 2, textY)
    elseif cs.isEditing then
        -- No flash while typing - hold still so the value being typed stays readable.
        if cs.invalid then
            gfx.FillColor(255, 60, 60)
        else
            gfx.FillColor(255, 200, 0)
        end
        gfx.Text(tostring(cs.value) .. "_", cx + cellW / 2, textY)
    elseif cs.invalid then
        drawInvalidCellText(cs.value, cx, cellW, textY)
    else
        -- Flash green-to-white when this cell's value changes while staying valid.
        -- Only checked for the current row, so only the row being edited can ever flash.
        local key = cs.trackingId .. fieldKey
        local lastVal = lastCellValue[key]
        if isCurrentRow and lastVal ~= nil and lastVal ~= cs.value then
            getFlashAnim(cellValidAnimations, key):restart(1, 0, 0.6)
        end
        lastCellValue[key] = cs.value

        local anim = cellValidAnimations[key]
        if anim and anim.progress < 1 then
            gfx.FillColor(flashFadeColor(anim, deltaTime))
        elseif isCurrent then
            gfx.FillColor(255, 200, 0)
        else
            gfx.FillColor(255, 255, 255)
        end
        -- 0 = blank start/end measure on a new drill (IsDrillIncomplete), not a real value.
        gfx.Text(cs.value == 0 and "" or tostring(cs.value), cx + cellW / 2, textY)
    end
end

-- Compact grid (one line per drill, header at top) instead of the generic
-- one-row-per-setting list, since C++ (m_CreateDrillsTab) emits a fixed 7
-- settings per drill then 2 trailing buttons, letting rows group by position.
-- Drills stay sorted by start point ascending - editing In-Measure/In-Beat can
-- move a row elsewhere in the list.
---@param deltaTime number
---@param tab table
---@param diagWidth number
---@param availableHeight number vertical space left below the tab bar for this tab's content
local function drawDrillsGrid(tab, diagWidth, availableHeight, deltaTime)
    local settings = tab.settings
    local total = #settings
    local groupSize = 7
    local numDrills = math.floor((total - 2) / groupSize) -- - 2 trailing buttons
    local w = diagWidth - 10

    local nameX, nameW = 0, 130
    local inMX, inBX = 135, 205
    local outMX, outBX = 290, 360
    local cellW = 65
    local deleteW = 80
    local deleteX = w - deleteW -- left edge of the Delete/Invalid cell
    local rowH = 32
    local headerH = 20

    gfx.TextAlign(gfx.TEXT_ALIGN_LEFT + gfx.TEXT_ALIGN_MIDDLE)
    gfx.FontSize(14)
    gfx.FillColor(180, 180, 180)
    gfx.Text("NAME", nameX, headerH / 2)
    gfx.TextAlign(gfx.TEXT_ALIGN_CENTER + gfx.TEXT_ALIGN_MIDDLE)
    gfx.FontSize(9) -- "START MEASURE"/"END MEASURE" are longer than the old "IN/OUT MEASURE"
    gfx.Text("START MEASURE", inMX + cellW / 2, headerH / 2)
    gfx.Text("START BEAT", inBX + cellW / 2, headerH / 2)
    gfx.Text("END MEASURE", outMX + cellW / 2, headerH / 2)
    gfx.Text("END BEAT", outBX + cellW / 2, headerH / 2)
    gfx.FillColor(255, 255, 255)

    -- Header stays fixed; drill rows below it scroll, keeping the current
    -- selection roughly centered.
    local gridListHeight = availableHeight - headerH
    local maxVisibleRows = math.max(1, math.floor(gridListHeight / rowH))
    local totalVisualRows = numDrills + 2 -- + Create New Drill + Set current in/out rows
    local maxScrollRow = math.max(0, totalVisualRows - maxVisibleRows)

    local createRowIdx = numDrills * groupSize + 1
    local addRowIdx = createRowIdx + 1

    local currentSetting = SettingsDiag.currentSetting
    local currentVisualRow
    if currentSetting == createRowIdx then
        currentVisualRow = numDrills -- 0-indexed: lands on the Create New Drill row
    elseif currentSetting == addRowIdx then
        currentVisualRow = numDrills + 1 -- 0-indexed: lands on the Set current in/out row
    else
        currentVisualRow = math.floor((currentSetting - 1) / groupSize)
    end

    local scrollRow = clamp(currentVisualRow - math.floor(maxVisibleRows / 2), 0, maxScrollRow)

    -- Reuse dead space for scroll hints instead of dedicated rows: the Delete
    -- column has no header label, and there's a small margin below the body.
    if scrollRow > 0 then
        gfx.TextAlign(gfx.TEXT_ALIGN_CENTER + gfx.TEXT_ALIGN_MIDDLE)
        gfx.FontSize(9)
        gfx.FillColor(255, 255, 255, 180)
        gfx.Text("^ MORE ^", deleteX + deleteW / 2, headerH / 2)
        gfx.FillColor(255, 255, 255)
    end
    if scrollRow + maxVisibleRows < totalVisualRows then
        gfx.TextAlign(gfx.TEXT_ALIGN_CENTER + gfx.TEXT_ALIGN_MIDDLE)
        gfx.FontSize(9)
        gfx.FillColor(255, 255, 255, 180)
        gfx.Text("v MORE BELOW v", w / 2, headerH + gridListHeight + 5)
        gfx.FillColor(255, 255, 255)
    end

    gfx.Scissor(0, headerH, diagWidth - 10, gridListHeight)
    gfx.Save()
    gfx.Translate(0, headerH)
    gfx.FontSize(20) -- set once for the whole scrolled body, not per-row

    drawUnderline(drillStroke.x1, drillStroke.y, drillStroke.x2 - drillStroke.x1, 255, 127, 0, 255)

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
        local deleteSetting = settings[deleteIdx]

        local rowY = (rowIndex0 - scrollRow) * rowH
        local rowMidY = rowY + rowH / 2

        local isRenameCurrent = renameIdx == currentSetting
        local isDeleteCurrent = deleteIdx == currentSetting

        local isCurrentRow = rowIndex0 == currentVisualRow

        -- Did THIS drill (the one under the cursor) move to a different row (a
        -- resort)? Only checked for the current row, not every row - a resort
        -- shifts every row between the old and new position by one, and they'd
        -- all look "moved" too if compared the same way.
        local trackingId = selectSetting.trackingId
        local prevRow = lastRowPosition[trackingId]
        if isCurrentRow and prevRow ~= nil and prevRow ~= row then
            getFlashAnim(rowMoveAnimations, trackingId):restart(1, 0, 0.6)
        end
        lastRowPosition[trackingId] = row

        local rowArmed = deleteSetting.armed

        gfx.TextAlign(gfx.TEXT_ALIGN_LEFT + gfx.TEXT_ALIGN_MIDDLE)
        if rowArmed then
            gfx.FillColor(255, 60, 60)
            gfx.Text(renameSetting.value, nameX, rowMidY)
        elseif renameSetting.isEditing then
            gfx.FillColor(255, 200, 0)
            gfx.Text(renameSetting.value .. "_", nameX, rowMidY)
        elseif isRenameCurrent then
            gfx.FillColor(255, 200, 0)
            gfx.Text(renameSetting.value, nameX, rowMidY)
        else
            local rowAnim = rowMoveAnimations[trackingId]
            if rowAnim and rowAnim.progress < 1 then
                gfx.FillColor(flashFadeColor(rowAnim, deltaTime))
            else
                gfx.FillColor(255, 255, 255)
            end
            gfx.Text(renameSetting.value, nameX, rowMidY)
        end

        gfx.TextAlign(gfx.TEXT_ALIGN_CENTER + gfx.TEXT_ALIGN_MIDDLE)
        drawDrillCell(settings, currentSetting, inMIdx, inMX, cellW, rowMidY, "_inM", deltaTime, isCurrentRow, rowArmed)
        drawDrillCell(settings, currentSetting, inBIdx, inBX, cellW, rowMidY, "_inB", deltaTime, isCurrentRow, rowArmed)
        drawDrillCell(settings, currentSetting, outMIdx, outMX, cellW, rowMidY, "_outM", deltaTime, isCurrentRow, rowArmed)
        drawDrillCell(settings, currentSetting, outBIdx, outBX, cellW, rowMidY, "_outB", deltaTime, isCurrentRow, rowArmed)

        -- Swap DELETE for INVALID when the drill's invalid, unless hovering Delete
        -- itself (still deletable); armed = awaiting a confirming 2nd press.
        local deleteLabel = "DELETE"
        local deleteR, deleteG, deleteB = 200, 200, 200
        if rowArmed then
            deleteLabel = "REALLY?"
            deleteR, deleteG, deleteB = 255, 160, 0
        elseif isDeleteCurrent then
            deleteR, deleteG, deleteB = 255, 60, 60
        elseif selectSetting.invalid then
            deleteLabel = "INVALID"
            deleteR, deleteG, deleteB = 255, 60, 60
        end

        gfx.TextAlign(gfx.TEXT_ALIGN_CENTER + gfx.TEXT_ALIGN_MIDDLE)
        gfx.FillColor(deleteR, deleteG, deleteB)
        gfx.Text(deleteLabel, deleteX + deleteW / 2, rowMidY)

        ::continueDrillRow::
    end

    local createSetting = settings[createRowIdx]
    if numDrills >= scrollRow and numDrills < scrollRow + maxVisibleRows then
        local createMidY = (numDrills - scrollRow) * rowH + rowH / 2
        gfx.TextAlign(gfx.TEXT_ALIGN_LEFT + gfx.TEXT_ALIGN_MIDDLE)
        gfx.FillColor(255, 255, 255)
        gfx.Text(createSetting.name, 0, createMidY)
    end

    -- Label toggles enabled/disabled depending on whether an in/out range is set.
    local addSetting = settings[addRowIdx]
    local addRowIndex0 = numDrills + 1
    if addRowIndex0 >= scrollRow and addRowIndex0 < scrollRow + maxVisibleRows then
        local addMidY = (addRowIndex0 - scrollRow) * rowH + rowH / 2
        local isAddEnabled = not addSetting.invalid

        gfx.TextAlign(gfx.TEXT_ALIGN_LEFT + gfx.TEXT_ALIGN_MIDDLE)
        gfx.FillColor(isAddEnabled and 255 or 100, isAddEnabled and 255 or 100, isAddEnabled and 255 or 100)
        gfx.Text(addSetting.name, 0, addMidY)
    end

    -- Underline's next target: x-span from currentSetting's column, y from its row.
    local strokeX1, strokeX2
    if currentSetting == createRowIdx or currentSetting == addRowIdx then
        strokeX1, strokeX2 = -5, w + 5
    else
        local col = (currentSetting - 1) % groupSize
        if col == 0 then strokeX1, strokeX2 = -5, w + 5 -- Select
        elseif col == 1 then strokeX1, strokeX2 = nameX - 2, nameX - 2 + nameW -- Rename
        elseif col == 2 then strokeX1, strokeX2 = inMX, inMX + cellW -- In-Measure
        elseif col == 3 then strokeX1, strokeX2 = inBX, inBX + cellW -- In-Beat
        elseif col == 4 then strokeX1, strokeX2 = outMX, outMX + cellW -- Out-Measure
        elseif col == 5 then strokeX1, strokeX2 = outBX, outBX + cellW -- Out-Beat
        else strokeX1, strokeX2 = deleteX, deleteX + deleteW -- Delete
        end
    end
    local strokeY = (currentVisualRow - scrollRow) * rowH + rowH - 3

    if strokeX1 ~= prevDrillStroke.x1 or strokeX2 ~= prevDrillStroke.x2 or strokeY ~= prevDrillStroke.y then
        drillStrokeAnimation.x1:restart(drillStroke.x1, strokeX1, 0.1)
        drillStrokeAnimation.x2:restart(drillStroke.x2, strokeX2, 0.1)
        drillStrokeAnimation.y:restart(drillStroke.y, strokeY, 0.1)
    end
    prevDrillStroke.x1, prevDrillStroke.x2, prevDrillStroke.y = strokeX1, strokeX2, strokeY

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

    invalidFlashTime = invalidFlashTime + deltaTime

    local posX = SettingsDiag.posX or 0.5
    local posY = SettingsDiag.posY or 0.5
    local message_1 = "Press both FXs to open/close. Use the Start button to press buttons."
    local message_2 = "Use FX keys to navigate tabs. Use arrow keys to navigate and modify settings."

    -- Measure how wide the tab bar needs to be so a trailing tab can't spill off
    -- the edge. TextAlign is persistent GL state, so set it explicitly to match
    -- the real tab-bar draw loop below or this measurement will be wrong.
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

    drillStroke.x1 = drillStrokeAnimation.x1:tick(deltaTime)
    drillStroke.x2 = drillStrokeAnimation.x2:tick(deltaTime)
    drillStroke.y = drillStrokeAnimation.y:tick(deltaTime)

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

    -- Space left below the tab bar - both the grid and generic list scroll to
    -- fit within it rather than drawing every row unconditionally.
    local listAreaHeight = diagHeight - (tabBarHeight + 5) - 10

    if tab.name == "Drills" then
        drawDrillsGrid(tab, diagWidth, listAreaHeight, deltaTime)
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