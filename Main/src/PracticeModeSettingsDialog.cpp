#include "stdafx.h"
#include "PracticeModeSettingsDialog.hpp"
#include "Beatmap/MapDatabase.hpp"

// measureCulprit says which pair (measure or beat) made the drill invalid.
static void ComputeDrillValidity(const Drill& d, bool& invalid, bool& measureCulprit)
{
    if (d.inMeasure > d.outMeasure)
    {
        invalid = true;
        measureCulprit = true;
    }
    else if (d.inMeasure == d.outMeasure && d.inBeat >= d.outBeat)
    {
        invalid = true;
        measureCulprit = false;
    }
    else
    {
        invalid = false;
        measureCulprit = false;
    }
}

// A newly created drill has both measures blank (0) until typed in, and either
// one can be filled in first - so this is true until BOTH are set, not just
// while both are still blank. Otherwise setting just the start measure would
// immediately make the drill "invalid" (in > out, since out is still 0) and
// sort it into place, before the user's even had a chance to reach the end
// measure - which is exactly the friction this is meant to avoid.
static bool IsDrillIncomplete(const Drill& d)
{
    return d.inMeasure == 0 || d.outMeasure == 0;
}

enum class DrillField { Measure, Beat };

// Shared by the 4 numeric cell getters below.
static void FillNumericCellData(BaseGameSettingsDialog::SettingData& data, const Drill& d, int value, DrillField field)
{
    data.intSetting.val = value;
    bool invalid = false, measureCulprit = false;
    ComputeDrillValidity(d, invalid, measureCulprit);
    data.invalid = invalid && (measureCulprit == (field == DrillField::Measure)) && !IsDrillIncomplete(d);
    data.trackingId = d.uiId;
}

PracticeModeSettingsDialog::PracticeModeSettingsDialog(Game& game, MapTime& lastMapTime,
    int32& tempOffset, Game::PlayOptions& playOptions, MapTimeRange& range)
    : m_chartIndex(game.GetChartIndex()), m_beatmap(game.GetBeatmap()),
    m_endTime(m_beatmap->GetLastObjectTime()),  m_lastMapTime(lastMapTime),
    m_tempOffset(tempOffset), m_playOptions(playOptions), m_range(range)
{
    m_pos = { 0.75f, 0.75f };

    m_condScore = g_gameConfig.GetInt(GameConfigKeys::DefaultFailConditionScore);
    m_condGrade = static_cast<GradeMark>(g_gameConfig.GetInt(GameConfigKeys::DefaultFailConditionGrade));
    m_condMiss = g_gameConfig.GetInt(GameConfigKeys::DefaultFailConditionMiss);
    m_condMissNear = g_gameConfig.GetInt(GameConfigKeys::DefaultFailConditionMissNear);
    m_condGauge = g_gameConfig.GetInt(GameConfigKeys::DefaultFailConditionGauge);

    m_drillSet = DrillSet::Load(m_chartIndex->path, m_chartIndex->hash);
    m_ClampDrills();
    for (Drill& d : m_drillSet.drills)
        d.uiId = m_AllocDrillId();
    m_SortDrillsInPlace(); // no tabs exist yet to rebuild - a plain in-place sort is enough here

    onClose.AddLambda([this]() { m_SaveDrillsIfDirty(); });
}

void PracticeModeSettingsDialog::m_SortDrillsInPlace()
{
    // Incomplete drills (IsDrillIncomplete) sort to the end instead of by
    // whatever's been typed so far, so filling in one field doesn't make the
    // row jump elsewhere in the list before the other field's even been set.
    std::stable_sort(m_drillSet.drills.begin(), m_drillSet.drills.end(), [](const Drill& a, const Drill& b) {
        const bool aSet = a.inMeasure > 0 && a.outMeasure > 0;
        const bool bSet = b.inMeasure > 0 && b.outMeasure > 0;
        if (aSet != bSet) return aSet;
        if (!aSet) return false;
        if (a.inMeasure != b.inMeasure) return a.inMeasure < b.inMeasure;
        return a.inBeat < b.inBeat;
    });
}

void PracticeModeSettingsDialog::m_SortDrillsAndFollow(int followUiId, DrillColumn col)
{
    m_SortDrillsInPlace();

    for (size_t j = 0; j < m_drillSet.drills.size(); ++j)
    {
        if (m_drillSet.drills[j].uiId == followUiId)
        {
            SetCurrentSetting(static_cast<int>(j) * kDrillGroupSize + static_cast<int>(col));
            m_RememberSelectedDrill(static_cast<int>(j));
            break;
        }
    }

    ResetTabs();
}

void PracticeModeSettingsDialog::m_RememberSelectedDrill(int row)
{
    if (m_drillSet.selectedIndex != row)
    {
        m_drillSet.selectedIndex = row;
        m_drillsDirty = true;
    }
}

void PracticeModeSettingsDialog::m_PushUndo()
{
    m_undoStack.Add(m_drillSet.drills);
    m_redoStack.clear();
    if (m_undoStack.size() > kMaxUndoDepth)
        m_undoStack.erase(m_undoStack.begin());
}

void PracticeModeSettingsDialog::m_ClampCurrentSettingToDrills()
{
    const int numDrills = static_cast<int>(m_drillSet.drills.size());
    const int maxIdx = numDrills * kDrillGroupSize + 1; // last valid row: Set-current-in/out
    if (GetCurrentSetting() > maxIdx)
        SetCurrentSetting(maxIdx);

    // Delete/Undo/Redo can shift indices - keep the remembered selection
    // matching wherever the cursor actually ends up.
    const int cur = GetCurrentSetting();
    if (numDrills > 0 && cur < numDrills * kDrillGroupSize)
        m_RememberSelectedDrill(cur / kDrillGroupSize);
}

