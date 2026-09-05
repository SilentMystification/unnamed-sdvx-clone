#pragma once

#include "GameConfig.hpp"
#include "Input.hpp"

enum class SettingType
{
    Integer,
    Floating,
    Boolean,
    Enum,
    Button,
    String,
};

// Base class for popup dialog for game settings
class BaseGameSettingsDialog
{
public:
    typedef struct SettingData
    {
        SettingData(const String& name, SettingType type) : name(name), type(type) {}

        String name;
        SettingType type;

        // Called to update setting
        Delegate<SettingData&> getter;
        // Called when the setting is updated
        Delegate<const SettingData&> setter;
        // Optional, set by the getter: row holds an invalid value. Display hint
        // (e.g. render red) that also blocks Enter/Select on a Button row (see m_PressSetting).
        bool invalid = false;
        // Optional, set by the getter: row needs a second confirming action before
        // its effect happens (e.g. Delete deletes on the 2nd press). Display hint only.
        bool armed = false;
        // Optional, set by the getter: stable per-row identity for the skin to key
        // animation/change-tracking state by, when array position isn't reliable
        // (e.g. Drills tab rows, which resort). 0 = not needed.
        int trackingId = 0;

        struct
        {
            float val;
            float min;
            float max;
            float mult;
        } floatSetting;

        struct
        {
            int val;
            int min;
            int max;
            int step = 1;
            int div = 1; // For decimal values
        } intSetting;

        struct
        {
            int val;
            Map<uint32, int> enumToVal;
            Vector<String> options;
        } enumSetting;

        struct
        {
            bool val;
        } boolSetting;

        struct
        {
            String val;
            size_t maxLength = 32;
        } stringSetting;

    } SettingData;
    typedef std::unique_ptr<SettingData> Setting;

    typedef struct TabData
    {
        String name;
        Vector<Setting> settings;

        void SetLua(struct lua_State* lua, const SettingData* editingSetting);
    } TabData;
    typedef std::unique_ptr<TabData> Tab;

public:
    ~BaseGameSettingsDialog();

    void Tick(float deltaTime);
    void Render(float deltaTime);
    bool Init();
    [[nodiscard]]
    bool IsSelectionOnPressable();

    inline void AddTab(Tab tab) { m_tabs.emplace_back(std::move(tab)); }

    [[nodiscard]]
    inline bool IsActive() const noexcept { return m_active; }
    [[nodiscard]]
    inline bool IsInitialized() const noexcept { return m_isInitialized; }

    inline void Open() noexcept { assert(!m_active); m_targetActive = true; }
    inline void Close() noexcept { assert(m_active); m_targetActive = false; }

    void ResetTabs();

    Delegate<> onClose;

protected:
    virtual void InitTabs() = 0;
    virtual void OnAdvanceTab() {};
    virtual void OnDeleteKeyPressed() {}; // Del, not Backspace (which only edits text)
    virtual void OnUndoPressed() {};
    virtual void OnRedoPressed() {};

    [[nodiscard]]
    Setting CreateBoolSetting(GameConfigKeys key, String name);
    [[nodiscard]]
    Setting CreateIntSetting(GameConfigKeys key, String name, Vector2i range, int step = 1);
    [[nodiscard]]
    Setting CreateFloatSetting(GameConfigKeys key, String name, Vector2 range, float mult = 1.0f);

    [[nodiscard]]
    Setting CreateBoolSetting(String label, bool& val);
    [[nodiscard]]
    Setting CreateIntSetting(String label, int& val, Vector2i range, int step = 1);
    [[nodiscard]]
    Setting CreateButton(String label, std::function<void(const BaseGameSettingsDialog::SettingData&)>&& callback);

    template <typename EnumClass>
    [[nodiscard]]
    Setting CreateEnumSetting(GameConfigKeys key, String name);

    [[nodiscard]]
    inline int GetCurrentTab() const noexcept { return m_currentTab; }
    inline void SetCurrentTab(int tabIndex) noexcept { m_currentTab = tabIndex; }

    [[nodiscard]]
    inline int GetCurrentSetting() const noexcept { return m_currentSetting; }
    inline void SetCurrentSetting(int settingIndex) noexcept { m_currentSetting = settingIndex; }
    [[nodiscard]]
    inline size_t GetTabSettingsCount(int tabIndex) const { return m_tabs[tabIndex]->settings.size(); }

