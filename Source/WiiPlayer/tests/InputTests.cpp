// The on-screen keyboard that stands in for a PC keyboard when the game asks
// for text (the high score name), the hook the text blocks use to ask, the
// HOME menu, the game's key bindings the controllers follow, and tilt.

#include "TestFramework.h"

#include <math.h>
#include <string.h>

#include "CKAll.h"
#include "VxWiiPlatform.h"
#include "WiiInputManager.h"
#include "WiiKeyBindings.h"
#include "WiiScreenKeyboard.h"
#include "WiiSystem.h"
#include "WiiTilt.h"

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

namespace
{
    // DirectInput scan codes.
    const CKDWORD kUp = 0xC8, kDown = 0xD0, kLeft = 0xCB, kRight = 0xCD, kShift = 0x2A, kSpace = 0x39;
    const CKDWORD kW = 0x11, kA = 0x1E, kS = 0x1F, kD = 0x20, kX = 0x2D;

    // The game's "Keyboard" array: one key parameter for each control.
    CKDataArray *CreateBindings(CKContext *context, const CKDWORD keys[WiiKeyBindings::CONTROL_COUNT])
    {
        static const char *kColumns[WiiKeyBindings::CONTROL_COUNT] = {"up", "down", "left", "right", "rotation", "camup"};
        CKDataArray *array = (CKDataArray *)context->CreateObject(CKCID_DATAARRAY, (CKSTRING) "Keyboard");
        for (int c = 0; c < WiiKeyBindings::CONTROL_COUNT; ++c)
            array->InsertColumn(-1, CKARRAYTYPE_PARAMETER, (CKSTRING)kColumns[c], CKPGUID_KEY);
        array->InsertRow(-1);
        for (int c = 0; c < WiiKeyBindings::CONTROL_COUNT; ++c)
        {
            CKDWORD key = keys[c];
            array->SetElementValue(0, c, &key, sizeof(key));
        }
        return array;
    }

    // The keys that come out of the bindings for the given pressed keys, in
    // scan code order.
    int Remap(const WiiKeyBindings &bindings, const CKDWORD *pressed, int count, CKDWORD out[8])
    {
        CKBYTE keys[256];
        memset(keys, 0, sizeof(keys));
        for (int i = 0; i < count; ++i)
            keys[pressed[i]] = 1;
        bindings.Apply(keys);
        int n = 0;
        for (int key = 0; key < 256 && n < 8; ++key)
        {
            if (keys[key])
                out[n++] = (CKDWORD)key;
        }
        return n;
    }

    bool Same(const CKDWORD *actual, int count, const CKDWORD *expected, int expectedCount)
    {
        return count == expectedCount && memcmp(actual, expected, count * sizeof(CKDWORD)) == 0;
    }

