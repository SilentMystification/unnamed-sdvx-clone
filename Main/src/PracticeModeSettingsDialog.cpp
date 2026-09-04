#include "stdafx.h"
#include "PracticeModeSettingsDialog.hpp"
#include "Beatmap/MapDatabase.hpp"

// A drill is invalid when its in-point isn't strictly before its out-point.
// measureCulprit tells the caller which pair of fields (measure or beat) is
// responsible, so the UI can highlight just that pair - only meaningful when
// invalid is true.
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

    onClose.AddLambda([this]() { m_SaveDrillsIfDirty(); });
}

void PracticeModeSettingsDialog::InitTabs()
{
    AddTab(m_CreateMainSettingTab());
    AddTab(m_CreateDrillsTab());
    AddTab(m_CreateLoopingTab());
    AddTab(m_CreateLoopControlTab());
    AddTab(m_CreateFailConditionTab());
    AddTab(m_CreateGameSettingTab());

    // Note: no SetCurrentTab(0) here. InitTabs() is also re-invoked by ResetTabs()
    // (BaseGameSettingsDialog::m_ResetTabs, used when Add/Delete/edit a drill), and
    // forcing tab 0 there would snap the view back to General on every edit. The base
    // class's Init() already sets m_currentTab = 0 before the first-ever InitTabs() call.
}

void PracticeModeSettingsDialog::OnAdvanceTab()
{
    m_SaveDrillsIfDirty();
}

