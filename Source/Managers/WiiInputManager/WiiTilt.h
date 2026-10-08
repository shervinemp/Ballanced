#ifndef WIITILT_H
#define WIITILT_H

/**
 * Optional tilt controls: a Wii Remote held sideways is the board the ball
 * rolls on. Tipping its far edge down rolls forward, its right end down rolls
 * right.
 *
 * Made to be comfortable to hold for a whole level:
 *  - Level is wherever the remote rests when play starts or resumes, so the
 *    hands can settle at any angle; pausing and resuming sets it again.
 *  - A few degrees either way do nothing, so an unsteady hold does not roll
 *    the ball, and the full push comes at about 20 degrees, well short of a
 *    strained wrist.
 *  - In between, the arrow is held for a share of frames that grows with the
 *    angle, so a slight tilt gives a gentle push.
 *  - Readings are smoothed against hand tremor, and readings taken while the
 *    remote is shaken or swung are skipped.
 */
class WiiTilt
{
public:
    WiiTilt();

    // Takes the next steady reading as level.
    void Recenter();

    // One frame of accelerometer readings in g, along the Wii Remote's axes
    // (x towards its left side, y towards the expansion end, z out of the
    // buttons). Sets each axis to -1, 0 or 1: the arrow to hold this frame.
    void Update(float x, float y, float z, int *right, int *forward);

    bool IsCentered() const { return !m_Centering; }
    // The push on each axis, -1 to 1, after the dead zone.
    float GetRight() const { return m_Right; }
    float GetForward() const { return m_Forward; }

private:
    static float Push(float degrees);
    static int Pulse(float push, float &accumulated);

    bool m_Filtered;
    float m_X, m_Y, m_Z;     // Smoothed reading
    bool m_Centering;
    int m_StillFrames;      // Still frames while taking level
    float m_LevelRight;      // Degrees
    float m_LevelForward;
    float m_Right;
    float m_Forward;
    float m_RightPulse;      // Share of a frame carried over
    float m_ForwardPulse;
};

#endif // WIITILT_H
