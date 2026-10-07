#include "WiiScreenKeyboard.h"

#include "WiiSystem.h"

#include <ctype.h>
#include <math.h>

namespace
{
    // DirectInput scan codes typed by the keyboard.
    const CKDWORD kBackspace = 0x0E;
    const CKDWORD kReturn = 0x1C;
    const CKDWORD kSpace = 0x39;

    const CKDWORD kDigitCodes[] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    const CKDWORD kTopCodes[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19};
    const CKDWORD kMiddleCodes[] = {0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x28};
    const CKDWORD kBottomCodes[] = {0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x34};

    // Layout, in render pixels: a panel across the lower half of the picture.
    const float kPanelX0 = 36.0f;
    const float kPanelY0 = 224.0f;
    const float kPanelX1 = 604.0f;
    const float kPanelY1 = 470.0f;
    const float kKeyWidth = 44.0f;
    const float kKeyHeight = 38.0f;
    const float kGap = 6.0f;
    const float kRowPitch = 42.0f;
    const float kFirstRow = 254.0f;
    const float kWideKey = 60.0f;

    // Colors (0xRRGGBBAA), after the console's own menus.
    const u32 kPanelFill = 0xEEF1F4F2;
    const u32 kPanelBorder = 0xB4BCC8FF;
    const u32 kHintColor = 0x5A6470FF;
    const u32 kKeyFill = 0xFFFFFFFF;
    const u32 kSpecialFill = 0xDCE2EAFF;
    const u32 kShiftOnFill = 0xBEE6F8FF;
    const u32 kKeyBorder = 0xA8B0BCFF;
    const u32 kHighlightFill = 0xD6F1FCFF;
    const u32 kHighlightBorder = 0x2FB4E8FF;
    const u32 kFlashFill = 0x2FB4E8FF;
    const u32 kKeyText = 0x283038FF;

    const CKDWORD kFlashTime = 120;
    const CKDWORD kRepeatDelay = 350;
    const CKDWORD kRepeatInterval = 110;
}

WiiScreenKeyboard::WiiScreenKeyboard()
    : m_KeyCount(0),
      m_Highlight(0),
      m_Shift(true),
      m_LastMoveX(0),
      m_LastMoveY(0),
      m_NextMove(0),
      m_Flash(-1),
      m_FlashUntil(0)
{
    Layout();
}

void WiiScreenKeyboard::AddKey(char lower, char upper, const char *label, CKDWORD scanCode, Action type,
                               float x, float y, float width)
{
    if (m_KeyCount >= (int)(sizeof(m_Keys) / sizeof(m_Keys[0])))
        return;
    Key &key = m_Keys[m_KeyCount++];
    key.Lower = lower;
    key.Upper = upper;
    key.Label = label;
    key.ScanCode = scanCode;
    key.Type = type;
    key.X0 = x;
    key.Y0 = y;
    key.X1 = x + width;
    key.Y1 = y + kKeyHeight;
}

void WiiScreenKeyboard::AddRow(const char *lower, const char *upper, const CKDWORD *scanCodes, float y, float x0)
{
    for (int i = 0; lower[i]; ++i)
        AddKey(lower[i], upper[i], NULL, scanCodes[i], ACTION_TYPE, x0 + i * (kKeyWidth + kGap), y, kKeyWidth);
}

static float RowStart(int keys, float extraWidth)
{
    const float width = keys * kKeyWidth + extraWidth + (keys - 1) * kGap;
    return floorf(320.0f - width * 0.5f);
}

