#include "WiiInputManager.h"

#include "WiiSystem.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiikeyboard/keyboard.h>
#include <wiiuse/wpad.h>

#include <math.h>
#include <string.h>
#include <strings.h>

// DirectInput scan codes the game reads.
enum
{
    DIK_ESCAPE = 0x01,
    DIK_RETURN = 0x1C,
    DIK_LSHIFT = 0x2A,
    DIK_SPACE = 0x39,
    DIK_UP = 0xC8,
    DIK_LEFT = 0xCB,
    DIK_RIGHT = 0xCD,
    DIK_DOWN = 0xD0,
};

namespace
{
    const float kStickThreshold = 0.5f;

    CKDWORD Milliseconds()
    {
        return (CKDWORD)ticks_to_millisecs(gettime());
    }

    // USB HID usage -> DirectInput scan code.
    CKDWORD UsbKeyToScanCode(u8 usage)
    {
        static const CKBYTE kLetters[26] = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,
        };
        static const CKBYTE kFunctionKeys[12] = {
            0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,
        };

        if (usage >= 0x04 && usage <= 0x1D)
            return kLetters[usage - 0x04];
        if (usage >= 0x1E && usage <= 0x27)
            return 0x02 + (usage - 0x1E); // 1..9, 0
        if (usage >= 0x3A && usage <= 0x45)
            return kFunctionKeys[usage - 0x3A];

        switch (usage)
        {
        case 0x28: return DIK_RETURN;
        case 0x29: return DIK_ESCAPE;
        case 0x2A: return 0x0E; // Backspace
        case 0x2B: return 0x0F; // Tab
        case 0x2C: return DIK_SPACE;
        case 0x2D: return 0x0C; // -
        case 0x2E: return 0x0D; // =
        case 0x2F: return 0x1A; // [
        case 0x30: return 0x1B; // ]
        case 0x31: return 0x2B; // backslash
        case 0x33: return 0x27; // ;
        case 0x34: return 0x28; // '
        case 0x35: return 0x29; // `
        case 0x36: return 0x33; // ,
        case 0x37: return 0x34; // .
        case 0x38: return 0x35; // /
        case 0x39: return 0x3A; // Caps Lock
        case 0x49: return 0xD2; // Insert
        case 0x4A: return 0xC7; // Home
        case 0x4B: return 0xC9; // Page Up
        case 0x4C: return 0xD3; // Delete
        case 0x4D: return 0xCF; // End
        case 0x4E: return 0xD1; // Page Down
        case 0x4F: return DIK_RIGHT;
        case 0x50: return DIK_LEFT;
        case 0x51: return DIK_DOWN;
        case 0x52: return DIK_UP;
        case 0xE0: return 0x1D; // Left Ctrl
        case 0xE1: return DIK_LSHIFT;
        case 0xE2: return 0x38; // Left Alt
        case 0xE4: return 0x9D; // Right Ctrl
        case 0xE5: return 0x36; // Right Shift
        case 0xE6: return 0xB8; // Right Alt
        default: return 0;
        }
    }

    // Arrow keys from an analog stick (x right, y up, both -1..1).
    void StickToArrows(float x, float y, CKBYTE wanted[WII_KEYBOARD_SIZE])
    {
        if (x > kStickThreshold)
            wanted[DIK_RIGHT] = 1;
        else if (x < -kStickThreshold)
            wanted[DIK_LEFT] = 1;
        if (y > kStickThreshold)
            wanted[DIK_UP] = 1;
        else if (y < -kStickThreshold)
            wanted[DIK_DOWN] = 1;
    }

    // wiiuse sticks report an angle (degrees clockwise from up) and a magnitude.
    void StickVector(const joystick_t &stick, float *x, float *y)
    {
        const float radians = stick.ang * (float)M_PI / 180.0f;
        *x = stick.mag * sinf(radians);
        *y = stick.mag * cosf(radians);
    }

    void Press(CKBYTE wanted[WII_KEYBOARD_SIZE], CKDWORD key, bool condition)
    {
        if (condition)
            wanted[key] = 1;
    }
}

