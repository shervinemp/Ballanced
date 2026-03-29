#ifndef WIIINPUTMANAGER_H
#define WIIINPUTMANAGER_H

#include "CKAll.h"

#ifdef WII
#include <wiiuse/wpad.h>
#endif

class WiiInputManager : public CKInputManager {
public:
    WiiInputManager(CKContext* Context);
    virtual ~WiiInputManager();

    virtual CKERROR OnCKInit();
    virtual CKERROR OnCKEnd();
    virtual CKERROR PreProcess();

    virtual void EnableKeyboardRepetition(CKBOOL iEnable = TRUE);
    virtual CKBOOL IsKeyboardRepetitionEnabled();
    virtual CKBOOL IsKeyDown(CKDWORD iKey, CKDWORD *oStamp = NULL);
    virtual CKBOOL IsKeyUp(CKDWORD iKey);
    virtual CKBOOL IsKeyToggled(CKDWORD iKey, CKDWORD *oStamp = NULL);
    virtual int GetKeyName(CKDWORD iKey, char *oKeyName);
    virtual CKDWORD GetKeyFromName(CKSTRING iKeyName);
    virtual unsigned char *GetKeyboardState();
    virtual CKBOOL IsKeyboardAttached();
    virtual int GetNumberOfKeyInBuffer();
    virtual int GetKeyFromBuffer(int i, CKDWORD &oKey, CKDWORD *oTimeStamp = NULL);

    virtual CKBOOL IsMouseButtonDown(CK_MOUSEBUTTON iButton);
    virtual CKBOOL IsMouseClicked(CK_MOUSEBUTTON iButton);
    virtual CKBOOL IsMouseToggled(CK_MOUSEBUTTON iButton);
    virtual void GetMouseButtonsState(CKBYTE oStates[4]);
    virtual void GetMousePosition(Vx2DVector &oPosition, CKBOOL iAbsolute = TRUE);
    virtual void GetMouseRelativePosition(VxVector &oPosition);
    virtual CKBOOL IsMouseAttached();

    virtual CKBOOL IsJoystickAttached(int iJoystick);
    virtual void GetJoystickPosition(int iJoystick, VxVector *oPosition);
    virtual void GetJoystickRotation(int iJoystick, VxVector *oRotation);
    virtual void GetJoystickSliders(int iJoystick, Vx2DVector *oPosition);
    virtual void GetJoystickPointOfViewAngle(int iJoystick, float *oAngle);
    virtual CKDWORD GetJoystickButtonsState(int iJoystick);
    virtual CKBOOL IsJoystickButtonDown(int iJoystick, int iButton);

    virtual void Pause(CKBOOL pause);
    virtual void ShowCursor(CKBOOL iShow);
    virtual CKBOOL GetCursorVisibility();

    virtual VXCURSOR_POINTER GetSystemCursor();
    virtual void SetSystemCursor(VXCURSOR_POINTER cursor);

private:
    CKBOOL m_CursorVisible;
};

#endif // WIIINPUTMANAGER_H