void PracticeModeSettingsDialog::m_SaveDrillsIfDirty()
{
    if (m_drillsDirty)
    {
        m_drillSet.Save();
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

    const size_t total = GetTabSettingsCount(kDrillsTabIndex);
    if (total == 0)
        return;

    // Row 0..numDrills-1 are drills; row numDrills is the trailing "Add drill" button,
    // which only counts as a selectable row while it's actually enabled (an in/out
    // range is set) - otherwise Up/Down skips over it entirely rather than landing on
    // an inert, greyed-out stop.
    const int numDrills = static_cast<int>((total - 1) / kDrillGroupSize);
    const bool addRowSelectable = m_range.begin != m_range.end; // see m_CreateDrillsTab's addDrillButton
    const int totalRows = numDrills + (addRowSelectable ? 1 : 0);

    if (totalRows == 0)
    {
        // No drills, and the Add-drill row isn't selectable either - nothing to do.
        SetCurrentSetting(static_cast<int>(total) - 1);
        return;
    }

    const int cur = GetCurrentSetting();
    const bool onAddRow = static_cast<size_t>(cur) >= total - 1;
    const int curRow = onAddRow ? numDrills : cur / kDrillGroupSize;

    const int newRow = ((curRow + steps) % totalRows + totalRows) % totalRows;
    // Always land on Select (column 0) when moving to a different drill - "the
    // whole row" - rather than preserving whatever field you were on, so Enter
    // right after Up/Down predictably loads the drill (see m_CreateDrillsTab).
    const int newIdx = (newRow == numDrills) ? static_cast<int>(total) - 1 : newRow * kDrillGroupSize;

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

    const size_t total = GetTabSettingsCount(kDrillsTabIndex);
    const int numDrills = total > 0 ? static_cast<int>((total - 1) / kDrillGroupSize) : 0;
    const int cur = GetCurrentSetting();
    const bool onAddRow = total == 0 || static_cast<size_t>(cur) >= total - 1;

    if (onAddRow || numDrills == 0)
        return; // nothing to navigate between on the trailing "Add drill" row

    const int base = (cur / kDrillGroupSize) * kDrillGroupSize;
    const int col = cur % kDrillGroupSize;

    // Left/Right only ever cycles among the sub-fields (Rename, In-Measure,
    // In-Beat, Out-Measure, Out-Beat, Delete - cols 1-6), never back onto
    // Select (col 0). Select is "the row selector" that Enter loads the drill
    // from, and it's only reachable via Up/Down (see m_AdvanceSelection, which
    // always lands on col 0) - so Enter can never load a drill as a side
    // effect of cycling past the last/first sub-field with Left/Right.
    constexpr int kSubFieldCount = kDrillGroupSize - 1;
    int newCol;
    if (col == 0)
    {
        const int dir = steps > 0 ? 1 : -1;
        const int remaining = steps - dir; // first step just leaves Select
        // Right from Select lands on In-Measure (subCol 1), not Rename (subCol
        // 0) - Rename is still reachable by going one further left from there.
        const int startSubCol = (dir > 0) ? 1 : (kSubFieldCount - 1);
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

// Note: rows below index by drill position (`i`) rather than binding directly to a
// `Drill&` reference into m_drillSet.drills, and every access re-checks `i <
// m_drillSet.drills.size()`. Add/Delete mutate the vector (push_back/erase) inside a
// setter and then call ResetTabs(), but ResetTabs() only takes effect at the top of the
// *next* Tick() (BaseGameSettingsDialog::Tick) - Render() still runs once more this frame
// against the old (unrebuilt) tab, invoking every row's getter (BaseGameSettingsDialog::
// m_SetTables -> TabData::SetLua, called for every tab, every frame). A raw reference or
// an unguarded index would dangle/overrun during that one-frame window.
PracticeModeSettingsDialog::Tab PracticeModeSettingsDialog::m_CreateDrillsTab()
{
    Tab drillsTab = std::make_unique<TabData>();
    drillsTab->name = "Drills";

    for (size_t i = 0; i < m_drillSet.drills.size(); ++i)
    {
        const Drill& d = m_drillSet.drills[i];

        {
            // Label and invalid-state are getter-driven (not baked in at
            // construction) so they stay live without needing a full ResetTabs.
            Setting s = CreateButton("", [this, i](const auto&) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill drill = m_drillSet.drills[i];
                // Set the out point first so the in point is what's seeked to last -
                // m_SetStartTime/m_SetEndTime each seek the playhead to the time they're
                // given (via onSetMapTime), so whichever runs last decides where practice
                // playback ends up. Both also update m_range/m_startMeasure/m_endMeasure,
                // which the Loop Points tab reads live, so this naturally shows there too.
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
                // An invalid drill can't be selected to load - see m_PressSetting.
                data.invalid = invalid;
            });
            drillsTab->settings.emplace_back(std::move(s));
        }

        {
            // A drill with no custom name shows/edits as its placeholder ("Drill N")
            // rather than an empty string, so Enter-to-edit starts with real,
            // backspace-able text instead of nothing to delete. If the committed
            // text is unchanged from that placeholder, store an empty name so the
            // drill keeps auto-renumbering (e.g. if drills above it are deleted)
            // instead of freezing to a literal "Drill N" from whenever this was edited.
            auto placeholderName = [i]() { return Utility::Sprintf("Drill %d", (int)i + 1); };

            Setting s = std::make_unique<SettingData>("- Name", SettingType::String);
            s->stringSetting.val = d.name.empty() ? placeholderName() : d.name;
            s->stringSetting.maxLength = 20;
            s->getter.AddLambda([this, i, placeholderName](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                const String& name = m_drillSet.drills[i].name;
                data.stringSetting.val = name.empty() ? placeholderName() : name;
            });
            s->setter.AddLambda([this, i, placeholderName](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_drillSet.drills[i].name = (data.stringSetting.val == placeholderName()) ? "" : data.stringSetting.val;
                m_drillsDirty = true;
                ResetTabs(); // the Select row's label above embeds the name
            });
            drillsTab->settings.emplace_back(std::move(s));
        }
        {
            Setting s = std::make_unique<SettingData>("- In measure no.", SettingType::Integer);
            s->intSetting.min = 1;
            s->intSetting.max = m_TimeToMeasure(m_endTime);
            s->intSetting.val = d.inMeasure;
            s->getter.AddLambda([this, i](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill& d = m_drillSet.drills[i];
                data.intSetting.val = d.inMeasure;
                bool invalid = false, measureCulprit = false;
                ComputeDrillValidity(d, invalid, measureCulprit);
                data.invalid = invalid && measureCulprit;
            });
            s->setter.AddLambda([this, i](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_drillSet.drills[i].inMeasure = data.intSetting.val;
                m_drillsDirty = true;
                ResetTabs(); // beat's valid max depends on this measure's numerator
            });
            drillsTab->settings.emplace_back(std::move(s));
        }
        {
            Setting s = std::make_unique<SettingData>("- In beat", SettingType::Integer);
            s->intSetting.min = 1;
            s->intSetting.max = m_NumeratorAtMeasure(d.inMeasure);
            s->intSetting.val = d.inBeat;
            s->getter.AddLambda([this, i](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill& d = m_drillSet.drills[i];
                data.intSetting.val = d.inBeat;
                bool invalid = false, measureCulprit = false;
                ComputeDrillValidity(d, invalid, measureCulprit);
                data.invalid = invalid && !measureCulprit;
            });
            s->setter.AddLambda([this, i](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_drillSet.drills[i].inBeat = data.intSetting.val;
                m_drillsDirty = true;
            });
            drillsTab->settings.emplace_back(std::move(s));
        }
        {
            Setting s = std::make_unique<SettingData>("- Out measure no.", SettingType::Integer);
            s->intSetting.min = 1;
            s->intSetting.max = m_TimeToMeasure(m_endTime);
            s->intSetting.val = d.outMeasure;
            s->getter.AddLambda([this, i](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill& d = m_drillSet.drills[i];
                data.intSetting.val = d.outMeasure;
                bool invalid = false, measureCulprit = false;
                ComputeDrillValidity(d, invalid, measureCulprit);
                data.invalid = invalid && measureCulprit;
            });
            s->setter.AddLambda([this, i](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_drillSet.drills[i].outMeasure = data.intSetting.val;
                m_drillsDirty = true;
                ResetTabs();
            });
            drillsTab->settings.emplace_back(std::move(s));
        }
        {
            Setting s = std::make_unique<SettingData>("- Out beat", SettingType::Integer);
            s->intSetting.min = 1;
            s->intSetting.max = m_NumeratorAtMeasure(d.outMeasure);
            s->intSetting.val = d.outBeat;
            s->getter.AddLambda([this, i](SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                const Drill& d = m_drillSet.drills[i];
                data.intSetting.val = d.outBeat;
                bool invalid = false, measureCulprit = false;
                ComputeDrillValidity(d, invalid, measureCulprit);
                data.invalid = invalid && !measureCulprit;
            });
            s->setter.AddLambda([this, i](const SettingData& data) {
                if (i >= m_drillSet.drills.size()) return;
                m_drillSet.drills[i].outBeat = data.intSetting.val;
                m_drillsDirty = true;
            });
            drillsTab->settings.emplace_back(std::move(s));
        }

        drillsTab->settings.emplace_back(CreateButton("- Delete this drill", [this, i](const auto&) {
            if (i >= m_drillSet.drills.size()) return;
            m_drillSet.drills.erase(m_drillSet.drills.begin() + i);
            m_drillsDirty = true;
            ResetTabs();
        }));
    }

    Setting addDrillButton = CreateButton("Set current in and out as a new drill", [this](const auto&) {
        if (m_range.begin == m_range.end)
            return; // no in/out set - button is shown disabled, ignore stray presses

        // Don't rely on HasEnd() (begin < end) here: "Set start"/"Set end" (and the
        // measure fields) can legitimately be pressed in either order - e.g. setting
        // start after already setting a later end - which HasEnd() would read as
        // "unset". Any two distinct points are a valid drill; order them ourselves.
        Drill d;
        m_TimeToMeasureBeat(Math::Min(m_range.begin, m_range.end), d.inMeasure, d.inBeat);
        m_TimeToMeasureBeat(Math::Max(m_range.begin, m_range.end), d.outMeasure, d.outBeat);
        m_drillSet.drills.Add(d);
        m_drillsDirty = true;
        ResetTabs();
    });
    // The button's own label doubles as the enabled/disabled signal for the skin
    // renderer, refreshed live every frame (buttons have no value to bind a
    // getter to otherwise, but the name field works the same way).
    addDrillButton->getter.AddLambda([this](SettingData& data) {
        data.name = (m_range.begin != m_range.end)
            ? "Set current in and out as a new drill"
            : "Set an In and Out position to save as a drill";
    });
    drillsTab->settings.emplace_back(std::move(addDrillButton));

    return drillsTab;
}

void PracticeModeSettingsDialog::m_SetStartTime(MapTime time, int measure)
{
    m_range.begin = time;
    m_startMeasure = measure >= 0 ? measure : m_TimeToMeasure(time);
    m_setStartButton->name = Utility::Sprintf("Set the start point (%dms) to here", time);
    onSetMapTime.Call(time);
    if (m_range.end < time)
    {
        m_range.end = time;
        m_endMeasure = m_startMeasure;
    }
}

void PracticeModeSettingsDialog::m_SetEndTime(MapTime time, int measure)
{
    m_range.end = time;
    m_endMeasure = measure >= 0 ? measure : m_TimeToMeasure(time);
    m_setEndButton->name = Utility::Sprintf("Set the end point (%dms) to here", time);

    if(time != 0) onSetMapTime.Call(time);
}


PracticeModeSettingsDialog::Tab PracticeModeSettingsDialog::m_CreateLoopingTab()
{
    Tab loopingTab = std::make_unique<TabData>();
    loopingTab->name = "Looping";

    // Loop begin
    {
        m_SetStartTime(m_range.begin);

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
        m_SetEndTime(m_range.end);

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