void PracticeModeSettingsDialog::OnUndoPressed()
{
    if (GetCurrentTab() != kDrillsTabIndex || m_undoStack.empty())
        return;

    m_redoStack.Add(m_drillSet.drills);
    m_drillSet.drills = m_undoStack.back();
    m_undoStack.erase(m_undoStack.end() - 1);
    m_drillsDirty = true;
    m_pendingDeleteIndex = -1;
    m_ClampCurrentSettingToDrills();
    ResetTabs();
}

void PracticeModeSettingsDialog::OnRedoPressed()
{
    if (GetCurrentTab() != kDrillsTabIndex || m_redoStack.empty())
        return;

    m_undoStack.Add(m_drillSet.drills);
    m_drillSet.drills = m_redoStack.back();
    m_redoStack.erase(m_redoStack.end() - 1);
    m_drillsDirty = true;
    m_pendingDeleteIndex = -1;
    m_ClampCurrentSettingToDrills();
    ResetTabs();
}

void PracticeModeSettingsDialog::m_RequestDeleteDrill(size_t i)
{
    if (i >= m_drillSet.drills.size())
        return;

    if (m_pendingDeleteIndex != static_cast<int>(i))
    {
        // First request arms the confirm; a second on the same drill (with nothing
        // navigated/rebuilt in between - see m_AdvanceSelection etc.) actually deletes.
        m_pendingDeleteIndex = static_cast<int>(i);
        return;
    }

    m_PushUndo();
    m_drillSet.drills.erase(m_drillSet.drills.begin() + i);
    m_drillsDirty = true;
    m_pendingDeleteIndex = -1;
    m_ClampCurrentSettingToDrills();
    ResetTabs();
}

void PracticeModeSettingsDialog::OnDeleteKeyPressed()
{
    if (GetCurrentTab() != kDrillsTabIndex)
        return;

    const int numDrills = static_cast<int>(m_drillSet.drills.size());
    const int cur = GetCurrentSetting();
    if (numDrills == 0 || cur >= numDrills * kDrillGroupSize)
        return; // on a trailing button (Create New Drill / Set current in-out), not a drill row

    m_RequestDeleteDrill(static_cast<size_t>(cur / kDrillGroupSize));
}

void PracticeModeSettingsDialog::m_ClampDrills()
{
    const int maxMeasure = m_TimeToMeasure(m_endTime);
    bool changed = false;

    for (Drill& d : m_drillSet.drills)
    {
        const int clampedInMeasure = Math::Clamp(d.inMeasure, 1, maxMeasure);
        const int clampedOutMeasure = Math::Clamp(d.outMeasure, 1, maxMeasure);
        // Beat's valid range depends on the (possibly just-clamped) measure's own
        // numerator, which can differ from whatever it was when this drill was saved.
        const int clampedInBeat = Math::Clamp(d.inBeat, 1, m_NumeratorAtMeasure(clampedInMeasure));
        const int clampedOutBeat = Math::Clamp(d.outBeat, 1, m_NumeratorAtMeasure(clampedOutMeasure));

        changed = changed || clampedInMeasure != d.inMeasure || clampedOutMeasure != d.outMeasure
            || clampedInBeat != d.inBeat || clampedOutBeat != d.outBeat;

        d.inMeasure = clampedInMeasure;
        d.outMeasure = clampedOutMeasure;
        d.inBeat = clampedInBeat;
        d.outBeat = clampedOutBeat;
    }

    if (changed)
        m_drillsDirty = true;
}

void PracticeModeSettingsDialog::InitTabs()
{
    m_pendingDeleteIndex = -1; // any full rebuild invalidates whichever row index was armed

    AddTab(m_CreateMainSettingTab());
    AddTab(m_CreateDrillsTab());
    AddTab(m_CreateLoopingTab());
    AddTab(m_CreateLoopControlTab());
    AddTab(m_CreateFailConditionTab());
    AddTab(m_CreateGameSettingTab());

    // No SetCurrentTab(0) here - InitTabs() also reruns via ResetTabs() (any drill
    // edit), and that would snap the view back to Main every time.
}

void PracticeModeSettingsDialog::OnAdvanceTab()
{
    m_SaveDrillsIfDirty();
    m_pendingDeleteIndex = -1;

    if (GetCurrentTab() != kDrillsTabIndex)
        return;

    // Land on the remembered drill, falling back to the first if it's out of
    // range or now invalid.
    const int numDrills = static_cast<int>(m_drillSet.drills.size());
    int row = 0;
    if (m_drillSet.selectedIndex >= 0 && m_drillSet.selectedIndex < numDrills)
    {
        bool invalid = false, measureCulprit = false;
        ComputeDrillValidity(m_drillSet.drills[m_drillSet.selectedIndex], invalid, measureCulprit);
        if (!invalid)
            row = m_drillSet.selectedIndex;
    }
    SetCurrentSetting(row * kDrillGroupSize + static_cast<int>(DrillColumn::Select));
}

void PracticeModeSettingsDialog::m_SaveDrillsIfDirty()
{
    if (m_drillsDirty)
    {
        // Incomplete drills (IsDrillIncomplete) don't reach disk until both measures
        // are filled in - filtered out of the written copy only, m_drillSet.drills
        // itself is untouched.
        DrillSet toSave = m_drillSet;
        toSave.drills.clear();
        for (const Drill& d : m_drillSet.drills)
        {
            if (!IsDrillIncomplete(d))
                toSave.drills.Add(d);
        }
        toSave.Save();
        m_drillsDirty = false;
    }
}

