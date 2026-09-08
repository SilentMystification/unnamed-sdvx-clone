#pragma once
#include "Shared/String.hpp"
#include "Shared/Vector.hpp"
#include "Vector.hpp"
#include "Map.hpp"

enum class USCFileType
{
	Regular = 0,
	Folder
};

/* 
	Result of file finding operations
*/
struct USCFileInfo
{
	String fullPath;
	uint64 lastWriteTime;
	USCFileType type;

};

/*
	File enumeration functions
*/
class Files
{
public:
	// Finds files in a given folder
	// uses the given extension filters if specified (results will be returned in a map with given exts as keys)
	// Additional interruptible flag can contain a boolean which can interrupt the search when set to true
	[[nodiscard]]
	static Map<String, Vector<USCFileInfo>> ScanFiles(const String& folder, const Vector<String>& extFilters, bool* interrupt = nullptr);

	// Finds files in a given folder, recursively
	// uses the given extension filters if specified (results will be returned in a map with given exts as keys)
	// Additional interruptible flag can contain a boolean which can interrupt the search when set to true
	[[nodiscard]]
	static Map<String, Vector<USCFileInfo>> ScanFilesRecursive(const String& folder, const Vector<String>& extFilters, bool* interrupt = nullptr);

	// Finds files in a given folder
	// uses the given extension filter if specified
	// Additional interruptible flag can contain a boolean which can interrupt the search when set to true
	[[nodiscard]]
	static Vector<USCFileInfo> ScanFiles(const String& folder, const String& extFilter = String(), bool* interrupt = nullptr);

	// Finds files in a given folder, recursively
	// uses the given extension filter if specified
	// Additional interruptible flag can contain a boolean which can interrupt the search when set to true
	[[nodiscard]]
	static Vector<USCFileInfo> ScanFilesRecursive(const String& folder, const String& extFilter = String(), bool* interrupt = nullptr);
};