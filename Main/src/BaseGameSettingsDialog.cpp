#include "stdafx.h"
#include "BaseGameSettingsDialog.hpp"

#include "lua.hpp"
#include "Application.hpp"

BaseGameSettingsDialog::~BaseGameSettingsDialog()
{
	if (m_lua)
	{
		g_application->DisposeLua(m_lua);
		m_lua = nullptr;
	}

	for (auto& tab : m_tabs)
	{
		tab->settings.clear();
	}

	m_tabs.clear();

	g_input.OnButtonReleased.RemoveAll(this);
	g_input.OnButtonPressed.RemoveAll(this);
	g_gameWindow->OnKeyPressed.RemoveAll(this);
	g_gameWindow->OnTextInput.RemoveAll(this);
	g_gameWindow->OnKeyRepeat.RemoveAll(this);
	SDL_StopTextInput();
}

void BaseGameSettingsDialog::ResetTabs()
{
    m_needsToResetTabs = true;
}

void BaseGameSettingsDialog::m_ResetTabs()
{
	for (auto& tab : m_tabs)
	{
		tab->settings.clear();
	}

	m_tabs.clear();

    InitTabs();
}

void BaseGameSettingsDialog::Tick(float deltaTime)
{
    if (m_active != m_targetActive)
    {
        m_active = m_targetActive;
        if (!m_active)
        {
            onClose.Call();
        }
    }

    if (!m_active)
    {
        return;
    }

    if (m_needsToResetTabs)
    {
        m_ResetTabs();
        m_needsToResetTabs = false;
    }

    if (!m_enableFXInputs)
    {
        m_enableFXInputs = !g_input.GetButton(Input::Button::FX_0) && !g_input.GetButton(Input::Button::FX_1);
    }

    if (m_editingSetting)
        return; // typed entry in progress - knobs shouldn't navigate or step values

    //tick inputs
    for (size_t i = 0; i < 2; i++)
    {
        m_knobAdvance[i] += g_input.GetInputLaserDir(i) * m_sensMult;
    }

    const int knobAdvance0Trunc = static_cast<int>(truncf(m_knobAdvance[0]));
    m_AdvanceSelection(knobAdvance0Trunc);
    m_knobAdvance[0] -= knobAdvance0Trunc;

    if (static_cast<size_t>(m_currentSetting) >= m_tabs[m_currentTab]->settings.size())
        return;

    auto currentSetting = m_tabs[m_currentTab]->settings.at(m_currentSetting).get();
    if (currentSetting->type == SettingType::Floating)
    {
        float advance = m_knobAdvance[1] * currentSetting->floatSetting.mult;
        if (g_input.GetButton(Input::Button::BT_0))
            advance /= 4.0f;
        if (g_input.GetButton(Input::Button::BT_1))
            advance /= 2.0f;
        if (g_input.GetButton(Input::Button::BT_2))
            advance *= 2.0f;
        if (g_input.GetButton(Input::Button::BT_3))
            advance *= 4.0f;
        currentSetting->floatSetting.val += advance;
        currentSetting->floatSetting.val = Math::Clamp(currentSetting->floatSetting.val,
            currentSetting->floatSetting.min,
            currentSetting->floatSetting.max);

        currentSetting->setter.Call(*currentSetting);
        m_knobAdvance[1] = 0.f;
    }
    else
    {
        const int knobAdvance1Trunc = static_cast<int>(truncf(m_knobAdvance[1]));
        m_NavigateColumn(knobAdvance1Trunc);
        m_knobAdvance[1] -= knobAdvance1Trunc;
    }
}

void BaseGameSettingsDialog::Render(float deltaTime)
{
    m_SetTables();
    lua_getglobal(m_lua, "render");
    lua_pushnumber(m_lua, deltaTime);
    lua_pushboolean(m_lua, m_active);
    if (lua_pcall(m_lua, 2, 0, 0) != 0)
    {
        g_application->ScriptError("gamesettingsdialog", m_lua);
    }
    lua_settop(m_lua, 0);
}

