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
CKBOOL WiiInputManager::IsKeyDown(CKDWORD iKey, CKDWORD *oStamp) {
#ifdef WII
    u32 held = WPAD_ButtonsHeld(WPAD_CHAN_0);

    if (iKey == KEY_LSHIFT && (held & WPAD_BUTTON_LEFT)) return TRUE;
    if (iKey == KEY_SPACE  && (held & WPAD_BUTTON_RIGHT)) return TRUE;
    if (iKey == KEY_ESCAPE && (held & WPAD_BUTTON_PLUS)) return TRUE;
#endif
    return FALSE;
}
CKBOOL WiiInputManager::IsKeyUp(CKDWORD iKey) { return TRUE; }
CKBOOL WiiInputManager::IsKeyToggled(CKDWORD iKey, CKDWORD *oStamp) {
#ifdef WII
    u32 pressed = WPAD_ButtonsDown(WPAD_CHAN_0);

    if (iKey == KEY_LSHIFT && (pressed & WPAD_BUTTON_LEFT)) return TRUE;
    if (iKey == KEY_SPACE  && (pressed & WPAD_BUTTON_RIGHT)) return TRUE;
    if (iKey == KEY_ESCAPE && (pressed & WPAD_BUTTON_PLUS)) return TRUE;
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
        oPosition.x = (data->ir.x / 1024.0f) * 640.0f;
        oPosition.y = (data->ir.y / 768.0f) * 480.0f;
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
