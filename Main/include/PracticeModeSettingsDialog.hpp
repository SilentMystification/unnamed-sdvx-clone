#pragma once
#include "BaseGameSettingsDialog.hpp"
#include "Beatmap/BeatmapObjects.hpp"
#include "Beatmap/DrillIndex.hpp"
#include "Game.hpp"

struct ChartIndex;

// Column order matches m_CreateDrillsTab's emplace_back order - Select is
// always reachable via Up/Down (m_AdvanceSelection), the rest via Left/Right.
enum class DrillColumn { Select, Rename, InMeasure, InBeat, OutMeasure, OutBeat, Delete };

class PracticeModeSettingsDialog : public BaseGameSettingsDialog
{
public:
	virtual ~PracticeModeSettingsDialog() = default;
	PracticeModeSettingsDialog(Game& game, MapTime& lastMapTime,
		int32& tempOffset, Game::PlayOptions& playOptions, MapTimeRange& range);
	void InitTabs() override;

	Delegate<MapTime> onSetMapTime;
	Delegate<float> onSpeedChange;
	Delegate<> onSettingChange;
	Delegate<> onPressStart;
	Delegate<> onPressExit;

private:
	Tab m_CreateMainSettingTab();
	Tab m_CreateDrillsTab();
	Tab m_CreateLoopingTab();
	Tab m_CreateLoopControlTab();
	Tab m_CreateFailConditionTab();
	Tab m_CreateGameSettingTab();

	void OnAdvanceTab() override;
	void OnDeleteKeyPressed() override;
	void OnUndoPressed() override;
	void OnRedoPressed() override;
	void m_SaveDrillsIfDirty();

	// On the Drills tab: Up/Down moves between drills (landing on Select), Left/
	// Right moves between a drill's sub-fields. Falls back to flat row
	// navigation on every other tab.
	void m_AdvanceSelection(int steps) override;
	void m_NavigateColumn(int steps) override;

	static constexpr int kDrillsTabIndex = 1; // Drills is the 2nd tab added in InitTabs()
	static constexpr int kDrillGroupSize = 7; // Select, Rename, In-Measure, In-Beat, Out-Measure, Out-Beat, Delete
	static constexpr size_t kMaxUndoDepth = 50;

	// Drills stay sorted by start point ascending. followUiId/col land the
	// cursor back on the right drill/column after the reorder moves its row.
	void m_SortDrillsInPlace();
	void m_SortDrillsAndFollow(int followUiId, DrillColumn col);

	// Remembers which drill (by row) to land on when re-entering the Drills tab
	// or reopening the dialog - persisted via DrillSet::selectedIndex.
	void m_RememberSelectedDrill(int row);

	int m_AllocDrillId() { return m_nextDrillId++; }
	int m_nextDrillId = 1;

	// Re-clamps GetCurrentSetting() after Delete/Undo/Redo shrinks the drill
	// list, else the underline desyncs until the next arrow key wraps it back.
	void m_ClampCurrentSettingToDrills();

	// Drills-tab-only undo/redo. m_PushUndo() snapshots the list before a
	// mutation and clears the redo stack.
	void m_PushUndo();
	Vector<Vector<Drill>> m_undoStack;
	Vector<Vector<Drill>> m_redoStack;

	// Clamps loaded drills to the chart's current range/time signature, in case
	// the chart was re-edited since a drill was saved. Marks m_drillsDirty if changed.
	void m_ClampDrills();

	// Drill index of a Delete awaiting a second, confirming request; -1 if none armed.
	int m_pendingDeleteIndex = -1;
	// Shared by the Delete button and Del key - arms on first call, deletes on the second.
	void m_RequestDeleteDrill(size_t i);

	inline MapTime m_MeasureToTime(int measure) const { return m_beatmap->GetMapTimeFromMeasureInd(measure-1); }
	inline int m_TimeToMeasure(MapTime time) const { return m_beatmap->GetMeasureIndFromMapTime(time)+1; }

	inline MapTime m_MeasureBeatToTime(int measure, int beat) const { return m_beatmap->GetMapTimeFromMeasureBeat(measure-1, beat-1); }
	inline void m_TimeToMeasureBeat(MapTime time, int& measure, int& beat) const
	{
		int m = 0, b = 0;
		m_beatmap->GetMeasureBeatFromMapTime(time, m, b);
		measure = m+1;
		beat = b+1;
	}
	// Valid beat range (1..numerator) at the given 1-indexed measure
	inline int m_NumeratorAtMeasure(int measure) const { return m_beatmap->GetTimingPoint(m_MeasureToTime(measure))->numerator; }

	// seek=false updates display state without seeking the playhead - needed for
	// m_CreateLoopingTab's construction-time calls, since InitTabs() reruns on
	// every ResetTabs() (e.g. a drill edit) and would otherwise re-seek then too.
	void m_SetStartTime(MapTime time, int measure = -1, bool seek = true);
	void m_SetEndTime(MapTime time, int measure = -1, bool seek = true);

	DrillSet m_drillSet;
	bool m_drillsDirty = false;

	std::unique_ptr<GameFailCondition> m_CreateGameFailCondition(GameFailCondition::Type type);

	ChartIndex* m_chartIndex;
	Ref<Beatmap> m_beatmap;
	MapTime m_endTime;
	MapTime& m_lastMapTime;
	int32& m_tempOffset;

	Game::PlayOptions& m_playOptions;
	// for ranges, use m_range instead of m_playOptions
	MapTimeRange& m_range;

	// Offset by 1
	int m_startMeasure = 1;
	int m_endMeasure = 1;

	SettingData* m_setStartButton = nullptr;
	SettingData* m_setEndButton = nullptr;

	// Fail conditions
	int m_condScore = static_cast<int>(MAX_SCORE);
	GradeMark m_condGrade = GradeMark::PUC;
	int m_condMiss = 0;
	int m_condMissNear = 0;
	int m_condGauge = 0;
};