bool BaseGameSettingsDialog::Init()
{
    m_tabs.clear();
    m_currentTab = 0;

    m_lua = g_application->LoadScript("gamesettingsdialog");
    if (!m_lua)
    {
		return false;
    }

    InitTabs();

    m_SetTables();
    g_input.OnButtonPressed.Add(this, &BaseGameSettingsDialog::m_OnButtonPressed);
    g_input.OnButtonReleased.Add(this, &BaseGameSettingsDialog::m_OnButtonReleased);
    g_gameWindow->OnKeyPressed.Add(this, &BaseGameSettingsDialog::m_OnKeyPressed);

    // Stays on for the dialog's whole lifetime so typing on a selected row starts
    // an Excel-style overwrite edit even before Enter - see m_OnEditTextInput.
    SDL_StartTextInput();
    g_gameWindow->OnTextInput.Add(this, &BaseGameSettingsDialog::m_OnEditTextInput);
    g_gameWindow->OnKeyRepeat.Add(this, &BaseGameSettingsDialog::m_OnEditKeyRepeat);

    m_isInitialized = true;

    return true;
}

bool BaseGameSettingsDialog::IsSelectionOnPressable()
{
    if (static_cast<size_t>(m_currentSetting) >= m_tabs[m_currentTab]->settings.size())
        return false;

    auto currentSetting = m_tabs[m_currentTab]->settings.at(m_currentSetting).get();
    return currentSetting->type == SettingType::Button || currentSetting->type == SettingType::Boolean;
}

BaseGameSettingsDialog::Setting BaseGameSettingsDialog::CreateFloatSetting(GameConfigKeys key, String name, Vector2 range, float mult)
{
    Setting s = std::make_unique<SettingData>(name, SettingType::Floating);

    auto getter = [key](SettingData& data) {
        data.floatSetting.val = g_gameConfig.GetFloat(key);
    };

    auto setter = [key](const SettingData& data) {
        g_gameConfig.Set(key, data.floatSetting.val);
    };

    s->floatSetting.val = g_gameConfig.GetFloat(key);
    s->floatSetting.min = range.x;
    s->floatSetting.max = range.y;
    s->floatSetting.mult = mult;
    s->setter.AddLambda(std::move(setter));
    s->getter.AddLambda(std::move(getter));
    return s;
}

BaseGameSettingsDialog::Setting BaseGameSettingsDialog::CreateBoolSetting(String label, bool& val)
{
    Setting s = std::make_unique<SettingData>(label, SettingType::Boolean);
    
    auto getter = [&val](SettingData& data) {
        data.boolSetting.val = val;
    };

    auto setter = [&val](const SettingData& data) {
        val = data.boolSetting.val;
    };

    s->boolSetting.val = val;
    s->setter.AddLambda(std::move(setter));
    s->getter.AddLambda(std::move(getter));

    return s;
}

BaseGameSettingsDialog::Setting BaseGameSettingsDialog::CreateIntSetting(String label, int& val, Vector2i range, int step)
{
    Setting s = std::make_unique<SettingData>(label, SettingType::Integer);

    auto getter = [&val](SettingData& data) {
        data.intSetting.val = val;
    };

    auto setter = [&val](const SettingData& data) {
        val = data.intSetting.val;
    };

    s->intSetting.min = range.x;
    s->intSetting.max = range.y;
    s->intSetting.val = val;
    s->intSetting.step = step;

    s->setter.AddLambda(std::move(setter));
    s->getter.AddLambda(std::move(getter));

    return s;
}

BaseGameSettingsDialog::Setting BaseGameSettingsDialog::CreateButton(String label, std::function<void(const BaseGameSettingsDialog::SettingData &)>&& callback)
{
    Setting s = std::make_unique<SettingData>(label, SettingType::Button);
    s->setter.AddLambda(std::move(callback));

    return s;
}

BaseGameSettingsDialog::Setting BaseGameSettingsDialog::CreateIntSetting(GameConfigKeys key, String name, Vector2i range, int step)
{
    Setting s = std::make_unique<SettingData>(name, SettingType::Integer);

    auto getter = [key](SettingData& data) {
        data.intSetting.val = g_gameConfig.GetInt(key);
    };

    auto setter = [key](const SettingData& data) {
        g_gameConfig.Set(key, data.intSetting.val);
    };

    s->intSetting.val = g_gameConfig.GetInt(key);
    s->intSetting.min = range.x;
    s->intSetting.max = range.y;
    s->intSetting.step = step;
    s->setter.AddLambda(std::move(setter));
    s->getter.AddLambda(std::move(getter));
    return s;
}

