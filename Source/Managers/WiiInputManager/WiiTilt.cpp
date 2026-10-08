#include "WiiTilt.h"

#include <math.h>

namespace
{
    const float kDegrees = 180.0f / (float)M_PI;
    const float kDeadZone = 4.0f;       // Degrees from level that do nothing
    const float kFullTilt = 20.0f;      // Degrees from level for the full push
    const float kLevelLimit = 50.0f;    // Furthest from flat a hold is taken as level
    const int kStillFrames = 5;        // Still readings before taking level
    const float kStillness = 2.0f;      // Degrees a still reading is off the smoothed one
    const float kSmoothing = 0.4f;      // Share of each new reading
    // Only gravity is felt while the remote is held still; more or less means
    // it is moving, and the reading says little about how it is tilted.
    const float kSteadyMin = 0.75f;
    const float kSteadyMax = 1.25f;

    float Wrap(float degrees)
    {
        if (degrees > 180.0f)
            return degrees - 360.0f;
        if (degrees < -180.0f)
            return degrees + 360.0f;
        return degrees;
    }
}

WiiTilt::WiiTilt()
{
    m_Filtered = false;
    m_X = m_Y = 0.0f;
    m_Z = 1.0f;
    Recenter();
}

void WiiTilt::Recenter()
{
    m_Centering = true;
    m_StillFrames = 0;
    m_LevelRight = m_LevelForward = 0.0f;
    m_Right = m_Forward = 0.0f;
    m_RightPulse = m_ForwardPulse = 0.0f;
}

float WiiTilt::Push(float degrees)
{
    const float amount = (fabsf(degrees) - kDeadZone) / (kFullTilt - kDeadZone);
    if (amount <= 0.0f)
        return 0.0f;
    const float push = amount < 1.0f ? amount : 1.0f;
    return degrees < 0.0f ? -push : push;
}

// Holds the arrow on a share of frames equal to the push, starting at once.
int WiiTilt::Pulse(float push, float &accumulated)
{
    accumulated += fabsf(push);
    if (accumulated <= 0.0f)
        return 0;
    accumulated -= 1.0f;
    return push < 0.0f ? -1 : 1;
}

void WiiTilt::Update(float x, float y, float z, int *right, int *forward)
{
    *right = 0;
    *forward = 0;

    const float g = sqrtf(x * x + y * y + z * z);
    const bool steady = g > kSteadyMin && g < kSteadyMax;
    if (steady)
    {
        if (!m_Filtered)
        {
            m_X = x;
            m_Y = y;
            m_Z = z;
            m_Filtered = true;
        }
        else
        {
            m_X += kSmoothing * (x - m_X);
            m_Y += kSmoothing * (y - m_Y);
            m_Z += kSmoothing * (z - m_Z);
        }
    }
    if (!m_Filtered)
        return;

    // Held sideways with the pointer end to the left, the remote's left side
    // (+x) faces the player and its expansion end (+y) is on the right.
    const float rightAngle = atan2f(-m_Y, m_Z) * kDegrees;
    const float forwardAngle = atan2f(m_X, m_Z) * kDegrees;

    if (m_Centering)
    {
        // Level is where the hands come to rest, so wait for the remote to be still.
        const bool still = steady && fabsf(Wrap(atan2f(-y, z) * kDegrees - rightAngle)) < kStillness &&
                           fabsf(Wrap(atan2f(x, z) * kDegrees - forwardAngle)) < kStillness;
        if (still && fabsf(rightAngle) < kLevelLimit && fabsf(forwardAngle) < kLevelLimit)
            ++m_StillFrames;
        else
            m_StillFrames = 0;
        if (m_StillFrames < kStillFrames)
            return;
        m_LevelRight = rightAngle;
        m_LevelForward = forwardAngle;
        m_Centering = false;
    }

    const float pushRight = Push(Wrap(rightAngle - m_LevelRight));
    const float pushForward = Push(Wrap(forwardAngle - m_LevelForward));
    // A push that starts or turns around begins with a press.
    if (pushRight * m_Right <= 0.0f)
        m_RightPulse = 0.0f;
    if (pushForward * m_Forward <= 0.0f)
        m_ForwardPulse = 0.0f;
    m_Right = pushRight;
    m_Forward = pushForward;
    if (m_Right != 0.0f)
        *right = Pulse(m_Right, m_RightPulse);
    if (m_Forward != 0.0f)
        *forward = Pulse(m_Forward, m_ForwardPulse);
}
