#include "WiiInputManager.h"
#include <math.h>

#ifdef WII
#include <ogc/machine/processor.h>
#include <wiiuse/wpad.h>

void SetupWiiFPU() {
    register double fpscr;
    // Read current FPSCR
    __asm__("mffs %0" : "=f"(fpscr));

    // Set the NI (Non-IEEE) bit to 1 to enable Flush-to-Zero for denormals
    union { double f; uint64_t i; } u;
    u.f = fpscr;
    u.i |= (1ULL << 2);

    // Write back to FPSCR
    __asm__("mtfsf 255, %0" : : "f"(u.f));
}
#endif

CKERROR CreateWiiInputManager(CKContext* context, CKBaseManager** res) {
    *res = new WiiInputManager(context);
    return CK_OK;
}

CKERROR RemoveWiiInputManager(CKContext* context, CKBaseManager* res) {
    delete (WiiInputManager*)res;
    return CK_OK;
}

WiiInputManager::WiiInputManager(CKContext* Context) : CKInputManager(Context, "WiiInputManager") {
    m_CursorVisible = FALSE;
}

WiiInputManager::~WiiInputManager() {}

CKERROR WiiInputManager::OnCKInit() {
#ifdef WII
    SetupWiiFPU();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
#endif
    return CK_OK;
}

CKERROR WiiInputManager::OnCKEnd() {
#ifdef WII
    WPAD_Shutdown();
#endif
    return CK_OK;
}

CKERROR WiiInputManager::PreProcess() {
#ifdef WII
    WPAD_ScanPads();
#endif
    return CK_OK;
}

void WiiInputManager::EnableKeyboardRepetition(CKBOOL iEnable) {}
CKBOOL WiiInputManager::IsKeyboardRepetitionEnabled() { return FALSE; }
// --- Simple Configurable Input Mapping Array ---
// This is a minimal abstraction to map Wii buttons to Virtools KEY constants.
// For a full production system, this could read from a settings file.
#ifdef WII
struct WiimoteKeyMap {
    CKDWORD vkKey;
    u32 wpadButton;
};

static const WiimoteKeyMap g_KeyMap[] = {
    {KEY_LSHIFT, WPAD_BUTTON_LEFT | WPAD_NUNCHUK_BUTTON_C | WPAD_CLASSIC_BUTTON_Y},  // Ball change / Action
    {KEY_SPACE,  WPAD_BUTTON_RIGHT | WPAD_NUNCHUK_BUTTON_Z | WPAD_CLASSIC_BUTTON_A}, // Jump
    {KEY_ESCAPE, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS},                       // Pause / Menu
    {KEY_UP,     WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP},
    {KEY_DOWN,   WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN},
    {KEY_RETURN, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A}                              // Select
};
static const int g_NumKeys = sizeof(g_KeyMap) / sizeof(WiimoteKeyMap);
#endif

CKBOOL WiiInputManager::IsKeyDown(CKDWORD iKey, CKDWORD *oStamp) {
#ifdef WII
    u32 held = WPAD_ButtonsHeld(WPAD_CHAN_0);

    for (int i = 0; i < g_NumKeys; i++) {
        if (g_KeyMap[i].vkKey == iKey && (held & g_KeyMap[i].wpadButton)) {
            return TRUE;
        }
    }
#endif
    return FALSE;
}
CKBOOL WiiInputManager::IsKeyUp(CKDWORD iKey) { return TRUE; }
CKBOOL WiiInputManager::IsKeyToggled(CKDWORD iKey, CKDWORD *oStamp) {
#ifdef WII
    u32 pressed = WPAD_ButtonsDown(WPAD_CHAN_0);

    for (int i = 0; i < g_NumKeys; i++) {
        if (g_KeyMap[i].vkKey == iKey && (pressed & g_KeyMap[i].wpadButton)) {
            return TRUE;
        }
    }
#endif
    return FALSE;
}
int WiiInputManager::GetKeyName(CKDWORD iKey, char *oKeyName) { if(oKeyName) oKeyName[0]=0; return 0; }
CKDWORD WiiInputManager::GetKeyFromName(CKSTRING iKeyName) { return 0; }
unsigned char *WiiInputManager::GetKeyboardState() { static unsigned char state[256]={0}; return state; }
CKBOOL WiiInputManager::IsKeyboardAttached() { return FALSE; }
int WiiInputManager::GetNumberOfKeyInBuffer() { return 0; }
int WiiInputManager::GetKeyFromBuffer(int i, CKDWORD &oKey, CKDWORD *oTimeStamp) { return 0; }

