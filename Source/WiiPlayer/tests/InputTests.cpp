// The on-screen keyboard that stands in for a PC keyboard when the game asks
// for text (the high score name), the hook the text blocks use to ask, and
// the HOME menu.

#include "TestFramework.h"

#include <string.h>

#include "CKAll.h"
#include "VxWiiPlatform.h"
#include "WiiScreenKeyboard.h"
#include "WiiSystem.h"

namespace
{
    const CKGUID kInputStringGuid(0x52fd6294, 0x612f51a5);

    WiiScreenKeyboard::Controls NoControls()
    {
        WiiScreenKeyboard::Controls controls;
        memset(&controls, 0, sizeof(controls));
        return controls;
    }

    WiiScreenKeyboard::Typed PressWith(WiiScreenKeyboard &keyboard, CKDWORD &now,
                                       void (*set)(WiiScreenKeyboard::Controls &))
    {
        WiiScreenKeyboard::Controls controls = NoControls();
        set(controls);
        WiiScreenKeyboard::Typed typed = keyboard.Update(controls, now);
        now += 20;
        // Let go, so the next press or move starts fresh.
        keyboard.Update(NoControls(), now);
        now += 20;
        return typed;
    }

    void Press(WiiScreenKeyboard::Controls &c) { c.Press = true; }
    void Right(WiiScreenKeyboard::Controls &c) { c.MoveX = 1; }
    void Left(WiiScreenKeyboard::Controls &c) { c.MoveX = -1; }
    void Down(WiiScreenKeyboard::Controls &c) { c.MoveY = 1; }
    void Delete(WiiScreenKeyboard::Controls &c) { c.Delete = true; }
    void Confirm(WiiScreenKeyboard::Controls &c) { c.Confirm = true; }
    void Shift(WiiScreenKeyboard::Controls &c) { c.Shift = true; }

    const wiisystem::OverlayItem *FindItem(const char *text)
    {
        const wiisystem::OverlayItem *items = NULL;
        const int count = wiisystem::GetOverlay(wiisystem::OVERLAY_KEYBOARD, &items);
        for (int i = 0; i < count; ++i)
        {
            if (strcmp(items[i].Text, text) == 0)
                return &items[i];
        }
        return NULL;
    }

    void TestTyping()
    {
        WiiScreenKeyboard keyboard;
        keyboard.Open();
        CKDWORD now = 1000;

        // The highlight starts on Q, with Shift on for a capital first letter.
        WiiScreenKeyboard::Typed typed = PressWith(keyboard, now, Press);
        WT_CHECK(typed.ScanCode == 0x10 && typed.Shifted, "first key %02X shifted %d, expected Q shifted",
                 (unsigned)typed.ScanCode, typed.Shifted);
        typed = PressWith(keyboard, now, Press);
        WT_CHECK(typed.ScanCode == 0x10 && !typed.Shifted, "second key %02X shifted %d, expected q",
                 (unsigned)typed.ScanCode, typed.Shifted);

        // Moving: right to W, down to S, left wraps the row.
        PressWith(keyboard, now, Right);
        typed = PressWith(keyboard, now, Press);
        WT_CHECK(typed.ScanCode == 0x11, "after Right typed %02X, expected W", (unsigned)typed.ScanCode);
        PressWith(keyboard, now, Down);
        typed = PressWith(keyboard, now, Press);
        WT_CHECK(typed.ScanCode == 0x1F, "after Down typed %02X, expected S", (unsigned)typed.ScanCode);
        PressWith(keyboard, now, Left);
        PressWith(keyboard, now, Left);
        typed = PressWith(keyboard, now, Press);
        WT_CHECK(typed.ScanCode == 0x28, "Left from A typed %02X, expected the wrapped apostrophe",
                 (unsigned)typed.ScanCode);

        // Shortcut buttons.
        typed = PressWith(keyboard, now, Delete);
        WT_CHECK(typed.ScanCode == 0x0E, "B typed %02X, expected Backspace", (unsigned)typed.ScanCode);
        typed = PressWith(keyboard, now, Confirm);
        WT_CHECK(typed.ScanCode == 0x1C, "+ typed %02X, expected Enter", (unsigned)typed.ScanCode);
        PressWith(keyboard, now, Shift);
        PressWith(keyboard, now, Left);
        typed = PressWith(keyboard, now, Press);
        WT_CHECK(typed.ScanCode == 0x26 && typed.Shifted, "after Shift typed %02X shifted %d, expected L shifted",
                 (unsigned)typed.ScanCode, typed.Shifted);

        // A held direction moves once, then repeats after a delay.
        keyboard.Open();
        WiiScreenKeyboard::Controls held = NoControls();
        held.MoveX = 1;
        keyboard.Update(held, now);        // Q -> W
        keyboard.Update(held, now + 100);  // still W
        keyboard.Update(held, now + 400);  // W -> E
        keyboard.Update(NoControls(), now + 420);
        typed = keyboard.Update(NoControls(), now + 440);
        WiiScreenKeyboard::Controls press = NoControls();
        press.Press = true;
        typed = keyboard.Update(press, now + 460);
        WT_CHECK(typed.ScanCode == 0x12, "held Right typed %02X, expected E after one repeat",
                 (unsigned)typed.ScanCode);
        now += 1000;

        // Pointing at a key and pressing A types it.
        keyboard.Open();
        keyboard.Show();
        const wiisystem::OverlayItem *m = FindItem("M");
        if (WT_CHECK(m != NULL, "key M on the overlay"))
        {
            WiiScreenKeyboard::Controls pointing = NoControls();
            pointing.Pointing = true;
            pointing.PointerX = (m->X0 + m->X1) * 0.5f;
            pointing.PointerY = (m->Y0 + m->Y1) * 0.5f;
            pointing.PointerPress = true;
            typed = keyboard.Update(pointing, now);
            WT_CHECK(typed.ScanCode == 0x32 && typed.Shifted, "pointer typed %02X, expected M",
                     (unsigned)typed.ScanCode);
        }
        WiiScreenKeyboard::Hide();
    }

