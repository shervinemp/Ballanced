#ifndef WIIKEYBINDINGS_H
#define WIIKEYBINDINGS_H

#include "CKAll.h"

/**
 * The keys the game binds to rolling the ball and moving the camera.
 *
 * Ballance keeps them in the data array "Keyboard" (columns up, down, left,
 * right, rotation and camup, one key each), filled from Database.tdb when the
 * game starts and changed in Options > Controls. A database brought over from
 * a PC can bind other keys than the arrows, Shift and Space, so the
 * controllers press whatever the game binds instead of fixed keys.
 */
class WiiKeyBindings
{
public:
    enum Control
    {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
        ROTATE_CAMERA,
        LIFT_CAMERA,
        CONTROL_COUNT
    };

    WiiKeyBindings();

    // Reads the bindings from the game, or goes back to the defaults while
    // the game has none.
    void Update(CKContext *context);

    // keys holds what the controllers pressed, each control given as its
    // default key (arrows, Shift, Space). Moves those to the keys the game
    // binds; a default key no control is bound to stays pressed, so the
    // D-Pad still moves through the menus.
    void Apply(CKBYTE keys[256]) const;

    CKDWORD GetKey(Control control) const { return m_Keys[control]; }
    static CKDWORD GetDefaultKey(Control control);

private:
    CKBOOL IsBound(CKDWORD key) const;

    CKDWORD m_Keys[CONTROL_COUNT];
    CK_ID m_ArrayId;
};

#endif // WIIKEYBINDINGS_H