    // A database from a PC can bind other keys; the controllers follow it.
    void TestKeyBindings(CKContext *context)
    {
        WiiKeyBindings bindings;
        bindings.Update(context);
        WT_CHECK(bindings.GetKey(WiiKeyBindings::FORWARD) == kUp && bindings.GetKey(WiiKeyBindings::LIFT_CAMERA) == kSpace,
                 "defaults without the game's array: %x %x", bindings.GetKey(WiiKeyBindings::FORWARD),
                 bindings.GetKey(WiiKeyBindings::LIFT_CAMERA));
        CKDWORD out[8];
        const CKDWORD upShift[] = {kShift, kUp};
        int n = Remap(bindings, upShift, 2, out);
        WT_CHECK(Same(out, n, upShift, 2), "default bindings changed the keys");

        const CKDWORD wasd[WiiKeyBindings::CONTROL_COUNT] = {kW, kS, kA, kD, kX, kSpace};
        CKDataArray *array = CreateBindings(context, wasd);
        bindings.Update(context);
        WT_CHECK(bindings.GetKey(WiiKeyBindings::FORWARD) == kW && bindings.GetKey(WiiKeyBindings::RIGHT) == kD &&
                     bindings.GetKey(WiiKeyBindings::ROTATE_CAMERA) == kX,
                 "bindings read: %x %x %x", bindings.GetKey(WiiKeyBindings::FORWARD),
                 bindings.GetKey(WiiKeyBindings::RIGHT), bindings.GetKey(WiiKeyBindings::ROTATE_CAMERA));
        // Up rolls with W and still moves through the menus; Shift is free,
        // so rotating the camera presses X and Shift.
        n = Remap(bindings, upShift, 2, out);
        const CKDWORD wasdOut[] = {kW, kShift, kX, kUp};
        WT_CHECK(Same(out, n, wasdOut, 4), "WASD: %d keys, first %x", n, n > 0 ? out[0] : 0);

        // Forward and backward swapped: Up is bound to backward, so it no
        // longer stands for forward.
        context->DestroyObject(array);
        const CKDWORD swapped[WiiKeyBindings::CONTROL_COUNT] = {kDown, kUp, kLeft, kRight, kShift, kSpace};
        array = CreateBindings(context, swapped);
        bindings.Update(context);
        const CKDWORD up[] = {kUp};
        n = Remap(bindings, up, 1, out);
        const CKDWORD down[] = {kDown};
        WT_CHECK(Same(out, n, down, 1), "swapped: %d keys, first %x", n, n > 0 ? out[0] : 0);

        // Gone again: back to the defaults.
        context->DestroyObject(array);
        bindings.Update(context);
        WT_CHECK(bindings.GetKey(WiiKeyBindings::FORWARD) == kUp, "defaults after the array went away: %x",
                 bindings.GetKey(WiiKeyBindings::FORWARD));
    }

    // Frames as the engine runs them; the game asks about keys and reads the
    // mouse between PreProcess and PostProcess.
    void Frames(WiiInputManager *input, int count, CKDWORD askKey = 0, bool readMouse = false)
    {
        for (int i = 0; i < count; ++i)
        {
            input->PreProcess();
            if (askKey)
                input->IsKeyDown(askKey);
            if (readMouse)
                input->IsMouseButtonDown(CK_MOUSEBUTTON_LEFT);
            input->PostProcess();
        }
    }

    // Tilt and the D-Pad camera apply while the game plays: it asks about the
    // camera keys, through short gaps, and not while a menu reads the mouse.
    void TestPlayState(CKContext *context)
    {
        WiiInputManager *input = (WiiInputManager *)context->GetManagerByGuid(INPUT_MANAGER_GUID);
        if (!WT_CHECK(input != NULL, "input manager"))
            return;
        Frames(input, 200);
        WT_CHECK(!input->IsGamePlaying(), "playing without the game asking about keys");
        Frames(input, 2, kShift);
        Frames(input, 1);
        WT_CHECK(input->IsGamePlaying(), "not playing while the game asks about Shift");
        Frames(input, 100);
        WT_CHECK(input->IsGamePlaying(), "play stopped in a short gap");
        Frames(input, 100);
        WT_CHECK(!input->IsGamePlaying(), "still playing long after the last question");
        Frames(input, 2, kSpace);
        Frames(input, 1);
        WT_CHECK(input->IsGamePlaying(), "not playing while the game asks about Space");
        Frames(input, 2, kShift, true);
        Frames(input, 1, kShift);
        WT_CHECK(!input->IsGamePlaying(), "playing while a menu reads the mouse");
        Frames(input, 40, kShift);
        WT_CHECK(input->IsGamePlaying(), "not playing once the menu has gone");
        Frames(input, 200);
    }

    // A sideways Wii Remote's reading with its far edge tipped down by
    // forward degrees and its right end by right degrees. Its left side (+x)
    // faces the player and its expansion end (+y) is on the right.
    void Reading(float forward, float right, float g, float out[3])
    {
        const float x = tanf(forward * (float)M_PI / 180.0f);
        const float y = -tanf(right * (float)M_PI / 180.0f);
        const float length = sqrtf(x * x + y * y + 1.0f);
        out[0] = g * x / length;
        out[1] = g * y / length;
        out[2] = g / length;
    }