BaseGameSettingsDialog::Setting BaseGameSettingsDialog::CreateBoolSetting(GameConfigKeys key, String name)
{
    Setting s = std::make_unique<SettingData>(name, SettingType::Boolean);

    auto getter = [key](SettingData& data) {
        data.boolSetting.val = g_gameConfig.GetBool(key);
    };

    auto setter = [key](const SettingData& data) {
        g_gameConfig.Set(key, data.boolSetting.val);
    };

    s->boolSetting.val = g_gameConfig.GetBool(key);
    s->setter.AddLambda(std::move(setter));
    s->getter.AddLambda(std::move(getter));
    return s;
}


void pushStringToTable(lua_State* lua, const char* name, const String& data) {
    lua_pushstring(lua, name);
    lua_pushstring(lua, data.c_str());
    lua_settable(lua, -3);
};

void pushIntToTable(lua_State* lua, const char* name, int data) {
    lua_pushstring(lua, name);
    lua_pushinteger(lua, data);
    lua_settable(lua, -3);
};

void pushBoolToTable(lua_State* lua, const char* name, bool data) {
    lua_pushstring(lua, name);
    lua_pushboolean(lua, data);
    lua_settable(lua, -3);
};

void pushFloatToTable(lua_State* lua, const char* name, float data) {
    lua_pushstring(lua, name);
    lua_pushnumber(lua, data);
    lua_settable(lua, -3);
};

void BaseGameSettingsDialog::m_SetTables()
{
    assert(m_lua);

    lua_newtable(m_lua);
    {
        lua_pushstring(m_lua, "tabs");
        lua_newtable(m_lua);
        {
            int tabCounter = 0;
            for (auto& tab : m_tabs)
            {
                lua_pushinteger(m_lua, ++tabCounter);
                tab->SetLua(m_lua, m_editingSetting);
                lua_settable(m_lua, -3);
            }
            lua_settable(m_lua, -3);
        }

        pushIntToTable(m_lua, "currentTab", m_currentTab + 1);
        pushIntToTable(m_lua, "currentSetting", m_currentSetting + 1);

        pushFloatToTable(m_lua, "posX", m_pos.x);
        pushFloatToTable(m_lua, "posY", m_pos.y);
    }
    lua_setglobal(m_lua, "SettingsDiag");
}

void BaseGameSettingsDialog::m_OnButtonPressed(Input::Button button, int32 delta)
{
    if (!m_active || m_closing)
        return;

    if (m_editingSetting)
        return; // typed entry has its own Enter/Escape handling in m_OnKeyPressed

    switch (button)
    {
    case Input::Button::BT_S:
        m_PressSetting();
        break;
    case Input::Button::FX_0:
        if (g_input.GetButton(Input::Button::FX_1))
            Close();
        break;
    case Input::Button::FX_1:
        if (g_input.GetButton(Input::Button::FX_0))
            Close();
        break;
    case Input::Button::Back:
        Close();
        break;
    default:
        break;
    }
}

void BaseGameSettingsDialog::m_OnButtonReleased(Input::Button button, int32 delta)
{
    if (!m_active || m_closing || !m_enableFXInputs)
        return;

    if (m_editingSetting)
        return;

    switch (button)
    {
    case Input::Button::FX_0:
        m_AdvanceTab(-1);
        break;
    case Input::Button::FX_1:
        m_AdvanceTab(1);
        break;
    case Input::Button::BT_0:
        m_ChangeStepSetting(-5);
        break;
    case Input::Button::BT_1:
        m_ChangeStepSetting(-1);
        break;
    case Input::Button::BT_2:
        m_ChangeStepSetting(1);
        break;
    case Input::Button::BT_3:
        m_ChangeStepSetting(5);
        break;
    default:
        break;
    }
}

inline static void AdvanceLooping(int& value, int steps, int size)
{
    value = (steps + value) % size;
    if (value < 0)
    {
        value = size + value;
    }
}

void BaseGameSettingsDialog::m_AdvanceSelection(int steps)
{
    if (m_tabs[m_currentTab]->settings.size() == 0)
        m_currentSetting = 0;
    else
        AdvanceLooping(m_currentSetting, steps, m_tabs[m_currentTab]->settings.size());

    if (steps != 0)
        m_knobAdvance[1] = 0.0f;
}

