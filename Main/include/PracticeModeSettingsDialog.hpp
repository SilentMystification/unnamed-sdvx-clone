#pragma once
#include "BaseGameSettingsDialog.hpp"
#include "Beatmap/BeatmapObjects.hpp"
#include "Beatmap/DrillIndex.hpp"
#include "Game.hpp"

struct ChartIndex;
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
	void m_SaveDrillsIfDirty();

	// On the Drills tab: Up/Down moves between whole drills (always landing back
	// on the Select sub-option, not whichever column you were on), Left/Right
	// moves between the sub-options (Select/Rename/In-Measure/In-Beat/
	// Out-Measure/Out-Beat/Delete) within the current drill. Falls back to the
	// base class's flat row navigation on every other tab. Both flush a pending
	// drill save when they move off the current drill/field.
	void m_AdvanceSelection(int steps) override;
	void m_NavigateColumn(int steps) override;

	static constexpr int kDrillsTabIndex = 1; // Drills is the 2nd tab added in InitTabs()
	static constexpr int kDrillGroupSize = 7; // Select, Rename, In-Measure, In-Beat, Out-Measure, Out-Beat, Delete

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

	void m_SetStartTime(MapTime time, int measure = -1);
	void m_SetEndTime(MapTime time, int measure = -1);

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