WiiInputManager::WiiInputManager(CKContext *context) : CKInputManager(context, (CKSTRING)"Wii Input Manager")
{
    m_KeyboardRepetition = TRUE;
    m_RepeatDelay = 500;
    m_RepeatInterval = 33;
    m_UsbKeyboardReady = FALSE;
    m_UsbKeyboardAttached = FALSE;
    m_PointerOnScreen = FALSE;
    m_PointerChannel = -1;
    m_ControllersConnected = 0;
    m_Paused = FALSE;
    m_CursorVisible = TRUE;
    m_Cursor = VXCURSOR_NORMALSELECT;
    m_MousePosition.Set(320.0f, 240.0f);
    ClearState();

    context->RegisterNewManager(this);
}

WiiInputManager::~WiiInputManager()
{
}

void WiiInputManager::ClearState()
{
    memset(m_KeyboardState, 0, sizeof(m_KeyboardState));
    memset(m_KeyboardStamps, 0, sizeof(m_KeyboardStamps));
    memset(m_PadKeys, 0, sizeof(m_PadKeys));
    memset(m_UsbKeys, 0, sizeof(m_UsbKeys));
    memset(m_KeyBuffer, 0, sizeof(m_KeyBuffer));
    m_KeyBufferCount = 0;
    memset(m_MouseButtons, 0, sizeof(m_MouseButtons));
    memset(m_LastMouseButtons, 0, sizeof(m_LastMouseButtons));
    m_MouseDelta.Set(0.0f, 0.0f, 0.0f);
    memset(m_Joysticks, 0, sizeof(m_Joysticks));
}

// ---------------------------------------------------------------------------
// Manager callbacks

CKERROR WiiInputManager::OnCKInit()
{
    // WPAD and PAD are started by the system layer before the engine.
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, wiisystem::GetRenderWidth(), wiisystem::GetRenderHeight());
    // Let idle Wii Remotes turn themselves off after five minutes, like system software does.
    WPAD_SetIdleTimeout(300);

    m_UsbKeyboardReady = KEYBOARD_Init(NULL) >= 0;
    return CK_OK;
}

CKERROR WiiInputManager::OnCKEnd()
{
    if (m_UsbKeyboardReady)
    {
        KEYBOARD_Deinit();
        m_UsbKeyboardReady = FALSE;
    }
    wiisystem::SetPointer(false, 0.0f, 0.0f, 0.0f);
    return CK_OK;
}

CKERROR WiiInputManager::OnCKReset()
{
    m_CursorVisible = TRUE;
    ClearState();
    return CK_OK;
}

CKERROR WiiInputManager::OnCKPlay()
{
    // Keys held across a pause must be pressed again.
    memset(m_KeyboardState, 0, sizeof(m_KeyboardState));
    m_KeyBufferCount = 0;
    if (m_UsbKeyboardReady)
        KEYBOARD_FlushEvents();
    return CK_OK;
}

CKERROR WiiInputManager::PreProcess()
{
    const CKDWORD now = Milliseconds();

    m_KeyBufferCount = 0;
    memcpy(m_LastMouseButtons, m_MouseButtons, sizeof(m_MouseButtons));
    m_MouseDelta.Set(0.0f, 0.0f, 0.0f);

    WPAD_ScanPads();

    if (m_Paused)
    {
        memset(m_KeyboardState, 0, sizeof(m_KeyboardState));
        memset(m_PadKeys, 0, sizeof(m_PadKeys));
        return CK_OK;
    }

    PollUsbKeyboard(now);

    CKBYTE wanted[WII_KEYBOARD_SIZE];
    memset(wanted, 0, sizeof(wanted));
    PollControllers(wanted, now);

    for (CKDWORD key = 0; key < WII_KEYBOARD_SIZE; ++key)
    {
        const CKBOOL down = (m_KeyboardState[key] & KS_PRESSED) && !(m_KeyboardState[key] & KS_RELEASED);
        if (wanted[key] && !down)
            PressKey(key, now);
        else if (!wanted[key] && m_PadKeys[key] && !m_UsbKeys[key] && down)
            ReleaseKey(key, now);
        m_PadKeys[key] = wanted[key];
    }

    if (m_KeyboardRepetition)
        RepeatKeys(now);

    PollPointer();
    return CK_OK;
}

