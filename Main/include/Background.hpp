#pragma once

// Result of resolving and reading a background/foreground script ahead of time - see
// PrefetchBackgroundScript(). Pure data (no lua_State, no GL), safe to build on a worker
// thread and hand to the main thread afterward.
struct BackgroundScriptPrefetch
{
	// True once resolution found a real script to use (either the chart's own layer, or
	// the skin's fallback) - false only in the rare case neither exists, in which case
	// scriptSource is empty and Background::Init falls back to its own (slower,
	// synchronous, main-thread) file-probing path unchanged.
	bool found = false;
	// Resolved folder the script came from (with trailing separator), needed for the
	// sibling ".fs" material path and any relative asset lookups the script does.
	String folderPath;
	// "fg" or "bg" - which script this is.
	String fname;
	// Raw contents of folderPath + fname + ".lua". Lua source is text, so a String holds
	// it directly - fed to luaL_loadbuffer instead of luaL_dofile re-reading from disk.
	String scriptSource;
};

// Resolves which background/foreground script a chart will use (the chart's own named
// layer, or the skin's fallback if that doesn't exist) and reads its bytes - pure string
// logic and file reads, deliberately no lua_State or GL calls, so this is safe to call
// from IAsyncLoadable::AsyncLoad()'s worker thread ahead of the main-thread Init() that
// used to do this same resolution+read synchronously. Mirrors TestBackground::Init's own
// resolution logic exactly (see Background.cpp) - that function is the single source of
// truth this one is kept in sync with.
BackgroundScriptPrefetch PrefetchBackgroundScript(const String& chartRootPath, const String& foregroundPathSetting, bool foreground);

/*
	Game background base class
*/
class Background
{
public:
	virtual ~Background() = default;
	virtual bool Init(bool foreground) = 0;
	virtual void Render(float deltaTime) = 0;

	// Set before calling Init(), if a prefetch result is available (see
	// PrefetchBackgroundScript above) - null means "no prefetch, do it the old
	// synchronous way". Plain field rather than a virtual setter since every concrete
	// Background subclass that cares (currently just TestBackground) can read it directly.
	const BackgroundScriptPrefetch* scriptPrefetch = nullptr;

	class Game* game;
};

// Creates the default game background
Background* CreateBackground(class Game* game, bool foreground = false, const BackgroundScriptPrefetch* prefetch = nullptr);