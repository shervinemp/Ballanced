#ifndef WIISCREENKEYBOARD_H
#define WIISCREENKEYBOARD_H

#include "CKAll.h"

/**
 * On-screen keyboard shown while the game reads typed text (the high score
 * name). Keys are picked with the Wii Remote pointer, or moved between with
 * the D-Pad or a stick, and typed into the game as keyboard scan codes.
 */
class WiiScreenKeyboard
{
public:
    // One frame of controller input, merged over all controllers.
    struct Controls
    {
        bool Pointing;      // A Wii Remote points at the screen
        float PointerX;
        float PointerY;
        bool PointerPress;  // A went down while pointing
        int MoveX;          // Held direction, -1 (left/up) to 1 (right/down)
        int MoveY;
        bool Press;         // Type the highlighted key
        bool Delete;        // Backspace
        bool Confirm;       // Enter
        bool Shift;         // Toggle Shift
    };

    struct Typed
    {
        CKDWORD ScanCode;   // 0 when nothing was typed
        bool Shifted;       // Hold Shift while the key goes down
    };

    WiiScreenKeyboard();

    // Starts a new text: Shift on for a capital first letter.
    void Open();
    Typed Update(const Controls &controls, CKDWORD now);
    // Publishes the keyboard to the overlay the rasterizer draws.
    void Show() const;
    static void Hide();

private:
    enum Action
    {
        ACTION_TYPE,
        ACTION_SHIFT,
        ACTION_DELETE,
        ACTION_CONFIRM
    };

    struct Key
    {
        char Lower;
        char Upper;
        const char *Label;  // For keys that are not a character
        CKDWORD ScanCode;
        Action Type;
        float X0, Y0, X1, Y1;
    };

    void Layout();
    void AddRow(const char *lower, const char *upper, const CKDWORD *scanCodes, float y, float x0);
    void AddKey(char lower, char upper, const char *label, CKDWORD scanCode, Action type,
                float x, float y, float width);
    int KeyAt(float x, float y) const;
    int Neighbour(int from, int dx, int dy) const;
    int Find(Action type) const;
    Typed Activate(int key, CKDWORD now);

    Key m_Keys[48];
    int m_KeyCount;
    int m_Highlight;
    bool m_Shift;
    int m_LastMoveX;
    int m_LastMoveY;
    CKDWORD m_NextMove;
    int m_Flash;
    CKDWORD m_FlashUntil;
};

#endif // WIISCREENKEYBOARD_H