void PracticeModeSettingsDialog::m_AdvanceSelection(int steps)
{
    if (steps == 0 || GetCurrentTab() != kDrillsTabIndex)
    {
        BaseGameSettingsDialog::m_AdvanceSelection(steps);
        return;
    }

    m_pendingDeleteIndex = -1; // any navigation disarms a pending Delete confirm

    // After the drill rows: Create New Drill (always selectable), then
    // Set-current-in/out (only selectable with a range set - otherwise Up/Down
    // skips it rather than landing on an inert, greyed-out stop).
    const int numDrills = static_cast<int>(m_drillSet.drills.size());
    const bool addRowSelectable = m_range.begin != m_range.end; // see m_CreateDrillsTab's addDrillButton
    const int totalRows = numDrills + 1 + (addRowSelectable ? 1 : 0);

    const int cur = GetCurrentSetting();
    const int createRowIdx = numDrills * kDrillGroupSize;
    const int addRowIdx = createRowIdx + 1;

    int curRow;
    if (cur == createRowIdx) curRow = numDrills;
    else if (cur == addRowIdx) curRow = numDrills + 1;
    else curRow = cur / kDrillGroupSize;

    const int newRow = ((curRow + steps) % totalRows + totalRows) % totalRows;
    int newIdx;
    if (newRow == numDrills) newIdx = createRowIdx;
    else if (newRow == numDrills + 1) newIdx = addRowIdx;
    else
    {
        newIdx = newRow * kDrillGroupSize; // always lands on Select, not whatever column you left
        m_RememberSelectedDrill(newRow);
    }

    m_SaveDrillsIfDirty();
    SetCurrentSetting(newIdx);
}

void PracticeModeSettingsDialog::m_NavigateColumn(int steps)
{
    if (steps == 0 || GetCurrentTab() != kDrillsTabIndex)
    {
        BaseGameSettingsDialog::m_NavigateColumn(steps);
        return;
    }

    m_pendingDeleteIndex = -1; // any navigation disarms a pending Delete confirm

    const int numDrills = static_cast<int>(m_drillSet.drills.size());
    const int cur = GetCurrentSetting();
    const bool onTrailingRow = numDrills == 0 || cur >= numDrills * kDrillGroupSize;

    if (onTrailingRow)
        return; // nothing to navigate between on Create New Drill / Set current in-out

    const int base = (cur / kDrillGroupSize) * kDrillGroupSize;
    const int col = cur % kDrillGroupSize;

    // Left/Right cycles the sub-fields (cols 1-6) only, never back onto Select
    // (col 0) - Select only loads a drill via Up/Down, so Enter can't load one
    // as a side effect of cycling past the first/last sub-field.
    constexpr int kSubFieldCount = kDrillGroupSize - 1;
    int newCol;
    if (col == 0)
    {
        const int dir = steps > 0 ? 1 : -1;
        const int remaining = steps - dir; // first step just leaves Select
        const int startSubCol = (dir > 0) ? 1 : (kSubFieldCount - 1); // right lands on In-Measure, not Rename
        const int newSubCol = ((startSubCol + remaining) % kSubFieldCount + kSubFieldCount) % kSubFieldCount;
        newCol = newSubCol + 1;
    }
    else
    {
        const int subCol = col - 1;
        const int newSubCol = ((subCol + steps) % kSubFieldCount + kSubFieldCount) % kSubFieldCount;
        newCol = newSubCol + 1;
    }

    m_SaveDrillsIfDirty();
    SetCurrentSetting(base + newCol);
}

PracticeModeSettingsDialog::Tab PracticeModeSettingsDialog::m_CreateMainSettingTab()
{
    Tab mainSettingTab = std::make_unique<TabData>();
    mainSettingTab->name = "Main";

    Setting loopBeginButton = CreateButton("Set start point (0ms) to here", [this](const auto&) {
        m_SetStartTime(Math::Clamp(m_lastMapTime, 0, m_endTime));
    });
    m_setStartButton = loopBeginButton.get();
    mainSettingTab->settings.emplace_back(std::move(loopBeginButton));

    Setting loopEndButton = CreateButton("Set end point (0ms) to here", [this](const auto&) {
        m_SetEndTime(m_lastMapTime);
    });
    m_setEndButton = loopEndButton.get();
    mainSettingTab->settings.emplace_back(std::move(loopEndButton));

    Setting loopOnSuccess = CreateBoolSetting("Loop on success", m_playOptions.loopOnSuccess);
    mainSettingTab->settings.emplace_back(std::move(loopOnSuccess));

    Setting loopOnFail = CreateBoolSetting("Loop on fail", m_playOptions.loopOnFail);
    mainSettingTab->settings.emplace_back(std::move(loopOnFail));

    Setting enableNavSetting = CreateBoolSetting(GameConfigKeys::PracticeSetupNavEnabled, "Enable navigation inputs for the setup");
    enableNavSetting->setter.Clear();
    enableNavSetting->setter.AddLambda([this](const SettingData& data) {
        g_gameConfig.Set(GameConfigKeys::PracticeSetupNavEnabled, data.boolSetting.val);
        onSettingChange.Call();
    });
    mainSettingTab->settings.emplace_back(std::move(enableNavSetting));

    Setting speedSetting = std::make_unique<SettingData>("Playback speed (%)", SettingType::Integer);
    speedSetting->intSetting.min = 25;
    speedSetting->intSetting.max = 400;
    speedSetting->intSetting.val = Math::RoundToInt(m_playOptions.playbackSpeed * 100);
    speedSetting->setter.AddLambda([this](const SettingData& data) { onSpeedChange.Call(data.intSetting.val == 100 ? 1.0f : data.intSetting.val / 100.0f); });
    speedSetting->getter.AddLambda([this](SettingData& data) { data.intSetting.val = Math::RoundToInt(m_playOptions.playbackSpeed * 100); });
    mainSettingTab->settings.emplace_back(std::move(speedSetting));

    mainSettingTab->settings.emplace_back(CreateButton("Start practice", [this](const auto&) { onPressStart.Call(); }));
    mainSettingTab->settings.emplace_back(CreateButton("Exit", [this](const auto&) { onPressExit.Call(); }));

    return mainSettingTab;
}

