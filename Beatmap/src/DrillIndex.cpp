#include "stdafx.h"
#include "DrillIndex.hpp"
#include "Shared/Files.hpp"

// Path::ReplaceExtension is declared in Path.hpp but has no implementation
// anywhere in the codebase, so build the sibling path manually instead.
static String MakeSiblingPath(const String& chartPath, const String& newExt)
{
	const String ext = Path::GetExtension(chartPath);
	if (ext.empty())
		return chartPath + "." + newExt;
	return chartPath.substr(0, chartPath.length() - ext.length()) + newExt;
}

static nlohmann::json LoadJsonFile(const String& path)
{
	File f;
	if (!f.OpenRead(path))
		return nlohmann::json();

	Buffer buf;
	buf.resize(f.GetSize());
	f.Read(buf.data(), buf.size());

	try
	{
		const String jsonData((char*)buf.data(), buf.size());
		return nlohmann::json::parse(*jsonData);
	}
	catch (const std::exception& e)
	{
		Logf("Encountered JSON error with %s: %s", Logger::Severity::Warning, *path, e.what());
	}
	return nlohmann::json();
}

bool DrillSet::FromJson(const nlohmann::json& j, const String& chartHash, DrillSet& out)
{
	if (!j.is_object() || !j.contains("drills") || !j["drills"].is_array())
		return false;
	if (!j.contains("chart_hash") || !j["chart_hash"].is_string())
		return false;

	String hash;
	j["chart_hash"].get_to(hash);
	if (hash != chartHash)
		return false;

	out.chartHash = chartHash;
	out.drills.clear();
	for (const auto& dj : j["drills"])
	{
		if (!dj.is_object())
			continue;

		Drill d;
		if (dj.contains("name") && dj["name"].is_string())
			dj["name"].get_to(d.name);
		d.inMeasure = std::max(1, dj.value("in_measure", 1));
		d.inBeat = std::max(1, dj.value("in_beat", 1));
		d.outMeasure = std::max(1, dj.value("out_measure", 1));
		d.outBeat = std::max(1, dj.value("out_beat", 1));
		out.drills.Add(d);
	}
	out.selectedIndex = j.value("selected_index", -1);
	return true;
}

nlohmann::json DrillSet::ToJson() const
{
	nlohmann::json j;
	j["version"] = 1;
	j["chart_hash"] = *chartHash;
	j["drills"] = nlohmann::json::array();
	for (const auto& d : drills)
	{
		nlohmann::json dj;
		dj["name"] = *d.name;
		dj["in_measure"] = d.inMeasure;
		dj["in_beat"] = d.inBeat;
		dj["out_measure"] = d.outMeasure;
		dj["out_beat"] = d.outBeat;
		j["drills"].push_back(dj);
	}
	j["selected_index"] = selectedIndex;
	return j;
}

DrillSet DrillSet::Load(const String& chartPath, const String& chartHash)
{
	const String sibling = MakeSiblingPath(chartPath, "drills");

	DrillSet result;
	if (Path::FileExists(sibling))
	{
		nlohmann::json j = LoadJsonFile(sibling);
		if (FromJson(j, chartHash, result))
		{
			result.path = sibling;
			return result;
		}
	}

	// Fall back to a folder-local scan for a differently-named shared file
	// whose contents still match this chart's hash
	const String folder = Path::RemoveLast(chartPath);
	for (const FileInfo& fi : Files::ScanFiles(folder, "drills"))
	{
		nlohmann::json j = LoadJsonFile(fi.fullPath);
		if (FromJson(j, chartHash, result))
		{
			result.path = sibling; // edits always save to the canonical sibling path
			return result;
		}
	}

	result = DrillSet();
	result.path = sibling;
	result.chartHash = chartHash;
	return result;
}

bool DrillSet::Save() const
{
	// File::OpenWrite doesn't truncate (no O_TRUNC) - writing directly to `path`
	// would leave trailing bytes from a previous, longer save behind, corrupting
	// the JSON. Write to a temp file and rename over the original instead, which
	// also means a crash/power-loss mid-write can't corrupt the existing file.
	const String tmpPath = path + ".tmp";

	File f;
	if (!f.OpenWrite(tmpPath))
		return false;

	const String data = ToJson().dump(4);
	f.Write(*data, data.length());
	f.Close();

	return Path::Rename(tmpPath, path, true);
}