CKERROR WiiInputManager::PostProcess()
{
    for (int key = 0; key < WII_KEYBOARD_SIZE; ++key)
    {
        if (m_KeyboardState[key] & KS_RELEASED)
            m_KeyboardState[key] = KS_IDLE;
    }
    for (int button = 0; button < 4; ++button)
    {
        if (m_MouseButtons[button] & KS_RELEASED)
            m_MouseButtons[button] = KS_IDLE;
    }
    return CK_OK;
}

// ---------------------------------------------------------------------------
// Polling

void WiiInputManager::PressKey(CKDWORD key, CKDWORD now)
{
    m_KeyboardState[key] = KS_PRESSED;
    m_KeyboardStamps[key] = now;
    PushKeyEvent(key, 0x80, now);
}

void WiiInputManager::ReleaseKey(CKDWORD key, CKDWORD now)
{
    m_KeyboardState[key] |= KS_RELEASED;
    m_KeyboardStamps[key] = now - m_KeyboardStamps[key];
    PushKeyEvent(key, 0x00, now);
}

void WiiInputManager::PushKeyEvent(CKDWORD key, CKDWORD data, CKDWORD now)
{
    if (m_KeyBufferCount >= WII_KEY_BUFFER_SIZE)
        return;
    m_KeyBuffer[m_KeyBufferCount].Key = key;
    m_KeyBuffer[m_KeyBufferCount].Data = data;
    m_KeyBuffer[m_KeyBufferCount].TimeStamp = now;
    ++m_KeyBufferCount;
}

// Typing repeats held keys into the buffer, like the DirectInput manager.
void WiiInputManager::RepeatKeys(CKDWORD now)
{
    for (CKDWORD key = 0; key < WII_KEYBOARD_SIZE; ++key)
    {
        if (m_KeyboardState[key] != KS_PRESSED)
            continue;
        if ((int)m_KeyboardStamps[key] > 0 && now - m_KeyboardStamps[key] > m_RepeatDelay)
            m_KeyboardStamps[key] = (CKDWORD)(-(int)m_KeyboardStamps[key]);
        if ((int)m_KeyboardStamps[key] >= 0)
            continue;
        for (int t = -(int)m_KeyboardStamps[key] - (int)m_RepeatDelay + (int)now; t > (int)m_RepeatInterval;)
        {
            t -= m_RepeatInterval;
            m_KeyboardStamps[key] = (CKDWORD)((int)m_KeyboardStamps[key] - (int)m_RepeatInterval);
            PushKeyEvent(key, 0x80, (CKDWORD)(-(int)m_KeyboardStamps[key]));
        }
    }
}