// Rows below capture drill position (`i`), not a `Drill&`, and re-check `i <
// m_drillSet.drills.size()` every access: ResetTabs() (from Add/Delete) only
// rebuilds next Tick(), so this frame's Render() still runs every getter
// against the old tab first - a raw reference would dangle in that window.
PracticeModeSettingsDialog::Tab PracticeModeSettingsDialog::m_CreateDrillsTab()
{
    Tab drillsTab = std::make_unique<TabData>();
    drillsTab->name = "Drills";

    for (size_t i = 0; i < m_drillSet.drills.size(); ++i)
    {
        const Drill& d = m_drillSet.drills[i];

        {
            Setting s = CreateButton("", [this, i](const auto&) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill drill = m_drillSet.drills[i];
                if (IsDrillIncomplete(drill)) return;
                // Out first, in last - both seek the playhead, so whichever runs last wins.
                m_SetEndTime(m_MeasureBeatToTime(drill.outMeasure, drill.outBeat), drill.outMeasure);
                m_SetStartTime(m_MeasureBeatToTime(drill.inMeasure, drill.inBeat), drill.inMeasure);
            });
            s->getter.AddLambda([this, i](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill& d = m_drillSet.drills[i];
                data.name = d.name.empty()
                    ? Utility::Sprintf("Drill %d: %d:%d -> %d:%d", (int)i + 1, d.inMeasure, d.inBeat, d.outMeasure, d.outBeat)
                    : Utility::Sprintf("%s: %d:%d -> %d:%d", d.name.c_str(), d.inMeasure, d.inBeat, d.outMeasure, d.outBeat);
                bool invalid = false, measureCulprit = false;
                ComputeDrillValidity(d, invalid, measureCulprit);
                data.invalid = invalid && !IsDrillIncomplete(d); // invalid blocks Select (m_PressSetting)
                data.trackingId = d.uiId;
            });
            drillsTab->settings.emplace_back(std::move(s));
        }

        {
            // Unnamed drills edit as "Drill N" (real text) but commit back to ""
            // if left unchanged, so they keep auto-renumbering.
            auto placeholderName = [i]() { return Utility::Sprintf("Drill %d", (int)i + 1); };

            Setting s = std::make_unique<SettingData>("- Name", SettingType::String);
            s->stringSetting.val = d.name.empty() ? placeholderName() : d.name;
            s->stringSetting.maxLength = 20;
            s->getter.AddLambda([this, i, placeholderName](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill& d = m_drillSet.drills[i];
                data.stringSetting.val = d.name.empty() ? placeholderName() : d.name;
                data.trackingId = d.uiId;
            });
            s->setter.AddLambda([this, i, placeholderName](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_PushUndo();
                m_drillSet.drills[i].name = (data.stringSetting.val == placeholderName()) ? "" : data.stringSetting.val;
                m_drillsDirty = true;
                ResetTabs(); // the Select row's label above embeds the name
            });
            drillsTab->settings.emplace_back(std::move(s));
        }
        {
            Setting s = std::make_unique<SettingData>("- Start measure no.", SettingType::Integer);
            s->intSetting.min = 1;
            s->intSetting.max = m_TimeToMeasure(m_endTime);
            s->intSetting.val = d.inMeasure;
            s->getter.AddLambda([this, i](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                FillNumericCellData(data, m_drillSet.drills[i], m_drillSet.drills[i].inMeasure, DrillField::Measure);
            });
            s->setter.AddLambda([this, i](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_PushUndo();
                const int followId = m_drillSet.drills[i].uiId;
                m_drillSet.drills[i].inMeasure = data.intSetting.val;
                m_drillsDirty = true;
                m_SortDrillsAndFollow(followId, DrillColumn::InMeasure); // keeps ascending start-point order
            });
            drillsTab->settings.emplace_back(std::move(s));
        }
        {
            Setting s = std::make_unique<SettingData>("- Start beat", SettingType::Integer);
            s->intSetting.min = 1;
            s->intSetting.max = m_NumeratorAtMeasure(d.inMeasure);
            s->intSetting.val = d.inBeat;
            s->getter.AddLambda([this, i](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill& d = m_drillSet.drills[i];
                data.intSetting.max = m_NumeratorAtMeasure(d.inMeasure); // live, not baked at construction
                FillNumericCellData(data, d, d.inBeat, DrillField::Beat);
            });
            s->setter.AddLambda([this, i](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_PushUndo();
                const int followId = m_drillSet.drills[i].uiId;
                m_drillSet.drills[i].inBeat = data.intSetting.val;
                m_drillsDirty = true;
                m_SortDrillsAndFollow(followId, DrillColumn::InBeat); // same-measure drills tie-break on this
            });
            drillsTab->settings.emplace_back(std::move(s));
        }
        {
            Setting s = std::make_unique<SettingData>("- End measure no.", SettingType::Integer);
            s->intSetting.min = 1;
            s->intSetting.max = m_TimeToMeasure(m_endTime);
            s->intSetting.val = d.outMeasure;
            s->getter.AddLambda([this, i](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                FillNumericCellData(data, m_drillSet.drills[i], m_drillSet.drills[i].outMeasure, DrillField::Measure);
            });
            s->setter.AddLambda([this, i](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_PushUndo();
                const int followId = m_drillSet.drills[i].uiId;
                m_drillSet.drills[i].outMeasure = data.intSetting.val;
                m_drillsDirty = true;
                // No-op for an already-complete drill (sort key is start point only),
                // but this may be what just made an incomplete one complete - without
                // this, it'd stay stuck at the end of the list until something else
                // triggered a resort.
                m_SortDrillsAndFollow(followId, DrillColumn::OutMeasure);
            });
            drillsTab->settings.emplace_back(std::move(s));
        }
        {
            Setting s = std::make_unique<SettingData>("- End beat", SettingType::Integer);
            s->intSetting.min = 1;
            s->intSetting.max = m_NumeratorAtMeasure(d.outMeasure);
            s->intSetting.val = d.outBeat;
            s->getter.AddLambda([this, i](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill& d = m_drillSet.drills[i];
                data.intSetting.max = m_NumeratorAtMeasure(d.outMeasure); // see In-Beat getter above
                FillNumericCellData(data, d, d.outBeat, DrillField::Beat);
            });
            s->setter.AddLambda([this, i](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_PushUndo();
                m_drillSet.drills[i].outBeat = data.intSetting.val;
                m_drillsDirty = true;
            });
            drillsTab->settings.emplace_back(std::move(s));
        }

        {
            Setting s = std::make_unique<SettingData>("- Delete this drill", SettingType::Button);
            s->setter.AddLambda([this, i](const SettingData&) { m_RequestDeleteDrill(i); });
            s->getter.AddLambda([this, i](SettingData& data) {
                data.armed = (m_pendingDeleteIndex == static_cast<int>(i));
            });
            drillsTab->settings.emplace_back(std::move(s));
        }
    }

    // Blank drill reachable without an in/out range already set - measures
    // blank (IsDrillIncomplete), beats default to a real 1.
    Setting createDrillButton = CreateButton("Create new drill", [this](const auto&) {
        m_PushUndo();
        Drill d;
        d.uiId = m_AllocDrillId();
        d.inMeasure = 0;
        d.inBeat = 1;
        d.outMeasure = 0;
        d.outBeat = 1;
        m_drillSet.drills.Add(d);
        m_drillsDirty = true;
        m_SortDrillsAndFollow(d.uiId, DrillColumn::InMeasure); // land the cursor ready to type it in
    });
    drillsTab->settings.emplace_back(std::move(createDrillButton));

    Setting addDrillButton = CreateButton("Set current in and out as a new drill", [this](const auto&) {
        m_PushUndo();

        // Not HasEnd() (begin < end) - start/end can legitimately be set in either
        // order, so order the two points ourselves instead.
        Drill d;
        d.uiId = m_AllocDrillId();
        m_TimeToMeasureBeat(Math::Min(m_range.begin, m_range.end), d.inMeasure, d.inBeat);
        m_TimeToMeasureBeat(Math::Max(m_range.begin, m_range.end), d.outMeasure, d.outBeat);
        m_drillSet.drills.Add(d);
        m_drillsDirty = true;
        m_SortDrillsAndFollow(d.uiId, DrillColumn::Select);
    });
    addDrillButton->getter.AddLambda([this](SettingData& data) {
        const bool hasRange = m_range.begin != m_range.end;
        data.name = hasRange
            ? "Set current in and out as a new drill"
            : "Set an In and Out position to save as a drill";
        data.invalid = !hasRange; // disabled - skins grey it out via this, not a name string match
    });
    drillsTab->settings.emplace_back(std::move(addDrillButton));

    return drillsTab;
}