    // Every key fits on the panel, inside the picture, without overlapping another.
    void TestLayout()
    {
        WiiScreenKeyboard keyboard;
        keyboard.Open();
        keyboard.Show();
        const wiisystem::OverlayItem *items = NULL;
        const int count = wiisystem::GetOverlay(wiisystem::OVERLAY_KEYBOARD, &items);
        if (!WT_CHECK(count > 40, "%d overlay items", count))
            return;
        const wiisystem::OverlayItem &panel = items[0];
        WT_CHECK(panel.X0 >= 0 && panel.Y0 >= 0 && panel.X1 <= 640 && panel.Y1 <= 480, "panel outside the picture");
        int bad = 0;
        for (int i = 2; i < count; ++i)
        {
            const wiisystem::OverlayItem &a = items[i];
            if (a.X0 < panel.X0 || a.X1 > panel.X1 || a.Y0 < panel.Y0 || a.Y1 > panel.Y1)
                ++bad;
            for (int j = i + 1; j < count; ++j)
            {
                const wiisystem::OverlayItem &b = items[j];
                if (a.X0 < b.X1 && b.X0 < a.X1 && a.Y0 < b.Y1 && b.Y0 < a.Y1)
                    ++bad;
            }
            // 8x16 characters at the item's scale must fit inside the key.
            const float width = strlen(a.Text) * 8.0f * a.TextScale;
            if (width > a.X1 - a.X0 - 4.0f || 16.0f * a.TextScale > a.Y1 - a.Y0)
                ++bad;
        }
        WT_CHECK(bad == 0, "%d keys overlap, leave the panel or overflow their text", bad);
        WiiScreenKeyboard::Hide();
    }

    // TT InputString asks for the keyboard every frame it runs, and the input
    // manager shows it while asked.
    void TestTextInputHook(CKContext *context)
    {
        VxWiiConsumeTextInputRequest();
        CKBehavior *beh = (CKBehavior *)context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "WiiInputString");
        if (WT_CHECK(beh->InitFromGuid(kInputStringGuid) == CK_OK, "TT InputString prototype"))
        {
            // Room for the text, as the game's saved block has.
            char text[64];
            memset(text, 0, sizeof(text));
            beh->GetLocalParameter(6)->SetValue(text, sizeof(text));
            beh->ActivateInput(0);
            beh->Execute(0.0f);
            WT_CHECK(VxWiiConsumeTextInputRequest(), "TT InputString did not ask for the keyboard");
            beh->Execute(0.0f);
            WT_CHECK(VxWiiConsumeTextInputRequest(), "TT InputString stopped asking while on");
            beh->ActivateInput(1);
            beh->Execute(0.0f);
            WT_CHECK(!VxWiiConsumeTextInputRequest(), "TT InputString asked for the keyboard while turning off");
        }
        context->DestroyObject(beh);

        CKBaseManager *input = context->GetManagerByGuid(INPUT_MANAGER_GUID);
        if (WT_CHECK(input != NULL, "input manager"))
        {
            VxWiiRequestTextInput();
            input->PreProcess();
            WT_CHECK(wiisystem::GetOverlay(wiisystem::OVERLAY_KEYBOARD, NULL) > 0, "keyboard not shown");
            input->PostProcess();
            input->PreProcess();
            WT_CHECK(wiisystem::GetOverlay(wiisystem::OVERLAY_KEYBOARD, NULL) == 0, "keyboard still shown");
            input->PostProcess();
        }
    }

    bool HomeMenuShows(const char *text)
    {
        const wiisystem::OverlayItem *items = NULL;
        const int count = wiisystem::GetOverlay(wiisystem::OVERLAY_HOME_MENU, &items);
        for (int i = 0; i < count; ++i)
        {
            if (strcmp(items[i].Text, text) == 0)
                return true;
        }
        return false;
    }

    // The HOME menu stays open without input, and closes on a quit request
    // (the console's Power and Reset buttons).
    void TestHomeMenu()
    {
        wiisystem::OpenHomeMenu();
        WT_CHECK(wiisystem::UpdateHomeMenu(), "HOME menu closed by itself");
        WT_CHECK(HomeMenuShows("HOME Menu") && HomeMenuShows("Homebrew Channel") && HomeMenuShows("Wii Menu") &&
                     HomeMenuShows("Close"),
                 "HOME menu items");
        wiisystem::RequestQuit(wiisystem::QUIT_TO_LOADER);
        WT_CHECK(!wiisystem::UpdateHomeMenu(), "HOME menu stayed open after a quit request");
        WT_CHECK(wiisystem::GetOverlay(wiisystem::OVERLAY_HOME_MENU, NULL) == 0, "HOME menu still drawn");
        wiisystem::RequestQuit(wiisystem::QUIT_NONE);
    }
}

void RunInputTests(CKContext *context)
{
    wiitest::BeginSuite("Screen keyboard and HOME menu");
    TestTyping();
    TestLayout();
    if (context)
        TestTextInputHook(context);
    TestHomeMenu();
    wiitest::EndSuite();
}
