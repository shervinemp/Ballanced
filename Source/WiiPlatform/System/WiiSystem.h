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

    // Shows the HOME menu over a paused game. Returns QUIT_NONE to resume, or
    // the quit action the player chose (already recorded with RequestQuit).
    QuitAction ShowHomeMenu();

    // On-screen pointer the rasterizer draws over the game while the game
    // shows its cursor and a Wii Remote points at the screen.
    void SetPointer(bool visible, float x, float y, float angle);
    bool GetPointer(float *x, float *y, float *angle);
}

#endif // BALLANCE_WII_SYSTEM_H