void PracticeModeSettingsDialog::m_SetStartTime(MapTime time, int measure, bool seek)
{
    m_range.begin = time;
    m_startMeasure = measure >= 0 ? measure : m_TimeToMeasure(time);
    m_setStartButton->name = Utility::Sprintf("Set the start point (%dms) to here", time);
    if (seek) onSetMapTime.Call(time);
    if (m_range.end < time)
    {
        m_range.end = time;
        m_endMeasure = m_startMeasure;
    }
}

void PracticeModeSettingsDialog::m_SetEndTime(MapTime time, int measure, bool seek)
{
    m_range.end = time;
    m_endMeasure = measure >= 0 ? measure : m_TimeToMeasure(time);
    m_setEndButton->name = Utility::Sprintf("Set the end point (%dms) to here", time);

    if(seek && time != 0) onSetMapTime.Call(time);
}


PracticeModeSettingsDialog::Tab PracticeModeSettingsDialog::m_CreateLoopingTab()
{
    Tab loopingTab = std::make_unique<TabData>();
    loopingTab->name = "Looping";

    // Loop begin
    {
        m_SetStartTime(m_range.begin, -1, false); // sync display state only - see header comment

        Setting loopBeginButton = CreateButton("Set the start point to here", [this](const auto&) {
            m_SetStartTime(Math::Clamp(m_lastMapTime, 0, m_endTime));
            });
        loopingTab->settings.emplace_back(std::move(loopBeginButton));

        Setting loopStartClearButton = CreateButton("Clear the start point", [this](const auto&) {
            m_SetStartTime(0);
            });
        loopingTab->settings.emplace_back(std::move(loopStartClearButton));

        Setting loopBeginMeasureSetting = CreateIntSetting("- in measure no.", m_startMeasure, {1, m_TimeToMeasure(m_endTime)});
        loopBeginMeasureSetting->setter.AddLambda([this](const SettingData& data) {
            m_SetStartTime(m_MeasureToTime(data.intSetting.val), data.intSetting.val);
        });
        loopingTab->settings.emplace_back(std::move(loopBeginMeasureSetting));

        Setting loopBeginMSSetting = CreateIntSetting("- in milliseconds", m_range.begin, {0, m_endTime}, 50);
        loopBeginMSSetting->setter.AddLambda([this](const SettingData& data) {
            m_SetStartTime(data.intSetting.val);
        });
        loopingTab->settings.emplace_back(std::move(loopBeginMSSetting));
    }

    // Loop end
    {
        m_SetEndTime(m_range.end, -1, false); // sync display state only - see header comment

        Setting loopEndButton = CreateButton("Set the end point to here", [this](const auto&) {
            m_SetEndTime(m_lastMapTime);
            });
        loopingTab->settings.emplace_back(std::move(loopEndButton));


        Setting loopEndClearButton = CreateButton("Clear the end point", [this](const auto&) {
            m_SetEndTime(0);
            });
        loopingTab->settings.emplace_back(std::move(loopEndClearButton));

        Setting loopEndMeasureSetting = CreateIntSetting("- in measure no.", m_endMeasure, {1, m_TimeToMeasure(m_endTime)});
        loopEndMeasureSetting->setter.AddLambda([this](const SettingData& data) {
            m_SetEndTime(m_MeasureToTime(data.intSetting.val), data.intSetting.val);
        });
        loopingTab->settings.emplace_back(std::move(loopEndMeasureSetting));

        Setting loopEndMSSetting = CreateIntSetting("- in milliseconds", m_range.end, {0, m_endTime}, 50);
        loopEndMSSetting->setter.AddLambda([this](const SettingData& data) {
            m_SetEndTime(data.intSetting.val);
        });
        loopingTab->settings.emplace_back(std::move(loopEndMSSetting));
    }

    return loopingTab;
}

