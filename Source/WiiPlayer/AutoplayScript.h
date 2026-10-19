#ifndef AUTOPLAYSCRIPT_H
#define AUTOPLAYSCRIPT_H

#include "CKAll.h"
#include "WiiPhysicsShapes.h"

class WiiGamePlayer;

// Plays the game from a script for benchmarks and profiles, the same way on a
// console and in Dolphin. Player.ini names the script under [Wii] Autoplay,
// relative to the game folder. One command per line, '#' starts a comment:
//
//   wait <frames>               let the game run
//   hold <frames> <key>...      hold keys down for that many frames
//   waitplay [<frames>]         wait (at most that long) until the game is in
//                               play, listening for its camera keys
//   report start                start counting frame times
//   report stop                 log the frame times since "report start", and
//                               the collision surfaces built or reused
//   profile start [<us>]        sample the CPU every <us> microseconds (1000), up
//                               to 40000 samples
//   profile stop <file>         write the samples (Source/WiiPlayer/tools/wii_profile.py)
//   shot <file>                 save the picture as a TGA file
//   log <text>                  write a line to Player.log
//   quit                        leave the game
//
// Keys are DirectInput key names with '_' for spaces (Enter, Escape,
// Up_Arrow, Left_Shift, ...) or scan codes such as 0x1C. Files are written
// to the game folder. Frames, not seconds, keep a script in step with the
// game however long loading takes.
class AutoplayScript
{
public:
    AutoplayScript();

    bool Load(const char *path, CKContext *context);
    bool IsActive() const { return m_Next < m_Commands.Size(); }

    // Once a frame, before the game processes it.
    void Update(WiiGamePlayer &player);

private:
    enum Type
    {
        WAIT,
        HOLD,
        WAIT_PLAY,
        REPORT_START,
        REPORT_STOP,
        PROFILE_START,
        PROFILE_STOP,
        SHOT,
        LOG,
        QUIT
    };

    struct Command
    {
        Type Kind;
        int Frames;
        int Line;
        CKBYTE Keys[256];
        XString Text;
    };

    void Execute(const Command &command, WiiGamePlayer &player);

    XClassArray<Command> m_Commands;
    int m_Next;
    int m_Remaining; // Frames left of the current timed command, -1 before it starts
    XString m_Folder;
    wiiphysicsshapes::Stats m_Shapes; // At "report start"
};

#endif // AUTOPLAYSCRIPT_H