CKBOOL WiiInputManager::IsMouseButtonDown(CK_MOUSEBUTTON iButton) {
#ifdef WII
    if (iButton == CK_MOUSEBUTTON_LEFT) {
        return (WPAD_ButtonsHeld(WPAD_CHAN_0) & WPAD_BUTTON_A) ? TRUE : FALSE;
    }
#endif
    return FALSE;
}
CKBOOL WiiInputManager::IsMouseClicked(CK_MOUSEBUTTON iButton) {
#ifdef WII
    if (iButton == CK_MOUSEBUTTON_LEFT) {
        return (WPAD_ButtonsDown(WPAD_CHAN_0) & WPAD_BUTTON_A) ? TRUE : FALSE;
    }
#endif
    return FALSE;
}
CKBOOL WiiInputManager::IsMouseToggled(CK_MOUSEBUTTON iButton) { return FALSE; }
void WiiInputManager::GetMouseButtonsState(CKBYTE oStates[4]) { for(int i=0;i<4;++i) oStates[i]=0; }
void WiiInputManager::GetMousePosition(Vx2DVector &oPosition, CKBOOL iAbsolute) {
    oPosition.Set(0,0);
#ifdef WII
    WPADData* data = WPAD_Data(WPAD_CHAN_0);
    // Check if the IR camera sees the sensor bar
    if (data->ir.valid) {
        // Map the raw IR coordinates (usually 1024x768 internal) to standard 640x480 screen space
        float mapX = (data->ir.x / 1024.0f) * 640.0f;
        float mapY = (data->ir.y / 768.0f) * 480.0f;

        // Clamp bounds to prevent cursor escaping screen space (which could cause UI issues)
        if (mapX < 0.0f) mapX = 0.0f;
        if (mapX > 640.0f) mapX = 640.0f;
        if (mapY < 0.0f) mapY = 0.0f;
        if (mapY > 480.0f) mapY = 480.0f;

        oPosition.x = mapX;
        oPosition.y = mapY;
    }
#endif
}
void WiiInputManager::GetMouseRelativePosition(VxVector &oPosition) { oPosition.Set(0,0,0); }
CKBOOL WiiInputManager::IsMouseAttached() { return FALSE; }

CKBOOL WiiInputManager::IsJoystickAttached(int iJoystick) { return iJoystick < 4 ? TRUE : FALSE; }

void WiiInputManager::GetJoystickPosition(int iJoystick, VxVector *oPosition) {
    if(!oPosition) return;
    oPosition->Set(0,0,0);

#ifdef WII
    if (iJoystick == 0) {
        WPADData* data = WPAD_Data(WPAD_CHAN_0);
        if (data->exp.type == WPAD_EXP_NUNCHUK) {
            float nX = data->exp.nunchuk.js.mag * sinf(data->exp.nunchuk.js.ang * (3.14159f / 180.0f));
            float nY = data->exp.nunchuk.js.mag * cosf(data->exp.nunchuk.js.ang * (3.14159f / 180.0f));
            oPosition->x = nX;
            oPosition->y = nY;
        } else if (data->exp.type == WPAD_EXP_CLASSIC) {
            float cX = data->exp.classic.ljs.mag * sinf(data->exp.classic.ljs.ang * (3.14159f / 180.0f));
            float cY = data->exp.classic.ljs.mag * cosf(data->exp.classic.ljs.ang * (3.14159f / 180.0f));
            oPosition->x = cX;
            oPosition->y = cY;
        } else {
            float tiltX = data->orient.roll / 25.0f;
            float tiltY = data->orient.pitch / 25.0f;
            if (tiltX > 1.0f) tiltX = 1.0f;
            if (tiltX < -1.0f) tiltX = -1.0f;
            if (tiltY > 1.0f) tiltY = 1.0f;
            if (tiltY < -1.0f) tiltY = -1.0f;
            if (fabsf(tiltX) < 0.15f) tiltX = 0.0f;
            if (fabsf(tiltY) < 0.15f) tiltY = 0.0f;
            oPosition->x = tiltX;
            oPosition->y = tiltY;
        }
    }
#endif
}

void WiiInputManager::GetJoystickRotation(int iJoystick, VxVector *oRotation) {
    if(!oRotation) return;
    oRotation->Set(0,0,0);

#ifdef WII
    if (iJoystick == 0) {
        WPADData* data = WPAD_Data(WPAD_CHAN_0);
        // Map Wiimote tilt (accelerometer pitch/roll)
        oRotation->x = data->orient.pitch;
        oRotation->y = data->orient.roll;
        oRotation->z = data->orient.yaw;
    }
#endif
}

void WiiInputManager::GetJoystickSliders(int iJoystick, Vx2DVector *oPosition) { if(oPosition) oPosition->Set(0,0); }
void WiiInputManager::GetJoystickPointOfViewAngle(int iJoystick, float *oAngle) { if(oAngle) *oAngle = 0; }

CKDWORD WiiInputManager::GetJoystickButtonsState(int iJoystick) {
    CKDWORD state = 0;
#ifdef WII
    if (iJoystick < 4) {
        state = WPAD_ButtonsHeld(iJoystick);
    }
#endif
    return state;
}

CKBOOL WiiInputManager::IsJoystickButtonDown(int iJoystick, int iButton) { return FALSE; }

void WiiInputManager::Pause(CKBOOL pause) {}
void WiiInputManager::ShowCursor(CKBOOL iShow) { m_CursorVisible = iShow; }
CKBOOL WiiInputManager::GetCursorVisibility() { return m_CursorVisible; }
VXCURSOR_POINTER WiiInputManager::GetSystemCursor() { return NULL; }
void WiiInputManager::SetSystemCursor(VXCURSOR_POINTER cursor) {}