PracticeModeSettingsDialog::Tab PracticeModeSettingsDialog::m_CreateLoopControlTab()
{
    Tab loopControlTab = std::make_unique<TabData>();
    loopControlTab->name = "LoopControl";

    {
        Setting loopOnSuccess = CreateBoolSetting("Loop on success", m_playOptions.loopOnSuccess);
        loopControlTab->settings.emplace_back(std::move(loopOnSuccess));

        Setting loopOnFail = CreateBoolSetting("Loop on fail", m_playOptions.loopOnFail);
        loopControlTab->settings.emplace_back(std::move(loopOnFail));
    }

    {
        Setting incSpeedOnSuccess = CreateBoolSetting("Increase speed on success", m_playOptions.incSpeedOnSuccess);
        incSpeedOnSuccess->setter.AddLambda([this](const SettingData& data) { if(data.boolSetting.val) m_playOptions.loopOnSuccess = true; });
        loopControlTab->settings.emplace_back(std::move(incSpeedOnSuccess));

        Setting incSpeedAmount = std::make_unique<SettingData>("- increment (%p)", SettingType::Integer);
        incSpeedAmount->intSetting.min = 1;
        incSpeedAmount->intSetting.max = 10;
        incSpeedAmount->intSetting.val = Math::RoundToInt(m_playOptions.incSpeedAmount * 100);
        incSpeedAmount->setter.AddLambda([this](const SettingData& data) {
            m_playOptions.incSpeedOnSuccess = m_playOptions.loopOnSuccess = true;
            m_playOptions.incSpeedAmount = data.intSetting.val / 100.0f;
        });
        incSpeedAmount->getter.AddLambda([this](SettingData& data) {data.intSetting.val = Math::RoundToInt(m_playOptions.incSpeedAmount * 100); });
        loopControlTab->settings.emplace_back(std::move(incSpeedAmount));

        Setting incStreak = CreateIntSetting("- required streaks", m_playOptions.incStreak, { 1, 10 });
        incStreak->setter.AddLambda([this](const SettingData&) { m_playOptions.incSpeedOnSuccess = m_playOptions.loopOnSuccess = true; });
        loopControlTab->settings.emplace_back(std::move(incStreak));
    }

    {
        Setting decSpeedOnFail = CreateBoolSetting("Decrease speed on fail", m_playOptions.decSpeedOnFail);
        decSpeedOnFail->setter.AddLambda([this](const SettingData& data) { if (data.boolSetting.val) m_playOptions.loopOnFail = true; });
        loopControlTab->settings.emplace_back(std::move(decSpeedOnFail));

        Setting decSpeedAmount = std::make_unique<SettingData>("- decrement (%p)", SettingType::Integer);
        decSpeedAmount->intSetting.min = 1;
        decSpeedAmount->intSetting.max = 10;
        decSpeedAmount->intSetting.val = Math::RoundToInt(m_playOptions.decSpeedAmount * 100);
        decSpeedAmount->setter.AddLambda([this](const SettingData& data) {
            m_playOptions.decSpeedOnFail = true;
            m_playOptions.loopOnFail = true;
            m_playOptions.decSpeedAmount = data.intSetting.val / 100.0f;
        });
        decSpeedAmount->getter.AddLambda([this](SettingData& data) { data.intSetting.val = Math::RoundToInt(m_playOptions.decSpeedAmount * 100); });
        loopControlTab->settings.emplace_back(std::move(decSpeedAmount));

        Setting minSpeed = std::make_unique<SettingData>("- minimum speed (%)", SettingType::Integer);
        minSpeed->intSetting.min = 25;
        minSpeed->intSetting.max = 100;
        minSpeed->intSetting.val = Math::RoundToInt(m_playOptions.minPlaybackSpeed * 100);
        minSpeed->setter.AddLambda([this](const SettingData& data) {
            m_playOptions.minPlaybackSpeed = data.intSetting.val / 100.0f;
        });
        minSpeed->getter.AddLambda([this](SettingData& data) { data.intSetting.val = Math::RoundToInt(m_playOptions.minPlaybackSpeed * 100); });
        loopControlTab->settings.emplace_back(std::move(minSpeed));
    }

    {
        Setting maxRewindMeasure = CreateBoolSetting("Set maximum amount of rewinding on fail", m_playOptions.enableMaxRewind);
        maxRewindMeasure->setter.AddLambda([this](const SettingData& data) { if (data.boolSetting.val) m_playOptions.loopOnFail = true; });
        loopControlTab->settings.emplace_back(std::move(maxRewindMeasure));

        Setting rewindMeasure = CreateIntSetting("- amount in # of measures", m_playOptions.maxRewindMeasure, { 0, 20 });
        loopControlTab->settings.emplace_back(std::move(rewindMeasure));
    }

    return loopControlTab;
}

