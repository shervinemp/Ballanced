#ifndef BALLANCE_WII_SYSTEM_H
#define BALLANCE_WII_SYSTEM_H

#include <gccore.h>

// Console services shared by the Wii player, rasterizer and managers.
namespace wiisystem
{
    enum QuitAction
    {
        QUIT_NONE = 0,
        QUIT_TO_LOADER,  // Return to the Homebrew Channel (or whatever launched us)
        QUIT_TO_MENU,    // Return to the Wii Menu
        QUIT_POWER_OFF   // Power or Wii Remote power button
    };

    // Brings up video, controllers and storage, and locates the game files.
    // Call first thing in main().
    bool Init(int argc, char **argv);

    // TV mode chosen from the console settings (progressive scan, PAL60, ...).
    GXRModeObj *GetVideoMode();

    // The picture the game renders, in embedded frame buffer pixels.
    int GetRenderWidth();
    int GetRenderHeight();

    // True when the console is set to 16:9.
    bool IsWidescreen();

    // Folder holding base.cmo and the game data, with a trailing '/'.
    const char *GetGamePath();

    // Set by the console buttons or the HOME menu; the main loop polls it.
    QuitAction GetQuitRequest();
    void RequestQuit(QuitAction action);

    // Leaves the game according to the pending request. Does not return.
    void Exit();

    // Shows a full-screen message on the TV and waits for a button press.
    // Used for errors that happen before (or instead of) the game starts.
    void ShowMessage(const char *title, const char *message);

    // HOME button: the input manager raises it, the player opens the HOME menu.
    void RequestHomeMenu();
    bool ConsumeHomeMenuRequest();

    // HOME menu, drawn over the paused game: after OpenHomeMenu, call
    // UpdateHomeMenu and draw a frame until it returns false. Leaving the game
    // from the menu is recorded with RequestQuit.
    void OpenHomeMenu();
    bool UpdateHomeMenu();

    // Frame timing: the rasterizer adds how long each frame waited for the GPU
    // to finish and for the TV's vertical blank; the player takes the totals.
    void AddFrameWaits(u64 gpuTicks, u64 vsyncTicks);
    void TakeFrameWaits(u64 *gpuTicks, u64 *vsyncTicks);

    // Heap bytes in use, and bytes the heap can still hand out (free blocks
    // plus the unclaimed part of the memory arenas it grows into).
    void GetMemoryStatus(u32 *used, u32 *available);

    // On-screen pointer the rasterizer draws over the game while the game
    // shows its cursor and a Wii Remote points at the screen.
    void SetPointer(bool visible, float x, float y, float angle);
    bool GetPointer(float *x, float *y, float *angle);

    // System screens drawn over the game picture (on-screen keyboard, HOME
    // menu): boxes with optional centered text, in render pixels, drawn in
    // order below the pointer. Colors are 0xRRGGBBAA; alpha 0 skips a part.
    struct OverlayItem
    {
        float X0, Y0, X1, Y1;
        u32 Fill;
        u32 Border;
        u32 TextColor;
        float TextScale; // 1 draws 8x16 pixel characters
        char Text[40];
    };

    enum OverlayLayer
    {
        OVERLAY_KEYBOARD = 0, // Owned by the input manager
        OVERLAY_NOTICE,       // Owned by the input manager
        OVERLAY_HOME_MENU,    // Owned by the HOME menu
        OVERLAY_LAYER_COUNT
    };

    // Replaces a layer's items (count 0 hides it).
    void SetOverlay(OverlayLayer layer, const OverlayItem *items, int count);
    int GetOverlay(OverlayLayer layer, const OverlayItem **items);

    // Fills an item; text may be NULL.
    OverlayItem MakeOverlayItem(float x0, float y0, float x1, float y1, u32 fill, u32 border,
                                const char *text = NULL, u32 textColor = 0xFFFFFFFF, float textScale = 1.0f);
}

#endif // BALLANCE_WII_SYSTEM_H