void BaseGameSettingsDialog::m_NavigateColumn(int steps)
{
    m_ChangeStepSetting(steps);
}
void BaseGameSettingsDialog::m_AdvanceTab(int steps)
{
    if (steps == 0)
        return;

    m_currentSetting = 0;
    AdvanceLooping(m_currentTab, steps, m_tabs.size());
    
    OnAdvanceTab();
}

void BaseGameSettingsDialog::m_ChangeStepSetting(int steps)
{
    if (steps == 0)
        return;

    if (static_cast<size_t>(m_currentSetting) >= m_tabs[m_currentTab]->settings.size())
        return;

    auto currentSetting = m_tabs[m_currentTab]->settings.at(m_currentSetting).get();

    switch (currentSetting->type)
    {
    case SettingType::Button:
        // Do not call a setter for buttons
        return;
    case SettingType::Integer:
        currentSetting->intSetting.val = Math::Clamp(currentSetting->intSetting.val + steps * currentSetting->intSetting.step,
            currentSetting->intSetting.min,
            currentSetting->intSetting.max);
        break;
    case SettingType::Boolean:
        currentSetting->boolSetting.val = !currentSetting->boolSetting.val;
        break;
    case SettingType::Enum:
    {
        int size = currentSetting->enumSetting.options.size();
        AdvanceLooping(currentSetting->enumSetting.val, steps, size);
        String& newVal = currentSetting->enumSetting.options.at(currentSetting->enumSetting.val);
        break;
    }
    case SettingType::Floating:
    default:
        break;
    }

    currentSetting->setter.Call(*currentSetting);
}

void BaseGameSettingsDialog::m_PressSetting()
{
    if (static_cast<size_t>(m_currentSetting) >= m_tabs[m_currentTab]->settings.size())
        return;

    auto currentSetting = m_tabs[m_currentTab]->settings.at(m_currentSetting).get();

    if (currentSetting->invalid)
        return; // e.g. a drill row whose in/out points are invalid - can't be selected

    switch (currentSetting->type)
    {
    case SettingType::Button:
        break;
    case SettingType::Boolean:
        currentSetting->boolSetting.val = !currentSetting->boolSetting.val;
        break;
    default:
        // Do not call a setter for non-pressables (Integer/String editing is
        // started by Enter, via m_OnEnterPressed, not Select/BT_S)
        return;
    }

    currentSetting->setter.Call(*currentSetting);
}

void BaseGameSettingsDialog::m_OnEnterPressed()
{
    if (static_cast<size_t>(m_currentSetting) >= m_tabs[m_currentTab]->settings.size())
        return;

    auto currentSetting = m_tabs[m_currentTab]->settings.at(m_currentSetting).get();

    // Typing needs a keyboard - a controller-only player can't Escape back out.
    if (currentSetting->type == SettingType::Integer || currentSetting->type == SettingType::String)
    {
        if (g_gameConfig.GetEnum<Enum_InputDevice>(GameConfigKeys::ButtonInputDevice) == InputDevice::Keyboard)
            m_StartEditingValue(currentSetting);
        return;
    }

    // Everything else (buttons, booleans) - Enter acts the same as Select/BT_S
    m_PressSetting();
}

void BaseGameSettingsDialog::m_StartEditingValue(SettingData* setting, bool startEmpty)
{
    m_editingSetting = setting;

    if (setting->type == SettingType::String)
    {
        m_editOriginalString = setting->stringSetting.val;
        m_editBuffer = startEmpty ? "" : setting->stringSetting.val;
    }
    else
    {
        m_editOriginalValue = setting->intSetting.val;
        m_editBuffer = startEmpty ? "" : Utility::Sprintf("%d", setting->intSetting.val);
    }
}

void BaseGameSettingsDialog::m_StopEditingValue()
{
    // Text input itself stays on for the dialog's whole lifetime (see Init) so a
    // fresh keystroke on the next selected row can immediately start editing it.
    m_editingSetting = nullptr;
    m_editBuffer.clear();
}