std::unique_ptr<GameFailCondition> PracticeModeSettingsDialog::m_CreateGameFailCondition(GameFailCondition::Type type)
{
    switch (type)
    {
    case GameFailCondition::Type::Score: return std::make_unique<GameFailCondition::Score>(m_condScore);
    case GameFailCondition::Type::Grade: return std::make_unique<GameFailCondition::Grade>(m_condGrade);
    case GameFailCondition::Type::Miss: return std::make_unique<GameFailCondition::MissCount>(m_condMiss);
    case GameFailCondition::Type::MissAndNear: return std::make_unique<GameFailCondition::MissAndNearCount>(m_condMissNear);
    case GameFailCondition::Type::Gauge: return std::make_unique<GameFailCondition::Gauge>(m_condGauge);
    default: return nullptr;
    }
}

PracticeModeSettingsDialog::Tab PracticeModeSettingsDialog::m_CreateFailConditionTab()
{
    Tab conditionTab = std::make_unique<TabData>();
    conditionTab->name = "Mission";

    Setting conditionType = std::make_unique<SettingData>("Fail condition", SettingType::Enum);
    for (const char* str : GameFailCondition::TYPE_STR)
        conditionType->enumSetting.options.Add(str);
    conditionType->enumSetting.val = static_cast<int>(GameFailCondition::Type::None);
    conditionType->getter.AddLambda([this](SettingData& data) {
        GameFailCondition::Type type = m_playOptions.failCondition ? m_playOptions.failCondition.get()->GetType() : GameFailCondition::Type::None;
        data.enumSetting.val = static_cast<int>(type);
    });
    conditionType->setter.AddLambda([this](const SettingData& data) {
        m_playOptions.failCondition = m_CreateGameFailCondition(static_cast<GameFailCondition::Type>(data.enumSetting.val));
    });
    conditionTab->settings.emplace_back(std::move(conditionType));

    Setting scoreCondition = CreateIntSetting("Score less than", m_condScore, { 800 * 10000, 1000 * 10000 }, 10000);
    scoreCondition->getter.AddLambda([this](SettingData& data) {
        data.intSetting.val = m_condScore;
    });
    scoreCondition->setter.AddLambda([this](const SettingData& data) {
        m_playOptions.failCondition = std::make_unique<GameFailCondition::Score>(data.intSetting.val);
    });
    conditionTab->settings.emplace_back(std::move(scoreCondition));

    Setting gradeCondition = std::make_unique<SettingData>("Grade less than", SettingType::Enum);
    for (const char* str : GRADE_MARK_STR)
        gradeCondition->enumSetting.options.Add(str);
    gradeCondition->enumSetting.val = static_cast<int>(m_condGrade);
    gradeCondition->getter.AddLambda([this](SettingData& data) {
        data.enumSetting.val = static_cast<int>(m_condGrade);
    });
    gradeCondition->setter.AddLambda([this](const SettingData& data) {
        m_condGrade = static_cast<GradeMark>(data.enumSetting.val);
        m_playOptions.failCondition = std::make_unique<GameFailCondition::Grade>(m_condGrade);
    });
    conditionTab->settings.emplace_back(std::move(gradeCondition));

    Setting missCondition = CreateIntSetting("Miss more than", m_condMiss, { 0, 100 });
    missCondition->getter.AddLambda([this](SettingData& data) {
        data.intSetting.val = m_condMiss;
    });
    missCondition->setter.AddLambda([this](const SettingData& data) {
        m_playOptions.failCondition = std::make_unique<GameFailCondition::MissCount>(data.intSetting.val);
    });
    conditionTab->settings.emplace_back(std::move(missCondition));

    Setting missNearCondition = CreateIntSetting("Miss+Near more than", m_condMissNear, { 0, 100 });
    missNearCondition->getter.AddLambda([this](SettingData& data) {
        data.intSetting.val = m_condMissNear;
    });
    missNearCondition->setter.AddLambda([this](const SettingData& data) {
        m_playOptions.failCondition = std::make_unique<GameFailCondition::MissAndNearCount>(data.intSetting.val);
    });
    conditionTab->settings.emplace_back(std::move(missNearCondition));

    Setting gaugeCondition = CreateIntSetting("Gauge less than", m_condGauge, { 0, 100 });
    gaugeCondition->getter.AddLambda([this](SettingData& data) {
        data.intSetting.val = m_condGauge;
    });
    gaugeCondition->setter.AddLambda([this](const SettingData& data) {
        m_playOptions.failCondition = std::make_unique<GameFailCondition::Gauge>(data.intSetting.val);
    });
    conditionTab->settings.emplace_back(std::move(gaugeCondition));

    return conditionTab;
}

