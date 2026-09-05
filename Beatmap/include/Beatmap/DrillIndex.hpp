#pragma once
#include "json.hpp"

// A single saved practice loop range, expressed in measure:beat terms
struct Drill
{
	// Empty is a valid, expected state - it means "no custom name", and the UI
	// falls back to an auto-numbered "Drill N" label rather than requiring one.
	String name;
	// 1-indexed, matches PracticeModeSettingsDialog's measure/beat display convention
	int inMeasure = 1;
	int inBeat = 1;
	int outMeasure = 1;
	int outBeat = 1;

	// UI-only bookkeeping (PracticeModeSettingsDialog): a stable identity for
	// tracking a drill across a re-sort or undo/redo. Never (de)serialized.
	int uiId = 0;
};

// A chart's list of saved drills, backed by a JSON file on disk so it can be
// shared between users by copying the file (keyed by chart content hash, not path)
struct DrillSet
{
	String path;
	String chartHash;
	Vector<Drill> drills;
	// Index into `drills` of the last-selected one, so reopening can restore it.
	// -1 = none remembered.
	int selectedIndex = -1;

	// Loads the DrillSet belonging to the chart at chartPath/chartHash.
	// Returns an empty DrillSet (ready to be populated and Save()d) if none is found.
	static DrillSet Load(const String& chartPath, const String& chartHash);

	bool Save() const;

private:
	static bool FromJson(const nlohmann::json& j, const String& chartHash, DrillSet& out);
	nlohmann::json ToJson() const;
};
