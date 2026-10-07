/**
 * @file VxMutexOgc.cpp
 * @brief libogc implementation of VxMutex for the Nintendo Wii.
 *
 * Virtools mutexes are re-entrant (as SDL3 and Win32 critical sections are),
 * so the LWP mutex is created recursive.
 */

#include "VxMutex.h"

#include <ogc/mutex.h>

VxMutex::VxMutex() {
    mutex_t *mutex = new mutex_t;
    if (LWP_MutexInit(mutex, true) != 0) {
        delete mutex;
        mutex = NULL;
    }
    m_Mutex = mutex;
}

VxMutex::~VxMutex() {
    mutex_t *mutex = (mutex_t *)m_Mutex;
    if (mutex) {
        LWP_MutexDestroy(*mutex);
        delete mutex;
    }
}

XBOOL VxMutex::EnterMutex() {
    mutex_t *mutex = (mutex_t *)m_Mutex;
    if (!mutex)
        return FALSE;
    return LWP_MutexLock(*mutex) == 0;
}

XBOOL VxMutex::LeaveMutex() {
    mutex_t *mutex = (mutex_t *)m_Mutex;
    if (!mutex)
        return FALSE;
    return LWP_MutexUnlock(*mutex) == 0;
}

XBOOL VxMutex::operator++(int) {
    return EnterMutex();
}

XBOOL VxMutex::operator--(int) {
    return LeaveMutex();
}
