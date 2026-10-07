#ifndef WIIINPUTMANAGER_H
#define WIIINPUTMANAGER_H

#include "CKAll.h"

#include "WiiScreenKeyboard.h"

#define WII_KEYBOARD_SIZE 256
#define WII_KEY_BUFFER_SIZE 256
#define WII_JOYSTICK_COUNT 4

/**
 * Input manager for the Nintendo Wii.
 *
 * Ballance reads the keyboard (DirectInput scan codes) and the mouse, so the
 * Wii controllers are presented as a virtual keyboard and mouse:
 *
 *   Wii Remote held sideways   D-Pad = arrows, 1 = Shift (rotate camera),
 *                              2 = Space (overhead camera), + = Esc, A = Enter
 *   Wii Remote pointing        pointer = mouse, A = click while the game reads the
 *                              mouse buttons (Enter otherwise and off screen),
 *                              B = Esc, D-Pad = arrows
 *   Nunchuk                    stick = arrows, Z = Shift, C = Space
 *   Classic / GameCube pad     stick or D-Pad = arrows, A = Enter, B = Esc,
 *                              L/Y/Z = Shift, R/X = Space, +/Start = Esc
 *   USB keyboard               keys as on a PC
 *
 * HOME opens the HOME menu (handled by the player).
 *
 * While the game reads typed text (a high score name), an on-screen keyboard
 * replaces the game controls: point and press A, or move with the D-Pad or a
 * stick and press A (2 sideways, C on a Nunchuk); B (1 sideways, Z) deletes,
 * + (Start) confirms and - toggles Shift.
 */
class WiiInputManager : public CKInputManager
{
public:
    WiiInputManager(CKContext *context);
    ~WiiInputManager() override;

    // Keyboard
    void EnableKeyboardRepetition(CKBOOL iEnable = TRUE) override;
    CKBOOL IsKeyboardRepetitionEnabled() override;
    CKBOOL IsKeyDown(CKDWORD iKey, CKDWORD *oStamp = NULL) override;
    CKBOOL IsKeyUp(CKDWORD iKey) override;
    CKBOOL IsKeyToggled(CKDWORD iKey, CKDWORD *oStamp = NULL) override;
    int GetKeyName(CKDWORD iKey, char *oKeyName) override;
    CKDWORD GetKeyFromName(CKSTRING iKeyName) override;
    unsigned char *GetKeyboardState() override;
    CKBOOL IsKeyboardAttached() override;
    int GetNumberOfKeyInBuffer() override;
    int GetKeyFromBuffer(int i, CKDWORD &oKey, CKDWORD *oTimeStamp = NULL) override;

    // Mouse (the Wii Remote pointer)
    CKBOOL IsMouseButtonDown(CK_MOUSEBUTTON iButton) override;
    CKBOOL IsMouseClicked(CK_MOUSEBUTTON iButton) override;
    CKBOOL IsMouseToggled(CK_MOUSEBUTTON iButton) override;
    void GetMouseButtonsState(CKBYTE oStates[4]) override;
    void GetMousePosition(Vx2DVector &oPosition, CKBOOL iAbsolute = TRUE) override;
    void GetMouseRelativePosition(VxVector &oPosition) override;
    CKBOOL IsMouseAttached() override;

    // Joysticks: Classic Controllers / Nunchuks / GameCube pads, one per port
    CKBOOL IsJoystickAttached(int iJoystick) override;
    void GetJoystickPosition(int iJoystick, VxVector *oPosition) override;
    void GetJoystickRotation(int iJoystick, VxVector *oRotation) override;
    void GetJoystickSliders(int iJoystick, Vx2DVector *oPosition) override;
    void GetJoystickPointOfViewAngle(int iJoystick, float *oAngle) override;
    CKDWORD GetJoystickButtonsState(int iJoystick) override;
    CKBOOL IsJoystickButtonDown(int iJoystick, int iButton) override;

    void Pause(CKBOOL pause) override;

    void ShowCursor(CKBOOL iShow) override;
    CKBOOL GetCursorVisibility() override;
    VXCURSOR_POINTER GetSystemCursor() override;
    void SetSystemCursor(VXCURSOR_POINTER cursor) override;

    CKERROR OnCKInit() override;
    CKERROR OnCKEnd() override;
    CKERROR OnCKReset() override;
    CKERROR OnCKPlay() override;
    CKERROR PreProcess() override;
    CKERROR PostProcess() override;

    CKDWORD GetValidFunctionsMask() override
    {
        return CKMANAGER_FUNC_OnCKInit | CKMANAGER_FUNC_OnCKEnd | CKMANAGER_FUNC_OnCKReset |
               CKMANAGER_FUNC_OnCKPlay | CKMANAGER_FUNC_PreProcess | CKMANAGER_FUNC_PostProcess;
    }

private:
    struct KeyEvent
    {
        CKDWORD Key;
        CKDWORD Data; // 0x80 pressed, 0x00 released
        CKDWORD TimeStamp;
    };

    struct Joystick
    {
        CKBOOL Attached;
        VxVector Position;
        VxVector Rotation;
        Vx2DVector Sliders;
        float PointOfView;
        CKDWORD Buttons;
    };

    void ClearState();
    void PollUsbKeyboard(CKDWORD now);
    void PollControllers(CKBYTE wanted[WII_KEYBOARD_SIZE], CKDWORD now);
    void PollPointer();
    CKBOOL MouseButtonsInUse() const;
    void UpdateScreenKeyboard(CKDWORD now);
    void ReleaseTypedKeys(CKDWORD now);
    void PressKey(CKDWORD key, CKDWORD now);
    void ReleaseKey(CKDWORD key, CKDWORD now);
    void PushKeyEvent(CKDWORD key, CKDWORD data, CKDWORD now);
    void RepeatKeys(CKDWORD now);

    CKBYTE m_KeyboardState[WII_KEYBOARD_SIZE];
    CKDWORD m_KeyboardStamps[WII_KEYBOARD_SIZE];
    CKBYTE m_PadKeys[WII_KEYBOARD_SIZE];   // Keys held through controllers last frame
    CKBYTE m_UsbKeys[WII_KEYBOARD_SIZE];   // Keys held on a USB keyboard
    KeyEvent m_KeyBuffer[WII_KEY_BUFFER_SIZE];
    int m_KeyBufferCount;
    CKBOOL m_KeyboardRepetition;
    CKDWORD m_RepeatDelay;
    CKDWORD m_RepeatInterval;
    CKBOOL m_UsbKeyboardReady;
    CKBOOL m_UsbKeyboardAttached;

    Vx2DVector m_MousePosition;
    VxVector m_MouseDelta;
    CKBYTE m_MouseButtons[4];
    CKBYTE m_LastMouseButtons[4];
    CKBOOL m_PointerOnScreen;
    CKDWORD m_Frame;            // PreProcess calls
    CKDWORD m_MouseButtonsRead; // Frame the game last read the mouse buttons
    int m_PointerChannel;

    CKBOOL m_TextInput;
    WiiScreenKeyboard m_ScreenKeyboard;
    WiiScreenKeyboard::Controls m_KeyboardControls;
    CKDWORD m_TypedKeys[2]; // Held for one frame
    int m_TypedCount;

    Joystick m_Joysticks[WII_JOYSTICK_COUNT];
    int m_ControllersConnected;

    CKBOOL m_Paused;
    CKBOOL m_CursorVisible;
    VXCURSOR_POINTER m_Cursor;
};

#endif // WIIINPUTMANAGER_H
