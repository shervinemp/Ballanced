/**
 * @file VxSharedLibraryOgc.cpp
 * @brief VxSharedLibrary for the Nintendo Wii.
 *
 * The Wii has no dynamic loader: every Virtools module is linked into the
 * executable and registered through the Player's static plugin table, so
 * loading a library by name always fails.
 */

#include "VxSharedLibrary.h"

VxSharedLibrary::VxSharedLibrary() {
    m_LibraryHandle = NULL;
}

void VxSharedLibrary::Attach(INSTANCE_HANDLE LibraryHandle) {
    m_LibraryHandle = LibraryHandle;
}

INSTANCE_HANDLE VxSharedLibrary::Load(const char *LibraryName) {
    (void)LibraryName;
    m_LibraryHandle = NULL;
    return NULL;
}

void VxSharedLibrary::ReleaseLibrary() {
    m_LibraryHandle = NULL;
}

void *VxSharedLibrary::GetFunctionPtr(const char *FunctionName) {
    (void)FunctionName;
    return NULL;
}