    // Feeds frames of one pose; counts the frames pressing each way.
    void Hold(WiiTilt &tilt, float forward, float right, int frames, int *forwardFrames, int *rightFrames,
              float g = 1.0f)
    {
        float r[3];
        Reading(forward, right, g, r);
        *forwardFrames = 0;
        *rightFrames = 0;
        for (int i = 0; i < frames; ++i)
        {
            int x, y;
            tilt.Update(r[0], r[1], r[2], &x, &y);
            *forwardFrames += y;
            *rightFrames += x;
        }
    }

    void TestTilt()
    {
        WiiTilt tilt;
        int forward, right;
        // Hands rest with the remote tipped 15 degrees towards the player:
        // that is level, and does nothing.
        Hold(tilt, -15.0f, 0.0f, 3, &forward, &right);
        WT_CHECK(!tilt.IsCentered() && forward == 0 && right == 0, "rolled before taking level");
        Hold(tilt, -15.0f, 0.0f, 60, &forward, &right);
        WT_CHECK(tilt.IsCentered() && forward == 0 && right == 0, "level: centered %d, %d forward, %d right",
                 (int)tilt.IsCentered(), forward, right);

        // A few degrees off level is an unsteady hold.
        Hold(tilt, -12.0f, -3.0f, 60, &forward, &right);
        WT_CHECK(forward == 0 && right == 0, "dead zone: %d forward, %d right", forward, right);

        // Past the full tilt the arrow is held every frame.
        Hold(tilt, 10.0f, 0.0f, 20, &forward, &right);
        Hold(tilt, 10.0f, 0.0f, 60, &forward, &right);
        WT_CHECK(forward == 60 && right == 0, "full tilt forward: %d forward, %d right", forward, right);

        // Halfway between the dead zone and full tilt, half the frames.
        Hold(tilt, -15.0f, -12.0f, 20, &forward, &right);
        Hold(tilt, -15.0f, -12.0f, 60, &forward, &right);
        WT_CHECK(forward == 0 && right >= -32 && right <= -28, "half tilt left: %d forward, %d right", forward, right);

        // A slight tilt starts pushing at once, then now and then.
        Hold(tilt, -15.0f, 0.0f, 30, &forward, &right);
        float r[3];
        Reading(-15.0f, 7.0f, 1.0f, r);
        int first = 0;
        for (int i = 0; i < 30 && first == 0; ++i)
        {
            int x, y;
            tilt.Update(r[0], r[1], r[2], &x, &y);
            first = x;
        }
        WT_CHECK(first == 1, "slight tilt right never pushed");

        // Shaking or swinging the remote does not count as tilt.
        Hold(tilt, -15.0f, 0.0f, 30, &forward, &right);
        Hold(tilt, 60.0f, 0.0f, 10, &forward, &right, 2.0f);
        WT_CHECK(forward == 0 && right == 0, "shake: %d forward, %d right", forward, right);

        // Recentering takes the new rest as level.
        tilt.Recenter();
        Hold(tilt, 5.0f, 0.0f, 60, &forward, &right);
        WT_CHECK(tilt.IsCentered() && forward == 0, "after recentering: %d forward", forward);
        Hold(tilt, -20.0f, 0.0f, 20, &forward, &right);
        Hold(tilt, -20.0f, 0.0f, 60, &forward, &right);
        WT_CHECK(forward == -60, "back from the new level: %d forward", forward);

        // Held far over, on its side, is no way to rest it: level waits.
        tilt.Recenter();
        Hold(tilt, 0.0f, 70.0f, 60, &forward, &right);
        WT_CHECK(!tilt.IsCentered() && right == 0, "took level on its side");
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

    wiitest::BeginSuite("Key bindings and tilt");
    if (context)
    {
        TestKeyBindings(context);
        TestPlayState(context);
    }
    TestTilt();
    wiitest::EndSuite();
}