void WiiScreenKeyboard::Layout()
{
    m_KeyCount = 0;
    AddRow("1234567890-", "1234567890-", kDigitCodes, kFirstRow, RowStart(11, 0.0f));
    AddRow("qwertyuiop", "QWERTYUIOP", kTopCodes, kFirstRow + kRowPitch, RowStart(10, 0.0f));
    AddRow("asdfghjkl'", "ASDFGHJKL'", kMiddleCodes, kFirstRow + 2 * kRowPitch, RowStart(10, 0.0f));

    // Shift, the bottom letters, Backspace.
    const float y = kFirstRow + 3 * kRowPitch;
    float x = RowStart(10, 2 * (kWideKey - kKeyWidth));
    AddKey(0, 0, "Shift", 0, ACTION_SHIFT, x, y, kWideKey);
    x += kWideKey + kGap;
    AddRow("zxcvbnm.", "ZXCVBNM.", kBottomCodes, y, x);
    x += 8 * (kKeyWidth + kGap);
    AddKey(0, 0, "Back", kBackspace, ACTION_DELETE, x, y, kWideKey);

    // Space and OK.
    const float spaceWidth = 300.0f;
    const float okWidth = 140.0f;
    x = floorf(320.0f - (spaceWidth + kGap + okWidth) * 0.5f);
    AddKey(' ', ' ', "Space", kSpace, ACTION_TYPE, x, kFirstRow + 4 * kRowPitch, spaceWidth);
    AddKey(0, 0, "OK", kReturn, ACTION_CONFIRM, x + spaceWidth + kGap, kFirstRow + 4 * kRowPitch, okWidth);
}

void WiiScreenKeyboard::Open()
{
    m_Highlight = 11; // Q
    m_Shift = true;
    m_LastMoveX = m_LastMoveY = 0;
    m_Flash = -1;
}

int WiiScreenKeyboard::KeyAt(float x, float y) const
{
    for (int i = 0; i < m_KeyCount; ++i)
    {
        const Key &key = m_Keys[i];
        if (x >= key.X0 && x < key.X1 && y >= key.Y0 && y < key.Y1)
            return i;
    }
    return -1;
}

// The key next to another in a direction, wrapping around the edges.
int WiiScreenKeyboard::Neighbour(int from, int dx, int dy) const
{
    const Key &origin = m_Keys[from];
    const float cx = (origin.X0 + origin.X1) * 0.5f;
    const float cy = (origin.Y0 + origin.Y1) * 0.5f;

    int best = -1;
    int wrap = -1;
    float bestScore = 0.0f;
    float wrapScore = 0.0f;
    for (int i = 0; i < m_KeyCount; ++i)
    {
        if (i == from)
            continue;
        const Key &key = m_Keys[i];
        const float ox = (key.X0 + key.X1) * 0.5f - cx;
        const float oy = (key.Y0 + key.Y1) * 0.5f - cy;
        float score;
        float far;
        if (dx != 0)
        {
            if (fabsf(oy) > 1.0f)
                continue;
            score = ox * dx > 0.0f ? fabsf(ox) : -1.0f;
            far = ox * dx < 0.0f ? fabsf(ox) : -1.0f;
        }
        else
        {
            // Nearest row first, then the closest column.
            score = oy * dy > 1.0f ? fabsf(oy) * 8.0f + fabsf(ox) : -1.0f;
            far = oy * dy < -1.0f ? fabsf(oy) * 8.0f - fabsf(ox) : -1.0f;
        }
        if (score >= 0.0f && (best < 0 || score < bestScore))
        {
            best = i;
            bestScore = score;
        }
        if (far >= 0.0f && (wrap < 0 || far > wrapScore))
        {
            wrap = i;
            wrapScore = far;
        }
    }
    if (best >= 0)
        return best;
    return wrap >= 0 ? wrap : from;
}

WiiScreenKeyboard::Typed WiiScreenKeyboard::Activate(int index, CKDWORD now)
{
    Typed typed = {0, false};
    if (index < 0 || index >= m_KeyCount)
        return typed;
    const Key &key = m_Keys[index];
    m_Flash = index;
    m_FlashUntil = now + kFlashTime;
    switch (key.Type)
    {
    case ACTION_SHIFT:
        m_Shift = !m_Shift;
        break;
    case ACTION_TYPE:
        typed.ScanCode = key.ScanCode;
        // Shift is held for one capital letter.
        if (isalpha((unsigned char)key.Lower))
        {
            typed.Shifted = m_Shift;
            m_Shift = false;
        }
        break;
    case ACTION_DELETE:
    case ACTION_CONFIRM:
        typed.ScanCode = key.ScanCode;
        break;
    }
    return typed;
}