void BaseGameSettingsDialog::m_CommitEditingValue()
{
    if (!m_editingSetting)
        return;

    SettingData* setting = m_editingSetting;

    if (setting->type == SettingType::String)
    {
        setting->stringSetting.val = m_editBuffer;
    }
    else
    {
        const int parsed = m_editBuffer.empty() ? 0 : atoi(*m_editBuffer);
        setting->intSetting.val = Math::Clamp(parsed, setting->intSetting.min, setting->intSetting.max);
    }

    m_StopEditingValue();
    setting->setter.Call(*setting);
}

void BaseGameSettingsDialog::m_CancelEditingValue()
{
    if (m_editingSetting)
    {
        if (m_editingSetting->type == SettingType::String)
            m_editingSetting->stringSetting.val = m_editOriginalString;
        else
            m_editingSetting->intSetting.val = m_editOriginalValue;
    }

    m_StopEditingValue();
}

void BaseGameSettingsDialog::m_OnEditTextInput(const String& text)
{
    if (!m_active || m_closing)
        return;

    SettingData* targetSetting = m_editingSetting;
    if (!targetSetting)
    {
        // Typing on a selected Integer/String row starts an overwrite edit (Enter
        // instead preloads the current value - see m_OnEnterPressed).
        if (static_cast<size_t>(m_currentSetting) >= m_tabs[m_currentTab]->settings.size())
            return;

        SettingData* currentSetting = m_tabs[m_currentTab]->settings.at(m_currentSetting).get();
        if (currentSetting->type != SettingType::Integer && currentSetting->type != SettingType::String)
            return;
        if (g_gameConfig.GetEnum<Enum_InputDevice>(GameConfigKeys::ButtonInputDevice) != InputDevice::Keyboard)
            return;

        targetSetting = currentSetting;
    }

    const bool isString = targetSetting->type == SettingType::String;
    const size_t maxLength = isString ? targetSetting->stringSetting.maxLength : 9;

    // Filter first, then length-check the whole chunk - checking length per-byte
    // could split a multi-byte UTF-8 character across the limit.
    String filtered;
    for (char c : text)
    {
        // char is signed here, so a UTF-8 continuation/lead byte (>= 0x80) reads as
        // negative and fails "c >= 32" - compare unsigned so non-ASCII isn't dropped.
        const unsigned char uc = static_cast<unsigned char>(c);
        if (isString ? (uc >= 32 && uc != 127) : (c >= '0' && c <= '9'))
            filtered += c;
    }

    // e.g. a letter typed into a number field - leave it untouched rather than
    // starting an edit with an empty buffer that would commit as 0.
    if (filtered.empty())
        return;

    if (!m_editingSetting)
        m_StartEditingValue(targetSetting, /*startEmpty=*/true);

    if (m_editBuffer.length() + filtered.length() <= maxLength)
        m_editBuffer += filtered;

    if (isString)
        m_editingSetting->stringSetting.val = m_editBuffer;
    else
    {
        // Empty buffer (e.g. backspaced to nothing) previews the field's min, not 0.
        const int parsed = m_editBuffer.empty() ? m_editingSetting->intSetting.min : atoi(*m_editBuffer);
        m_editingSetting->intSetting.val = Math::Clamp(parsed, m_editingSetting->intSetting.min, m_editingSetting->intSetting.max);
    }
}

void BaseGameSettingsDialog::m_OnEditKeyRepeat(SDL_Scancode code)
{
    if (!m_editingSetting || code != SDL_SCANCODE_BACKSPACE || m_editBuffer.empty())
        return;

    m_editBuffer = m_editBuffer.substr(0, m_editBuffer.length() - 1);

    if (m_editingSetting->type == SettingType::String)
        m_editingSetting->stringSetting.val = m_editBuffer;
    else
        m_editingSetting->intSetting.val = m_editBuffer.empty() ? 0 : atoi(*m_editBuffer);
}