void WiiInputManager::PollUsbKeyboard(CKDWORD now)
{
    if (!m_UsbKeyboardReady)
        return;

    keyboard_event event;
    while (KEYBOARD_GetEvent(&event) > 0)
    {
        switch (event.type)
        {
        case KEYBOARD_CONNECTED:
            m_UsbKeyboardAttached = TRUE;
            break;
        case KEYBOARD_DISCONNECTED:
            m_UsbKeyboardAttached = FALSE;
            for (CKDWORD key = 0; key < WII_KEYBOARD_SIZE; ++key)
            {
                if (m_UsbKeys[key] && !m_PadKeys[key])
                    ReleaseKey(key, now);
            }
            memset(m_UsbKeys, 0, sizeof(m_UsbKeys));
            break;
        case KEYBOARD_PRESSED:
        {
            const CKDWORD key = UsbKeyToScanCode(event.keycode);
            if (key && !m_UsbKeys[key])
            {
                m_UsbKeys[key] = 1;
                if (!(m_KeyboardState[key] & KS_PRESSED) || (m_KeyboardState[key] & KS_RELEASED))
                    PressKey(key, now);
            }
            break;
        }
        case KEYBOARD_RELEASED:
        {
            const CKDWORD key = UsbKeyToScanCode(event.keycode);
            if (key && m_UsbKeys[key])
            {
                m_UsbKeys[key] = 0;
                if (!m_PadKeys[key])
                    ReleaseKey(key, now);
            }
            break;
        }
        }
    }
}

