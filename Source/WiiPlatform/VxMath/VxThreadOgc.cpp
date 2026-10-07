/**
 * @file VxThreadOgc.cpp
 * @brief libogc (LWP) implementation of VxThread for the Nintendo Wii.
 */

#include "VxThread.h"

#include <ogc/lwp.h>
#include <stdint.h>

namespace {

const u32 kThreadStackSize = 128 * 1024;

// Per-thread bookkeeping. It outlives the VxThread handle when a running thread
// is closed without being joined, because the thread still writes to it.
struct VxOgcThread {
    lwp_t Handle;
    VxThread *Owner;
    unsigned int ExitCode;
    volatile bool Finished;
    bool Joined;
};

u8 VxOgcPriority(unsigned int priority) {
    switch (priority) {
    case VXTP_IDLE:         return 8;
    case VXTP_LOWLEVEL:     return 40;
    case VXTP_BELOWNORMAL:  return 56;
    case VXTP_ABOVENORMAL:  return 72;
    case VXTP_HIGHLEVEL:    return 88;
    case VXTP_TIMECRITICAL: return 100;
    case VXTP_NORMAL:
    default:                return 64;
    }
}

VxOgcThread *VxOgcThreadOf(GENERIC_HANDLE handle) {
    return (VxOgcThread *)handle;
}

} // namespace

VxThread *VxThread::m_MainThread = NULL;

VxThread::VxThread() : m_Name(), m_Thread(NULL), m_ThreadID(0), m_Priority(0), m_Func(NULL), m_Args(NULL) {
    m_State = VXTS_JOINABLE;
}

VxThread::~VxThread() {
    GetMutex().EnterMutex();
    GetHashThread().Remove(m_Thread);
    GetMutex().LeaveMutex();
    Close();
}

unsigned long VX_STDCALL VxThread::ThreadFunc(void *args) {
    if (!args)
        return VXTERROR_NULLTHREAD;

    VxThread *thread = (VxThread *)args;
    thread->m_State |= VXTS_STARTED;

    XDWORD ret;
    if (thread->m_Func)
        ret = thread->m_Func(thread->m_Args);
    else
        ret = thread->Run();

    thread->m_State = VXTS_INITIALE;
    return ret;
}

void *VxThread::ThreadEntryPoint(void *args) {
    VxOgcThread *record = (VxOgcThread *)args;
    VxThread *thread = record->Owner;

    unsigned int ret = VXTERROR_NULLTHREAD;
    if (thread) {
        thread->m_State |= VXTS_STARTED;
        ret = thread->m_Func ? thread->m_Func(thread->m_Args) : thread->Run();
        thread->m_State &= ~VXTS_STARTED;
    }

    record->ExitCode = ret;
    record->Finished = true;
    return (void *)(uintptr_t)ret;
}

XBOOL VxThread::CreateThread(VxThreadFunction *func, void *args) {
    if (IsCreated())
        return TRUE;

    m_Func = func;
    m_Args = args;

    VxOgcThread *record = new VxOgcThread();
    record->Handle = LWP_THREAD_NULL;
    record->Owner = this;
    record->ExitCode = 0;
    record->Finished = false;
    record->Joined = false;

    // Publish the handle before the thread can look itself up.
    m_Thread = (GENERIC_HANDLE)record;
    m_State |= VXTS_CREATED;
    if (m_Name.Length() == 0) {
        m_Name = "THREAD_";
        m_Name << (int)(intptr_t)record;
    }
    GetMutex().EnterMutex();
    GetHashThread().Insert(m_Thread, this);
    GetMutex().LeaveMutex();

    if (LWP_CreateThread(&record->Handle, ThreadEntryPoint, record, NULL, kThreadStackSize, VxOgcPriority(m_Priority)) != 0) {
        GetMutex().EnterMutex();
        GetHashThread().Remove(m_Thread);
        GetMutex().LeaveMutex();
        delete record;
        m_Thread = NULL;
        m_State &= ~VXTS_CREATED;
        return FALSE;
    }

    m_ThreadID = (XUINTPTR)record->Handle;
    return TRUE;
}