void BaseGameSettingsDialog::m_OnKeyPressed(SDL_Scancode code, int32 delta)
{
    if (!m_active || m_closing)
        return;

    if (m_editingSetting)
    {
        if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
            m_CommitEditingValue();
        else if (code == SDL_SCANCODE_ESCAPE)
            m_CancelEditingValue();
        // Digits go through m_OnEditTextInput, backspace through m_OnEditKeyRepeat -
        // everything else (including keyboard '1', which is also BT_S) is swallowed.
        return;
    }

    switch (code)
    {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        m_OnEnterPressed();
        break;
    case SDL_SCANCODE_LEFT:
        m_NavigateColumn(-1);
        break;
    case SDL_SCANCODE_RIGHT:
        m_NavigateColumn(1);
        break;
    case SDL_SCANCODE_TAB:
        if ((g_gameWindow->GetModifierKeys() & ModifierKeys::Shift) == ModifierKeys::Shift)
            m_AdvanceTab(-1);
        else
            m_AdvanceTab(1);
        break;
    case SDL_SCANCODE_UP:
        m_AdvanceSelection(-1);
        break;
    case SDL_SCANCODE_DOWN:
        m_AdvanceSelection(1);
        break;
    case SDL_SCANCODE_DELETE:
        OnDeleteKeyPressed();
        break;
    case SDL_SCANCODE_Z:
        if ((g_gameWindow->GetModifierKeys() & ModifierKeys::Ctrl) == ModifierKeys::Ctrl)
            OnUndoPressed();
        break;
    case SDL_SCANCODE_Y:
        if ((g_gameWindow->GetModifierKeys() & ModifierKeys::Ctrl) == ModifierKeys::Ctrl)
            OnRedoPressed();
        break;
    default:
        break;
    }
}

void BaseGameSettingsDialog::TabData::SetLua(lua_State* lua, const SettingData* editingSetting)
{
    lua_newtable(lua);
    pushStringToTable(lua, "name", name);
    lua_pushstring(lua, "settings");
    lua_newtable(lua);
    {
        int settingCounter = 0;
        for (auto& setting : settings)
        {
            lua_pushinteger(lua, ++settingCounter);
            lua_newtable(lua);
            pushStringToTable(lua, "name", setting->name);

            const bool isEditing = setting.get() == editingSetting;
            pushBoolToTable(lua, "isEditing", isEditing);
            pushBoolToTable(lua, "invalid", setting->invalid);
            pushBoolToTable(lua, "armed", setting->armed);
            pushIntToTable(lua, "trackingId", setting->trackingId);

            // Also get settings values in case they've been changed elsewhere - except
            // the row currently being typed into, whose getter would otherwise
            // overwrite the in-progress value with the last committed one every frame.
            if (!isEditing)
                setting->getter.Call(*setting);

            switch (setting->type)
            {
            case SettingType::Integer:
                if (setting->intSetting.div > 1)
                {
                    pushStringToTable(lua, "type", "float");
                    pushFloatToTable(lua, "value", setting->intSetting.val / (float) setting->intSetting.div);
                    pushIntToTable(lua, "min", setting->intSetting.min / (float)setting->intSetting.div);
                    pushIntToTable(lua, "max", setting->intSetting.max / (float)setting->intSetting.div);
                }
                else
                {
                    pushStringToTable(lua, "type", "int");
                    pushIntToTable(lua, "value", setting->intSetting.val);
                    pushIntToTable(lua, "min", setting->intSetting.min);
                    pushIntToTable(lua, "max", setting->intSetting.max);
                }
                break;
            case SettingType::Floating:
                pushStringToTable(lua, "type", "float");
                pushFloatToTable(lua, "value", setting->floatSetting.val);
                pushIntToTable(lua, "min", setting->floatSetting.min);
                pushIntToTable(lua, "max", setting->floatSetting.max);
                break;
            case SettingType::Boolean:
                pushStringToTable(lua, "type", "toggle");
                pushBoolToTable(lua, "value", setting->boolSetting.val);
                break;
            case SettingType::Enum:
            {
                pushStringToTable(lua, "type", "enum");
                pushIntToTable(lua, "value", setting->enumSetting.val + 1);
                lua_pushstring(lua, "options");
                lua_newtable(lua);
                int optionCounter = 0;
                for (auto&& o : setting->enumSetting.options)
                {
                    lua_pushinteger(lua, ++optionCounter);
                    lua_pushstring(lua, *o);
                    lua_settable(lua, -3);
                }
                lua_settable(lua, -3);
            }
                break;
            case SettingType::Button:
                pushStringToTable(lua, "type", "button");
                break;
            case SettingType::String:
                pushStringToTable(lua, "type", "string");
                pushStringToTable(lua, "value", setting->stringSetting.val);
                break;
            }
            lua_settable(lua, -3);
        }
        lua_settable(lua, -3);
    }
}