void WiiInputManager::PollControllers(CKBYTE wanted[WII_KEYBOARD_SIZE], CKDWORD now)
{
    (void)now;
    int connected = 0;
    bool pointerClick = false;
    m_PointerChannel = -1;

    const u32 gamecubePads = PAD_ScanPads();

    for (int chan = 0; chan < WII_JOYSTICK_COUNT; ++chan)
    {
        Joystick &joystick = m_Joysticks[chan];
        memset(&joystick, 0, sizeof(joystick));

        u32 type = 0;
        if (WPAD_Probe(chan, &type) == WPAD_ERR_NONE)
        {
            WPADData *data = WPAD_Data(chan);
            const u32 held = WPAD_ButtonsHeld(chan);
            const u32 down = WPAD_ButtonsDown(chan);
            ++connected;

            if (down & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME))
                wiisystem::RequestHomeMenu();

            const bool pointing = data && data->ir.valid;
            if (pointing && m_PointerChannel < 0)
                m_PointerChannel = chan;

            const int expansion = data ? data->exp.type : WPAD_EXP_NONE;
            if (expansion == WPAD_EXP_NONE && !pointing)
            {
                // Held sideways: the D-Pad turns with the remote.
                Press(wanted, DIK_UP, held & WPAD_BUTTON_RIGHT);
                Press(wanted, DIK_DOWN, held & WPAD_BUTTON_LEFT);
                Press(wanted, DIK_LEFT, held & WPAD_BUTTON_UP);
                Press(wanted, DIK_RIGHT, held & WPAD_BUTTON_DOWN);
                Press(wanted, DIK_LSHIFT, held & (WPAD_BUTTON_1 | WPAD_BUTTON_B));
                Press(wanted, DIK_SPACE, held & WPAD_BUTTON_2);
                Press(wanted, DIK_RETURN, held & WPAD_BUTTON_A);
                Press(wanted, DIK_ESCAPE, held & (WPAD_BUTTON_PLUS | WPAD_BUTTON_MINUS));
            }
            else
            {
                // Upright (pointing, or with an expansion in the other hand).
                Press(wanted, DIK_UP, held & WPAD_BUTTON_UP);
                Press(wanted, DIK_DOWN, held & WPAD_BUTTON_DOWN);
                Press(wanted, DIK_LEFT, held & WPAD_BUTTON_LEFT);
                Press(wanted, DIK_RIGHT, held & WPAD_BUTTON_RIGHT);
                Press(wanted, DIK_LSHIFT, held & WPAD_BUTTON_1);
                Press(wanted, DIK_SPACE, held & WPAD_BUTTON_2);
                Press(wanted, DIK_ESCAPE, held & (WPAD_BUTTON_PLUS | WPAD_BUTTON_MINUS | WPAD_BUTTON_B));
                // A clicks what the pointer is on; off screen it confirms like Enter.
                if (pointing)
                    pointerClick = pointerClick || (held & WPAD_BUTTON_A);
                else
                    Press(wanted, DIK_RETURN, held & WPAD_BUTTON_A);
            }

            if (data && expansion == WPAD_EXP_NUNCHUK)
            {
                float x, y;
                StickVector(data->exp.nunchuk.js, &x, &y);
                StickToArrows(x, y, wanted);
                Press(wanted, DIK_LSHIFT, held & WPAD_NUNCHUK_BUTTON_Z);
                Press(wanted, DIK_SPACE, held & WPAD_NUNCHUK_BUTTON_C);

                joystick.Attached = TRUE;
                joystick.Position.Set(x, -y, 0.0f);
                joystick.Buttons = ((held & WPAD_NUNCHUK_BUTTON_Z) ? 1 : 0) | ((held & WPAD_NUNCHUK_BUTTON_C) ? 2 : 0);
            }
            else if (data && expansion == WPAD_EXP_CLASSIC)
            {
                float x, y;
                StickVector(data->exp.classic.ljs, &x, &y);
                StickToArrows(x, y, wanted);
                Press(wanted, DIK_UP, held & WPAD_CLASSIC_BUTTON_UP);
                Press(wanted, DIK_DOWN, held & WPAD_CLASSIC_BUTTON_DOWN);
                Press(wanted, DIK_LEFT, held & WPAD_CLASSIC_BUTTON_LEFT);
                Press(wanted, DIK_RIGHT, held & WPAD_CLASSIC_BUTTON_RIGHT);
                Press(wanted, DIK_RETURN, held & WPAD_CLASSIC_BUTTON_A);
                Press(wanted, DIK_ESCAPE, held & (WPAD_CLASSIC_BUTTON_B | WPAD_CLASSIC_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_MINUS));
                Press(wanted, DIK_LSHIFT, held & (WPAD_CLASSIC_BUTTON_Y | WPAD_CLASSIC_BUTTON_ZL | WPAD_CLASSIC_BUTTON_FULL_L));
                Press(wanted, DIK_SPACE, held & (WPAD_CLASSIC_BUTTON_X | WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_FULL_R));

                float rx, ry;
                StickVector(data->exp.classic.rjs, &rx, &ry);
                joystick.Attached = TRUE;
                joystick.Position.Set(x, -y, 0.0f);
                joystick.Rotation.Set(rx, -ry, 0.0f);
                joystick.Sliders.Set(data->exp.classic.l_shoulder, data->exp.classic.r_shoulder);
                joystick.Buttons = held >> 16;
            }
        }

        if (gamecubePads & (1u << chan))
        {
            const u32 held = PAD_ButtonsHeld(chan);
            ++connected;

            if (PAD_ButtonsDown(chan) & PAD_BUTTON_START && (held & PAD_TRIGGER_Z))
                wiisystem::RequestHomeMenu();

            const float x = PAD_StickX(chan) / 80.0f;
            const float y = PAD_StickY(chan) / 80.0f;
            StickToArrows(x, y, wanted);
            Press(wanted, DIK_UP, held & PAD_BUTTON_UP);
            Press(wanted, DIK_DOWN, held & PAD_BUTTON_DOWN);
            Press(wanted, DIK_LEFT, held & PAD_BUTTON_LEFT);
            Press(wanted, DIK_RIGHT, held & PAD_BUTTON_RIGHT);
            Press(wanted, DIK_RETURN, held & PAD_BUTTON_A);
            Press(wanted, DIK_ESCAPE, held & (PAD_BUTTON_B | PAD_BUTTON_START));
            Press(wanted, DIK_LSHIFT, held & (PAD_BUTTON_Y | PAD_TRIGGER_L | PAD_TRIGGER_Z));
            Press(wanted, DIK_SPACE, held & (PAD_BUTTON_X | PAD_TRIGGER_R));

            if (!joystick.Attached)
            {
                joystick.Attached = TRUE;
                joystick.Position.Set(x, -y, 0.0f);
                joystick.Rotation.Set(PAD_SubStickX(chan) / 80.0f, -PAD_SubStickY(chan) / 80.0f, 0.0f);
                joystick.Sliders.Set(PAD_TriggerL(chan) / 255.0f, PAD_TriggerR(chan) / 255.0f);
                joystick.Buttons = held;
            }
        }
    }

    // Losing the last controller mid-game opens the game's pause menu.
    if (connected == 0 && m_ControllersConnected > 0)
        wanted[DIK_ESCAPE] = 1;
    m_ControllersConnected = connected;

    // Left mouse button follows A on the pointing remote.
    if (pointerClick && !(m_MouseButtons[0] & KS_PRESSED))
        m_MouseButtons[0] = KS_PRESSED;
    else if (!pointerClick && (m_MouseButtons[0] & KS_PRESSED))
        m_MouseButtons[0] |= KS_RELEASED;
}