void VxThread::SetPriority(unsigned int priority) {
    m_Priority = priority;
    if (IsCreated())
        SetPriority();
}

void VxThread::SetName(const char *name) {
    m_Name = name;
}

void VxThread::Close() {
    VxOgcThread *record = VxOgcThreadOf(m_Thread);
    if (record) {
        if (record->Joined || record->Finished) {
            if (!record->Joined)
                LWP_JoinThread(record->Handle, NULL);
            delete record;
        } else {
            // Still running: let it finish on its own, like a detached thread.
            record->Owner = NULL;
        }
    }
    m_ThreadID = 0;
    m_Thread = NULL;
    m_State = 0;
    m_Priority = 0;
    m_Func = NULL;
    m_Args = NULL;
}

const XString &VxThread::GetName() const {
    return m_Name;
}

unsigned int VxThread::GetPriority() const {
    return m_Priority;
}

XBOOL VxThread::IsCreated() const {
    return (m_State & VXTS_CREATED) != 0;
}

XBOOL VxThread::IsJoinable() const {
    return (m_State & VXTS_JOINABLE) != 0;
}

XBOOL VxThread::IsMainThread() const {
    return (m_State & VXTS_MAIN) != 0;
}

XBOOL VxThread::IsStarted() const {
    return (m_State & VXTS_STARTED) != 0;
}

VxThread *VxThread::GetCurrentVxThread() {
    const lwp_t self = LWP_GetSelf();

    GetMutex().EnterMutex();
    XHashTable<VxThread *, GENERIC_HANDLE>::Iterator it = GetHashThread().Begin();
    for (; it != GetHashThread().End(); ++it) {
        VxThread *thread = *it;
        VxOgcThread *record = thread ? VxOgcThreadOf(thread->m_Thread) : NULL;
        if (record && record->Handle == self) {
            GetMutex().LeaveMutex();
            return thread;
        }
    }
    GetMutex().LeaveMutex();
    return NULL;
}

int VxThread::Wait(unsigned int *status, unsigned int timeout) {
    (void)timeout; // LWP joins have no timeout.

    VxOgcThread *record = VxOgcThreadOf(m_Thread);
    if (!record)
        return VXTERROR_NULLTHREAD;

    if (!record->Joined) {
        if (LWP_JoinThread(record->Handle, NULL) != 0)
            return VXTERROR_WAIT;
        record->Joined = true;
    }

    if (status)
        *status = record->ExitCode;
    return VXT_OK;
}

const GENERIC_HANDLE VxThread::GetHandle() const {
    return m_Thread;
}

XUINTPTR VxThread::GetID() const {
    return m_ThreadID;
}

XBOOL VxThread::GetExitCode(unsigned int &status) {
    VxOgcThread *record = VxOgcThreadOf(m_Thread);
    if (!record)
        return FALSE;
    status = record->Finished ? record->ExitCode : VXT_STILLACTIVE;
    return TRUE;
}

XBOOL VxThread::Terminate(unsigned int *status) {
    // LWP threads cannot be killed safely.
    if (status)
        *status = VXT_TERMINATEFORCED;
    return FALSE;
}

XUINTPTR VxThread::GetCurrentVxThreadId() {
    return (XUINTPTR)LWP_GetSelf();
}

void VxThread::SetPriority() {
    VxOgcThread *record = VxOgcThreadOf(m_Thread);
    if (record && record->Handle != LWP_THREAD_NULL)
        LWP_SetThreadPriority(record->Handle, VxOgcPriority(m_Priority));
}

VxMutex &VxThread::GetMutex() {
    static VxMutex threadMutex;
    return threadMutex;
}

XHashTable<VxThread *, GENERIC_HANDLE> &VxThread::GetHashThread() {
    static XHashTable<VxThread *, GENERIC_HANDLE> hashThread;
    return hashThread;
}