int WiiScreenKeyboard::Find(Action type) const
{
    for (int i = 0; i < m_KeyCount; ++i)
    {
        if (m_Keys[i].Type == type)
            return i;
    }
    return -1;
}

WiiScreenKeyboard::Typed WiiScreenKeyboard::Update(const Controls &controls, CKDWORD now)
{
    Typed typed = {0, false};

    // Pointing picks the key under the pointer.
    const int pointed = controls.Pointing ? KeyAt(controls.PointerX, controls.PointerY) : -1;
    if (pointed >= 0)
        m_Highlight = pointed;

    // D-Pad and sticks move the highlight, repeating while held.
    int moveX = controls.MoveY != 0 ? 0 : controls.MoveX;
    int moveY = controls.MoveY;
    if (moveX != 0 || moveY != 0)
    {
        bool move = false;
        if (moveX != m_LastMoveX || moveY != m_LastMoveY)
        {
            move = true;
            m_NextMove = now + kRepeatDelay;
        }
        else if ((int)(now - m_NextMove) >= 0)
        {
            move = true;
            m_NextMove = now + kRepeatInterval;
        }
        if (move)
            m_Highlight = Neighbour(m_Highlight, moveX, moveY);
    }
    m_LastMoveX = moveX;
    m_LastMoveY = moveY;

    if (controls.PointerPress && pointed >= 0)
        return Activate(pointed, now);
    if (controls.Press)
        return Activate(m_Highlight, now);

    // Shortcut buttons flash their key too.
    if (controls.Delete)
        return Activate(Find(ACTION_DELETE), now);
    if (controls.Confirm)
        return Activate(Find(ACTION_CONFIRM), now);
    if (controls.Shift)
        return Activate(Find(ACTION_SHIFT), now);
    if (m_Flash >= 0 && (int)(now - m_FlashUntil) >= 0)
        m_Flash = -1;
    return typed;
}

void WiiScreenKeyboard::Show() const
{
    wiisystem::OverlayItem items[64];
    int count = 0;
    items[count++] = wiisystem::MakeOverlayItem(kPanelX0, kPanelY0, kPanelX1, kPanelY1, kPanelFill, kPanelBorder);
    items[count++] = wiisystem::MakeOverlayItem(kPanelX0, kPanelY0 + 4.0f, kPanelX1, kFirstRow - 4.0f, 0, 0,
                                                "A: type   B: delete   -: Shift   +: OK", kHintColor);

    for (int i = 0; i < m_KeyCount && count < 64; ++i)
    {
        const Key &key = m_Keys[i];
        const bool special = key.Label != NULL;
        u32 fill = special ? kSpecialFill : kKeyFill;
        if (key.Type == ACTION_SHIFT && m_Shift)
            fill = kShiftOnFill;
        u32 border = kKeyBorder;
        u32 textColor = kKeyText;
        if (i == m_Highlight)
        {
            fill = kHighlightFill;
            border = kHighlightBorder;
        }
        if (i == m_Flash)
        {
            fill = kFlashFill;
            textColor = 0xFFFFFFFF;
        }

        char text[2] = {m_Shift ? key.Upper : key.Lower, '\0'};
        const char *label = special ? key.Label : text;
        const float scale = special && key.Type != ACTION_CONFIRM ? 1.0f : 2.0f;
        items[count++] = wiisystem::MakeOverlayItem(key.X0, key.Y0, key.X1, key.Y1, fill, border, label, textColor, scale);
    }
    wiisystem::SetOverlay(wiisystem::OVERLAY_KEYBOARD, items, count);
}

void WiiScreenKeyboard::Hide()
{
    wiisystem::SetOverlay(wiisystem::OVERLAY_KEYBOARD, NULL, 0);
}