void WiiInputManager::PollPointer()
{
    float angle = 0.0f;
    m_PointerOnScreen = FALSE;
    if (m_PointerChannel >= 0)
    {
        WPADData *data = WPAD_Data(m_PointerChannel);
        if (data && data->ir.valid)
        {
            const float width = (float)wiisystem::GetRenderWidth();
            const float height = (float)wiisystem::GetRenderHeight();
            float x = data->ir.x;
            float y = data->ir.y;
            m_PointerOnScreen = x >= 0.0f && y >= 0.0f && x < width && y < height;
            if (x < 0.0f) x = 0.0f;
            if (y < 0.0f) y = 0.0f;
            if (x > width - 1.0f) x = width - 1.0f;
            if (y > height - 1.0f) y = height - 1.0f;

            m_MouseDelta.Set(x - m_MousePosition.x, y - m_MousePosition.y, 0.0f);
            m_MousePosition.Set(x, y);
            angle = data->ir.angle;
        }
    }
    wiisystem::SetPointer(m_CursorVisible && m_PointerOnScreen, m_MousePosition.x, m_MousePosition.y, angle);
}

// ---------------------------------------------------------------------------
// Keyboard

void WiiInputManager::EnableKeyboardRepetition(CKBOOL iEnable)
{
    m_KeyboardRepetition = iEnable;
}

CKBOOL WiiInputManager::IsKeyboardRepetitionEnabled()
{
    return m_KeyboardRepetition;
}

CKBOOL WiiInputManager::IsKeyDown(CKDWORD iKey, CKDWORD *oStamp)
{
    if (iKey >= WII_KEYBOARD_SIZE || !(m_KeyboardState[iKey] & KS_PRESSED))
        return FALSE;
    if (oStamp)
        *oStamp = m_KeyboardStamps[iKey];
    return TRUE;
}

CKBOOL WiiInputManager::IsKeyUp(CKDWORD iKey)
{
    return iKey < WII_KEYBOARD_SIZE && m_KeyboardState[iKey] == KS_IDLE;
}

CKBOOL WiiInputManager::IsKeyToggled(CKDWORD iKey, CKDWORD *oStamp)
{
    if (iKey >= WII_KEYBOARD_SIZE || !(m_KeyboardState[iKey] & KS_RELEASED))
        return FALSE;
    if (oStamp)
        *oStamp = m_KeyboardStamps[iKey];
    return TRUE;
}

int WiiInputManager::GetKeyName(CKDWORD iKey, char *oKeyName)
{
    return VxScanCodeToName(iKey, oKeyName);
}

CKDWORD WiiInputManager::GetKeyFromName(CKSTRING iKeyName)
{
    char keyName[32];
    CKDWORD key;
    for (key = 0; key < WII_KEYBOARD_SIZE; ++key)
    {
        if (GetKeyName(key, keyName) != 0 && strcasecmp(keyName, iKeyName) == 0)
            break;
    }
    return key;
}

unsigned char *WiiInputManager::GetKeyboardState()
{
    return m_KeyboardState;
}