PracticeModeSettingsDialog::Tab PracticeModeSettingsDialog::m_CreateGameSettingTab()
{
    Tab gameSettingTab = std::make_unique<TabData>();
    gameSettingTab->name = "Settings";

    Setting globalOffsetSetting = CreateIntSetting(GameConfigKeys::GlobalOffset, "Global offset", { -200, 200 });
    globalOffsetSetting->setter.Clear();
    globalOffsetSetting->setter.AddLambda([this](const SettingData& data) {
        g_gameConfig.Set(GameConfigKeys::GlobalOffset, data.intSetting.val);
        onSettingChange.Call();
    });
    gameSettingTab->settings.emplace_back(std::move(globalOffsetSetting));

    if (m_chartIndex)
    {
        Setting chartOffsetSetting = CreateIntSetting("Chart offset", m_chartIndex->custom_offset, { -200, 200 });
        chartOffsetSetting->setter.Clear();
        chartOffsetSetting->setter.AddLambda([this](const SettingData& data) {
            m_chartIndex->custom_offset = data.intSetting.val;
            onSettingChange.Call();
        });
        gameSettingTab->settings.emplace_back(std::move(chartOffsetSetting));
    }

    Setting tempOffsetSetting = CreateIntSetting("Temporary offset", m_tempOffset, { -200, 200 });
    tempOffsetSetting->setter.Clear();
    tempOffsetSetting->setter.AddLambda([this](const SettingData& data) {
        m_tempOffset = data.intSetting.val;
        onSettingChange.Call();
    });
    gameSettingTab->settings.emplace_back(std::move(tempOffsetSetting));

    Setting leadInTimeSetting = CreateIntSetting(GameConfigKeys::PracticeLeadInTime, "Lead-in time for practices (ms)", { 250, 10000 }, 250);
    leadInTimeSetting->setter.Clear();
    leadInTimeSetting->setter.AddLambda([this](const SettingData& data) {
        g_gameConfig.Set(GameConfigKeys::PracticeLeadInTime, data.intSetting.val);
        onSettingChange.Call();
    });
    gameSettingTab->settings.emplace_back(std::move(leadInTimeSetting));

    Setting enableNavSetting = CreateBoolSetting(GameConfigKeys::PracticeSetupNavEnabled, "Enable navigation inputs for the setup");
    enableNavSetting->setter.Clear();
    enableNavSetting->setter.AddLambda([this](const SettingData& data) {
        g_gameConfig.Set(GameConfigKeys::PracticeSetupNavEnabled, data.boolSetting.val);
        onSettingChange.Call();
    });
    gameSettingTab->settings.emplace_back(std::move(enableNavSetting));

    Setting revertToSetupSetting = CreateBoolSetting(GameConfigKeys::RevertToSetupAfterScoreScreen, "Revert to the setup after the result is shown");
    revertToSetupSetting->setter.Clear();
    revertToSetupSetting->setter.AddLambda([this](const SettingData& data) {
        g_gameConfig.Set(GameConfigKeys::RevertToSetupAfterScoreScreen, data.boolSetting.val);
        onSettingChange.Call();
    });
    gameSettingTab->settings.emplace_back(std::move(revertToSetupSetting));

    Setting adjustHSforLowerPSSetting = CreateBoolSetting(GameConfigKeys::AdjustHiSpeedForLowerPlaybackSpeed, "Adjust HiSpeed for playback speeds lower than x1.0");
    adjustHSforLowerPSSetting->setter.Clear();
    adjustHSforLowerPSSetting->setter.AddLambda([this](const SettingData& data) {
        g_gameConfig.Set(GameConfigKeys::AdjustHiSpeedForLowerPlaybackSpeed, data.boolSetting.val);
        onSettingChange.Call();
    });
    gameSettingTab->settings.emplace_back(std::move(adjustHSforLowerPSSetting));

    Setting adjustHSforHigherPSSetting = CreateBoolSetting(GameConfigKeys::AdjustHiSpeedForHigherPlaybackSpeed, "Adjust HiSpeed for playback speeds higher than x1.0");
    adjustHSforHigherPSSetting->setter.Clear();
    adjustHSforHigherPSSetting->setter.AddLambda([this](const SettingData& data) {
        g_gameConfig.Set(GameConfigKeys::AdjustHiSpeedForHigherPlaybackSpeed, data.boolSetting.val);
        onSettingChange.Call();
    });
    gameSettingTab->settings.emplace_back(std::move(adjustHSforHigherPSSetting));

    return gameSettingTab;
}