    // Up/Down (and knob 0): which row is selected within the current tab.
    // Default: move one row at a time through the tab's flat setting list.
    virtual void m_AdvanceSelection(int steps);
    // Left/Right (and knob 1): default forwards to m_ChangeStepSetting (edit the
    // current row's value) - kept separate from BT0-3, which always call
    // m_ChangeStepSetting directly, so a tab can override just this one to mean
    // "navigate between sub-options" without losing BT0-3 as a way to edit values.
    virtual void m_NavigateColumn(int steps);

    Vector2 m_pos = { 0.5f, 0.5f };

private:
    void m_SetTables();
    void m_AdvanceTab(int steps);
    void m_ChangeStepSetting(int steps); //int, enum, toggle, are all advanced in distinct steps
    void m_PressSetting();
    void m_OnButtonPressed(Input::Button button, int32 delta);
    void m_OnButtonReleased(Input::Button button, int32 delta);
    void m_OnKeyPressed(SDL_Scancode code, int32 delta);
    void m_ResetTabs();

    // Typed keyboard entry for the currently-selected Integer/String row,
    // triggered by pressing Enter on it (Enter is the dedicated "start editing"
    // key - BT_S/Select still only presses buttons and toggles booleans, same
    // as before this feature existed, so a controller's Select action never
    // changes meaning). While active, all other button/key input is suppressed
    // (see the guards in m_OnButtonPressed/m_OnButtonReleased/Tick) so typed
    // digits/characters - including keyboard '1', which doubles as BT_S - can't
    // also trigger unrelated dialog actions, the same way SongSelect's search
    // bar suppresses normal input while it has focus.
    void m_OnEnterPressed();
    // startEmpty: true for "start typing to overwrite" (Excel-style - the
    // triggering character is applied on top of an empty buffer), false for
    // "Enter to modify" (buffer preloaded with the current value).
    void m_StartEditingValue(SettingData* setting, bool startEmpty = false);
    void m_StopEditingValue();
    void m_CommitEditingValue();
    void m_CancelEditingValue();
    void m_OnEditTextInput(const String& text);
    void m_OnEditKeyRepeat(SDL_Scancode code);

    SettingData* m_editingSetting = nullptr;
    String m_editBuffer;
    int m_editOriginalValue = 0;
    String m_editOriginalString;

    // Set a target in open/close and apply it in the next tick because stuff
    bool m_targetActive = false;
    bool m_active = false;
    bool m_closing = false;
    bool m_isInitialized = false;

    bool m_enableFXInputs = false;

    bool m_needsToResetTabs = false;

    int m_currentTab = 0;
    int m_currentSetting = 0;

    float m_knobAdvance[2] = {0.0f, 0.0f};
    float m_sensMult = 1.0f;

    struct lua_State* m_lua = nullptr;
    Vector<Tab> m_tabs;
};

template <typename EnumClass>
BaseGameSettingsDialog::Setting BaseGameSettingsDialog::CreateEnumSetting(GameConfigKeys key, String name)
{
    Setting s = std::make_unique<SettingData>(name, SettingType::Enum);

    int ind = 0;

    EnumStringMap<typename EnumClass::EnumType> nameMap = EnumClass::GetMap();
    for (auto& it : nameMap)
    {
        s->enumSetting.options.Add(it.second);
        s->enumSetting.enumToVal.Add(static_cast<int>(it.first), ind++);
    }

    s->enumSetting.val = s->enumSetting.enumToVal[static_cast<uint32>(g_gameConfig.GetEnum<EnumClass>(key))];

    auto getter = [key](SettingData& data) {
        data.enumSetting.val = data.enumSetting.enumToVal[static_cast<uint32>(g_gameConfig.GetEnum<EnumClass>(key))];
    };

    auto setter = [key](const SettingData& data) {
        g_gameConfig.SetEnum<EnumClass>(key, EnumClass::FromString(data.enumSetting.options[data.enumSetting.val]));
    };

    s->getter.AddLambda(std::move(getter));
    s->setter.AddLambda(std::move(setter));

    return s;
}