CKBOOL WiiInputManager::IsKeyboardAttached()
{
    // Controllers always provide the keys the game needs.
    return TRUE;
}

int WiiInputManager::GetNumberOfKeyInBuffer()
{
    return m_KeyBufferCount;
}

int WiiInputManager::GetKeyFromBuffer(int i, CKDWORD &oKey, CKDWORD *oTimeStamp)
{
    if (i < 0 || i >= m_KeyBufferCount)
        return 0;
    oKey = m_KeyBuffer[i].Key;
    if (oTimeStamp)
        *oTimeStamp = m_KeyBuffer[i].TimeStamp;
    return (m_KeyBuffer[i].Data & 0x80) ? KS_PRESSED : KS_RELEASED;
}

// ---------------------------------------------------------------------------
// Mouse

static int MouseButtonIndex(CK_MOUSEBUTTON button)
{
    return (button >= CK_MOUSEBUTTON_LEFT && button <= CK_MOUSEBUTTON_4) ? (int)button : -1;
}

CKBOOL WiiInputManager::IsMouseButtonDown(CK_MOUSEBUTTON iButton)
{
    const int index = MouseButtonIndex(iButton);
    return index >= 0 && (m_MouseButtons[index] & KS_PRESSED) != 0;
}

CKBOOL WiiInputManager::IsMouseClicked(CK_MOUSEBUTTON iButton)
{
    const int index = MouseButtonIndex(iButton);
    return index >= 0 && (m_MouseButtons[index] & KS_PRESSED) && !(m_LastMouseButtons[index] & KS_PRESSED);
}

CKBOOL WiiInputManager::IsMouseToggled(CK_MOUSEBUTTON iButton)
{
    const int index = MouseButtonIndex(iButton);
    return index >= 0 && (m_MouseButtons[index] & KS_RELEASED) != 0;
}

void WiiInputManager::GetMouseButtonsState(CKBYTE oStates[4])
{
    memcpy(oStates, m_MouseButtons, sizeof(m_MouseButtons));
}

void WiiInputManager::GetMousePosition(Vx2DVector &oPosition, CKBOOL iAbsolute)
{
    // The render context covers the whole picture, so screen and window
    // coordinates are the same.
    (void)iAbsolute;
    oPosition = m_MousePosition;
}

void WiiInputManager::GetMouseRelativePosition(VxVector &oPosition)
{
    oPosition = m_MouseDelta;
}

CKBOOL WiiInputManager::IsMouseAttached()
{
    return TRUE;
}

// ---------------------------------------------------------------------------
// Joysticks

CKBOOL WiiInputManager::IsJoystickAttached(int iJoystick)
{
    return iJoystick >= 0 && iJoystick < WII_JOYSTICK_COUNT && m_Joysticks[iJoystick].Attached;
}

void WiiInputManager::GetJoystickPosition(int iJoystick, VxVector *oPosition)
{
    if (!oPosition)
        return;
    if (IsJoystickAttached(iJoystick))
        *oPosition = m_Joysticks[iJoystick].Position;
    else
        oPosition->Set(0.0f, 0.0f, 0.0f);
}

void WiiInputManager::GetJoystickRotation(int iJoystick, VxVector *oRotation)
{
    if (!oRotation)
        return;
    if (IsJoystickAttached(iJoystick))
        *oRotation = m_Joysticks[iJoystick].Rotation;
    else
        oRotation->Set(0.0f, 0.0f, 0.0f);
}

void WiiInputManager::GetJoystickSliders(int iJoystick, Vx2DVector *oPosition)
{
    if (!oPosition)
        return;
    if (IsJoystickAttached(iJoystick))
        *oPosition = m_Joysticks[iJoystick].Sliders;
    else
        oPosition->Set(0.0f, 0.0f);
}

void WiiInputManager::GetJoystickPointOfViewAngle(int iJoystick, float *oAngle)
{
    if (oAngle)
        *oAngle = IsJoystickAttached(iJoystick) ? m_Joysticks[iJoystick].PointOfView : -1.0f;
}

CKDWORD WiiInputManager::GetJoystickButtonsState(int iJoystick)
{
    return IsJoystickAttached(iJoystick) ? m_Joysticks[iJoystick].Buttons : 0;
}

CKBOOL WiiInputManager::IsJoystickButtonDown(int iJoystick, int iButton)
{
    if (iButton < 0 || iButton >= 32)
        return FALSE;
    return (GetJoystickButtonsState(iJoystick) & (1u << iButton)) != 0;
}

// ---------------------------------------------------------------------------
// Misc

void WiiInputManager::Pause(CKBOOL pause)
{
    m_Paused = pause;
    if (pause)
        ClearState();
}

void WiiInputManager::ShowCursor(CKBOOL iShow)
{
    m_CursorVisible = iShow;
}

CKBOOL WiiInputManager::GetCursorVisibility()
{
    return m_CursorVisible;
}

VXCURSOR_POINTER WiiInputManager::GetSystemCursor()
{
    return m_Cursor;
}

void WiiInputManager::SetSystemCursor(VXCURSOR_POINTER cursor)
{
    m_Cursor = cursor;
}

// ---------------------------------------------------------------------------
// Plugin entry points

#ifdef CK_LIB
#define CreateNewManager                CreateNewInputManager
#define RemoveManager                   RemoveInputManager
#define CKGetPluginInfoCount            CKGet_InputManager_PluginInfoCount
#define CKGetPluginInfo                 CKGet_InputManager_PluginInfo
#define g_PluginInfo                    g_InputManager_PluginInfo
#define CKInitializeParameterTypes      CKInputManagerInitializeParameterTypes
#define CKInitializeOperationTypes      CKInputManagerInitializeOperationTypes
#define CKInitializeOperationFunctions  CKInputManagerInitializeOperationFunctions
#define CKUnInitializeParameterTypes    CKInputManagerUnInitializeParameterTypes
#define CKUnInitializeOperationTypes    CKInputManagerUnInitializeOperationTypes
#endif

// Shared with the desktop input manager (Managers/SdlInputManager/Parameters.cpp).
void CKInitializeParameterTypes(CKContext *context);
void CKInitializeOperationTypes(CKContext *context);
void CKInitializeOperationFunctions(CKContext *context);
void CKUnInitializeParameterTypes(CKContext *context);
void CKUnInitializeOperationTypes(CKContext *context);

CKPluginInfo g_PluginInfo;

static CKERROR CreateNewManager(CKContext *context)
{
    CKInitializeParameterTypes(context);
    CKInitializeOperationTypes(context);
    CKInitializeOperationFunctions(context);
    new WiiInputManager(context);
    return CK_OK;
}

static CKERROR RemoveManager(CKContext *context)
{
    WiiInputManager *manager = (WiiInputManager *)context->GetManagerByGuid(INPUT_MANAGER_GUID);
    delete manager;
    CKUnInitializeParameterTypes(context);
    CKUnInitializeOperationTypes(context);
    return CK_OK;
}

PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo(int index)
{
    (void)index;
    g_PluginInfo.m_Author = (CKSTRING)"Ballanced";
    g_PluginInfo.m_Description = (CKSTRING)"Wii Remote, Classic Controller, GameCube and USB keyboard input";
    g_PluginInfo.m_Extension = (CKSTRING)"";
    g_PluginInfo.m_Type = CKPLUGIN_MANAGER_DLL;
    g_PluginInfo.m_Version = 0x000001;
    g_PluginInfo.m_InitInstanceFct = CreateNewManager;
    g_PluginInfo.m_ExitInstanceFct = RemoveManager;
    g_PluginInfo.m_GUID = INPUT_MANAGER_GUID;
    g_PluginInfo.m_Summary = (CKSTRING)"Wii Input Manager";
    return &g_PluginInfo;
}
