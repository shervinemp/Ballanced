/**
 * @file VxWindowFunctionsOgc.cpp
 * @brief VxWindowFunctions for the Nintendo Wii (libogc + newlib/libfat).
 *
 * There is no window system, cursor or message box on the console: the
 * "window" is the TV picture whose size the player publishes through
 * VxWiiSetDisplaySize. File functions go through newlib, which libfat backs
 * with the SD card ("sd:/") and USB storage ("usb:/").
 */

#include "VxWindowFunctions.h"
#include "VxWiiPlatform.h"

#include <ogc/system.h>

#include <ctype.h>
#include <dirent.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "VxImageDescEx.h"
#include "VxMath.h"
#include "XString.h"

// ============================================================================
// Console environment
// ============================================================================

static XString g_ApplicationPath;
static int g_DisplayWidth = 640;
static int g_DisplayHeight = 480;

void VxWiiSetApplicationPath(const char *path) {
    g_ApplicationPath = path ? path : "";
    const int length = g_ApplicationPath.Length();
    if (length > 0 && g_ApplicationPath[length - 1] != '/')
        g_ApplicationPath << '/';
}

const char *VxWiiGetApplicationPath() {
    return g_ApplicationPath.CStr();
}

void VxWiiSetDisplaySize(int width, int height) {
    if (width > 0 && height > 0) {
        g_DisplayWidth = width;
        g_DisplayHeight = height;
    }
}

void VxWiiGetDisplaySize(int *width, int *height) {
    if (width)
        *width = g_DisplayWidth;
    if (height)
        *height = g_DisplayHeight;
}

static XString VxApplicationDirectory() {
    if (g_ApplicationPath.Length() > 0)
        return g_ApplicationPath;
    XString cwd = VxGetCurrentDirectory();
    if (cwd.Length() > 0 && cwd[cwd.Length() - 1] != '/')
        cwd << '/';
    return cwd;
}

// ============================================================================
// Keyboard Functions (DirectInput scan codes, as stored by Virtools)
// ============================================================================

static XBOOL VxKeyStateActive(unsigned char keystate[256], XDWORD key) {
    return keystate && key < 256 && (keystate[key] & 0x81) != 0;
}

char VxScanCodeToAscii(XDWORD scancode, unsigned char keystate[256]) {
    static const char kDigits[] = "1234567890-=";
    static const char kShiftedDigits[] = "!@#$%^&*()_+";
    static const char kRow1[] = "qwertyuiop[]";
    static const char kShiftedRow1[] = "QWERTYUIOP{}";
    static const char kRow2[] = "asdfghjkl;'`";
    static const char kShiftedRow2[] = "ASDFGHJKL:\"~";
    static const char kRow3[] = "\\zxcvbnm,./";
    static const char kShiftedRow3[] = "|ZXCVBNM<>?";

    const XBOOL shift = VxKeyStateActive(keystate, 0x2A) || VxKeyStateActive(keystate, 0x36);
    const XBOOL caps = keystate && (keystate[0x3A] & 0x01) != 0;

    char c = '\0';
    if (scancode >= 0x02 && scancode <= 0x0D)
        c = shift ? kShiftedDigits[scancode - 0x02] : kDigits[scancode - 0x02];
    else if (scancode >= 0x10 && scancode <= 0x1B)
        c = shift ? kShiftedRow1[scancode - 0x10] : kRow1[scancode - 0x10];
    else if (scancode >= 0x1E && scancode <= 0x29)
        c = shift ? kShiftedRow2[scancode - 0x1E] : kRow2[scancode - 0x1E];
    else if (scancode >= 0x2B && scancode <= 0x35)
        c = shift ? kShiftedRow3[scancode - 0x2B] : kRow3[scancode - 0x2B];
    else if (scancode == 0x39)
        c = ' ';

    // Caps lock only flips letters.
    if (caps && isalpha((unsigned char)c))
        c = islower((unsigned char)c) ? (char)toupper((unsigned char)c) : (char)tolower((unsigned char)c);
    return c;
}

struct VxScanCodeName {
    XDWORD ScanCode;
    const char *Name;
};

static const VxScanCodeName kScanCodeNames[] = {
    {0x01, "Escape"}, {0x0E, "Backspace"}, {0x0F, "Tab"}, {0x1C, "Enter"},
    {0x1D, "Left Ctrl"}, {0x2A, "Left Shift"}, {0x36, "Right Shift"}, {0x37, "Numpad *"},
    {0x38, "Left Alt"}, {0x39, "Space"}, {0x3A, "Caps Lock"},
    {0x3B, "F1"}, {0x3C, "F2"}, {0x3D, "F3"}, {0x3E, "F4"}, {0x3F, "F5"}, {0x40, "F6"},
    {0x41, "F7"}, {0x42, "F8"}, {0x43, "F9"}, {0x44, "F10"}, {0x57, "F11"}, {0x58, "F12"},
    {0x45, "Num Lock"}, {0x46, "Scroll Lock"},
    {0x47, "Numpad 7"}, {0x48, "Numpad 8"}, {0x49, "Numpad 9"}, {0x4A, "Numpad -"},
    {0x4B, "Numpad 4"}, {0x4C, "Numpad 5"}, {0x4D, "Numpad 6"}, {0x4E, "Numpad +"},
    {0x4F, "Numpad 1"}, {0x50, "Numpad 2"}, {0x51, "Numpad 3"}, {0x52, "Numpad 0"},
    {0x53, "Numpad ."}, {0x8D, "Numpad ="}, {0x9C, "Numpad Enter"}, {0x9D, "Right Ctrl"},
    {0xB3, "Numpad ,"}, {0xB5, "Numpad /"}, {0xB8, "Right Alt"}, {0xC5, "Pause"},
    {0xC7, "Home"}, {0xC8, "Up Arrow"}, {0xC9, "PREVIOUS"}, {0xCB, "Left Arrow"},
    {0xCD, "Right Arrow"}, {0xCF, "End"}, {0xD0, "Down Arrow"}, {0xD1, "NEXT"},
    {0xD2, "Insert"}, {0xD3, "Delete"},
};

int VxScanCodeToName(XDWORD scancode, char *keyName) {
    if (!keyName)
        return 0;

    const char ascii = VxScanCodeToAscii(scancode, NULL);
    if (ascii >= 33 && ascii <= 126) {
        keyName[0] = (char)toupper((unsigned char)ascii);
        keyName[1] = '\0';
        return 2;
    }

    for (size_t i = 0; i < sizeof(kScanCodeNames) / sizeof(kScanCodeNames[0]); ++i) {
        if (kScanCodeNames[i].ScanCode == scancode) {
            strcpy(keyName, kScanCodeNames[i].Name);
            return (int)strlen(keyName) + 1;
        }
    }

    keyName[0] = '\0';
    return 1;
}

// ============================================================================
// Cursor Functions (the pointer is drawn by the game, not by the system)
// ============================================================================

int VxShowCursor(XBOOL show) {
    static int s_CursorDisplayCount = 0;
    s_CursorDisplayCount += show ? 1 : -1;
    return s_CursorDisplayCount;
}

XBOOL VxSetCursor(VXCURSOR_POINTER cursorID) {
    (void)cursorID;
    return TRUE;
}

// ============================================================================
// FPU Control Functions (x87 control words have no Broadway equivalent)
// ============================================================================

XWORD VxGetFPUControlWord() {
    return 0x027F;
}

void VxSetFPUControlWord(XWORD Fpu) {
    (void)Fpu;
}

void VxSetBaseFPUControlWord() {
}

// ============================================================================
// Environment Variable Functions
// ============================================================================

void VxAddLibrarySearchPath(const char *path) {
    (void)path; // Nothing is loaded dynamically on the Wii.
}

XBOOL VxGetEnvironmentVariable(const char *envName, XString &envValue) {
    const char *value = (envName && *envName) ? getenv(envName) : NULL;
    if (!value) {
        envValue = "";
        return FALSE;
    }
    envValue = value;
    return TRUE;
}

XBOOL VxSetEnvironmentVariable(const char *envName, const char *envValue) {
    if (!envName || !*envName)
        return FALSE;
    if (envValue)
        return setenv(envName, envValue, 1) == 0;
    return unsetenv(envName) == 0;
}

// ============================================================================
// Window Functions: the single "window" is the TV picture
// ============================================================================

WIN_HANDLE VxWindowFromPoint(CKPOINT pt) {
    (void)pt;
    return NULL;
}

XBOOL VxGetClientRect(WIN_HANDLE Win, CKRECT *rect) {
    (void)Win;
    if (!rect)
        return FALSE;
    rect->left = 0;
    rect->top = 0;
    rect->right = g_DisplayWidth;
    rect->bottom = g_DisplayHeight;
    return TRUE;
}

XBOOL VxGetWindowRect(WIN_HANDLE Win, CKRECT *rect) {
    return VxGetClientRect(Win, rect);
}

XBOOL VxScreenToClient(WIN_HANDLE Win, CKPOINT *pt) {
    (void)Win;
    return pt != NULL;
}

XBOOL VxClientToScreen(WIN_HANDLE Win, CKPOINT *pt) {
    (void)Win;
    return pt != NULL;
}

WIN_HANDLE VxSetParent(WIN_HANDLE Child, WIN_HANDLE Parent) {
    (void)Child;
    (void)Parent;
    return NULL;
}

WIN_HANDLE VxGetParent(WIN_HANDLE Win) {
    (void)Win;
    return NULL;
}

XBOOL VxMoveWindow(WIN_HANDLE Win, int x, int y, int Width, int Height, XBOOL Repaint) {
    (void)Win;
    (void)x;
    (void)y;
    (void)Width;
    (void)Height;
    (void)Repaint;
    return TRUE;
}

// ============================================================================
// Directory and Path Functions
// ============================================================================

static XString VxJoinPathString(const char *dir, const char *file) {
    XString result = dir ? dir : "";
    const int length = result.Length();
    if (length > 0 && result[length - 1] != '/' && result[length - 1] != '\\')
        result << '/';
    result << (file ? file : "");
    return result;
}

// Virtools data uses backslashes; libfat only understands '/'.
static XString VxNormalizePath(const char *path) {
    XString normalized = path ? path : "";
    for (int i = 0; i < normalized.Length(); ++i) {
        if (normalized[i] == '\\')
            normalized[i] = '/';
    }
    return normalized;
}

XString VxGetTempPath() {
    XString path = VxApplicationDirectory();
    path << "Temp/";
    mkdir(path.CStr(), 0777);
    return path;
}

XBOOL VxMakeDirectory(const char *path) {
    if (!path)
        return FALSE;
    XString normalized = VxNormalizePath(path);
    return mkdir(normalized.CStr(), 0777) == 0 || VxDirectoryExists(normalized.CStr());
}

XBOOL VxRemoveDirectory(const char *path) {
    if (!path)
        return FALSE;
    return rmdir(VxNormalizePath(path).CStr()) == 0;
}

static XBOOL VxDeleteDirectoryRecursive(const char *path) {
    DIR *dir = opendir(path);
    if (!dir)
        return FALSE;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        XString fullpath = VxJoinPathString(path, entry->d_name);
        struct stat st;
        if (stat(fullpath.CStr(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            VxDeleteDirectoryRecursive(fullpath.CStr());
        else
            unlink(fullpath.CStr());
    }
    closedir(dir);
    return rmdir(path) == 0;
}

XBOOL VxDeleteDirectory(const char *path) {
    if (!path || !*path)
        return FALSE;
    return VxDeleteDirectoryRecursive(VxNormalizePath(path).CStr());
}

XBOOL VxFileExists(const char *path) {
    struct stat st;
    return path && stat(VxNormalizePath(path).CStr(), &st) == 0 && S_ISREG(st.st_mode);
}

XBOOL VxDirectoryExists(const char *path) {
    struct stat st;
    return path && stat(VxNormalizePath(path).CStr(), &st) == 0 && S_ISDIR(st.st_mode);
}

XBOOL VxCopyFile(const char *src, const char *dst, XBOOL failIfExists) {
    if (!src || !dst)
        return FALSE;
    if (failIfExists && VxFileExists(dst))
        return FALSE;

    FILE *input = fopen(VxNormalizePath(src).CStr(), "rb");
    if (!input)
        return FALSE;
    FILE *output = fopen(VxNormalizePath(dst).CStr(), "wb");
    if (!output) {
        fclose(input);
        return FALSE;
    }

    const size_t bufferSize = 64 * 1024;
    char *buffer = (char *)malloc(bufferSize);
    XBOOL ok = buffer != NULL;
    size_t read = 0;
    while (ok && (read = fread(buffer, 1, bufferSize, input)) > 0) {
        if (fwrite(buffer, 1, read, output) != read)
            ok = FALSE;
    }
    if (ferror(input))
        ok = FALSE;

    free(buffer);
    fclose(output);
    fclose(input);
    return ok;
}

XBOOL VxDeleteFile(const char *path) {
    return path && unlink(VxNormalizePath(path).CStr()) == 0;
}

// Case-insensitive '*'/'?' matching, like the Win32 file system.
static XBOOL VxDirectoryNameMatches(const char *name, const char *mask) {
    if (!mask || !*mask)
        return TRUE;

    const char *n = name;
    const char *m = mask;
    const char *star = NULL;
    const char *retry = NULL;
    while (*n) {
        if (*m == '?' || tolower((unsigned char)*m) == tolower((unsigned char)*n)) {
            ++m;
            ++n;
        } else if (*m == '*') {
            star = m++;
            retry = n;
        } else if (star) {
            m = star + 1;
            n = ++retry;
        } else {
            return FALSE;
        }
    }
    while (*m == '*')
        ++m;
    return *m == '\0';
}

XBOOL VxListDirectory(const char *dir, const char *mask, XBOOL includeDirectories, VxDirectoryEntryCallback callback, void *userData) {
    if (!dir || !callback)
        return FALSE;

    XString directory = VxNormalizePath(dir);
    DIR *handle = opendir(directory.CStr());
    if (!handle)
        return FALSE;

    XBOOL ok = TRUE;
    struct dirent *entryData;
    while ((entryData = readdir(handle)) != NULL) {
        if (strcmp(entryData->d_name, ".") == 0 || strcmp(entryData->d_name, "..") == 0)
            continue;
        if (!VxDirectoryNameMatches(entryData->d_name, mask))
            continue;

        XString fullpath = VxJoinPathString(directory.CStr(), entryData->d_name);
        struct stat st;
        if (stat(fullpath.CStr(), &st) != 0)
            continue;

        const XBOOL isDirectory = S_ISDIR(st.st_mode) ? TRUE : FALSE;
        if (isDirectory && !includeDirectories)
            continue;

        VxDirectoryEntry entry;
        entry.Name = entryData->d_name;
        entry.IsDirectory = isDirectory;
        entry.Size = (size_t)st.st_size;
        if (!callback(&entry, userData)) {
            ok = FALSE;
            break;
        }
    }
    closedir(handle);
    return ok;
}

XBOOL VxGetCurrentDirectory(char *path, size_t pathSize) {
    if (!path || pathSize == 0)
        return FALSE;
    return getcwd(path, pathSize) != NULL;
}

XString VxGetCurrentDirectory() {
    char buffer[1024];
    if (!getcwd(buffer, sizeof(buffer)))
        return "";
    return buffer;
}

XBOOL VxGetApplicationBasePath(char *path, size_t pathSize) {
    if (!path || pathSize == 0)
        return FALSE;
    XString base = VxApplicationDirectory();
    if ((size_t)base.Length() + 1 > pathSize)
        return FALSE;
    memcpy(path, base.CStr(), base.Length() + 1);
    return TRUE;
}

XBOOL VxGetUserConfigPath(const char *appName, char *path, size_t pathSize) {
    if (!path || pathSize == 0)
        return FALSE;
    XString configPath;
    if (!VxGetUserConfigPath(appName, configPath))
        return FALSE;
    if ((size_t)configPath.Length() + 1 > pathSize) {
        path[0] = '\0';
        return FALSE;
    }
    memcpy(path, configPath.CStr(), configPath.Length() + 1);
    return TRUE;
}

// Homebrew keeps its settings next to the executable on the SD card.
XBOOL VxGetUserConfigPath(const char *appName, XString &path) {
    (void)appName;
    path = VxApplicationDirectory();
    return path.Length() > 0;
}

XBOOL VxSetCurrentDirectory(const char *path) {
    return path && chdir(VxNormalizePath(path).CStr()) == 0;
}

XBOOL VxMakePath(char *fullpath, size_t fullpathSize, const char *path, const char *file) {
    if (!path || !file || !fullpath || fullpathSize == 0)
        return FALSE;

    const size_t pathLen = strlen(path);
    const size_t fileLen = strlen(file);
    const XBOOL needSeparator = pathLen > 0 && path[pathLen - 1] != '/' && path[pathLen - 1] != '\\';
    const size_t totalLen = pathLen + (needSeparator ? 1 : 0) + fileLen;
    if (totalLen + 1 > fullpathSize)
        return FALSE;

    memcpy(fullpath, path, pathLen);
    size_t pos = pathLen;
    if (needSeparator)
        fullpath[pos++] = '/';
    memcpy(fullpath + pos, file, fileLen);
    fullpath[totalLen] = '\0';
    return TRUE;
}

XBOOL VxMakePath(XString &fullpath, const char *path, const char *file) {
    fullpath = "";
    if (!path || !file)
        return FALSE;
    fullpath = VxJoinPathString(path, file);
    return TRUE;
}

XBOOL VxTestDiskSpace(const char *dir, size_t size) {
    // libfat reports free space slowly (it scans the FAT), and the game only
    // writes small files; assume there is room.
    (void)dir;
    (void)size;
    return TRUE;
}

int VxMessageBox(WIN_HANDLE hWnd, const char *lpText, const char *lpCaption, XDWORD uType) {
    (void)hWnd;
    (void)uType;
    SYS_Report("[%s] %s\n", lpCaption ? lpCaption : "Message", lpText ? lpText : "");
    return 1; // IDOK
}

// ============================================================================
// Module Functions: one statically linked executable
// ============================================================================

XString VxGetModuleFileName(INSTANCE_HANDLE Handle) {
    (void)Handle;
    XString path = VxApplicationDirectory();
    path << "boot.dol";
    return path;
}

size_t VxGetModuleFileName(INSTANCE_HANDLE Handle, char *string, size_t StringSize) {
    if (!string || StringSize == 0)
        return 0;
    XString path = VxGetModuleFileName(Handle);
    const size_t length = (size_t)path.Length();
    const size_t copyLength = length < StringSize - 1 ? length : StringSize - 1;
    memcpy(string, path.CStr(), copyLength);
    string[copyLength] = '\0';
    return copyLength;
}

INSTANCE_HANDLE VxGetModuleHandle(const char *filename) {
    // Only the executable itself exists.
    return filename ? NULL : (INSTANCE_HANDLE)&g_ApplicationPath;
}

XBOOL VxCreateFileTree(const char *file) {
    if (!file || file[0] == '\0')
        return FALSE;

    XString filepath = VxNormalizePath(file);
    if (filepath.Length() <= 1)
        return FALSE;

    // Skip a device prefix such as "sd:/".
    int start = 1;
    const char *colon = strchr(filepath.CStr(), ':');
    if (colon && colon[1] == '/')
        start = (int)(colon - filepath.CStr()) + 2;

    for (int i = start; i < filepath.Length(); ++i) {
        if (filepath[i] != '/')
            continue;
        filepath[i] = '\0';
        struct stat st;
        if (stat(filepath.CStr(), &st) == 0) {
            if (!S_ISDIR(st.st_mode)) {
                filepath[i] = '/';
                return FALSE;
            }
        } else if (mkdir(filepath.CStr(), 0777) != 0) {
            filepath[i] = '/';
            return FALSE;
        }
        filepath[i] = '/';
    }
    return TRUE;
}

XDWORD VxURLDownloadToCacheFile(const char *File, char *CachedFile, int szCachedFile) {
    (void)File;
    if (CachedFile && szCachedFile > 0)
        CachedFile[0] = '\0';
    return 1; // No network downloads.
}

// ============================================================================
// Bitmap Functions: plain 32-bit ARGB images in memory
// ============================================================================

struct VxOgcBitmap {
    int Width;
    int Height;
    XBYTE *Pixels;
};

static void VxOgcBitmapDesc(const VxOgcBitmap *bitmap, VxImageDescEx &desc) {
    desc = VxImageDescEx();
    desc.Width = bitmap->Width;
    desc.Height = bitmap->Height;
    desc.BitsPerPixel = 32;
    desc.BytesPerLine = bitmap->Width * 4;
    desc.AlphaMask = A_MASK;
    desc.RedMask = R_MASK;
    desc.GreenMask = G_MASK;
    desc.BlueMask = B_MASK;
    desc.Image = bitmap->Pixels;
}

BITMAP_HANDLE VxCreateBitmap(const VxImageDescEx &desc) {
    if (desc.Width <= 0 || desc.Height <= 0)
        return NULL;

    VxOgcBitmap *bitmap = new VxOgcBitmap;
    bitmap->Width = desc.Width;
    bitmap->Height = desc.Height;
    bitmap->Pixels = new XBYTE[(size_t)desc.Width * (size_t)desc.Height * 4];
    memset(bitmap->Pixels, 0, (size_t)desc.Width * (size_t)desc.Height * 4);

    if (desc.Image) {
        VxImageDescEx dst;
        VxOgcBitmapDesc(bitmap, dst);
        VxDoBlit(desc, dst);
    }
    return (BITMAP_HANDLE)bitmap;
}

void VxDeleteBitmap(BITMAP_HANDLE Bitmap) {
    VxOgcBitmap *bitmap = (VxOgcBitmap *)Bitmap;
    if (bitmap) {
        delete[] bitmap->Pixels;
        delete bitmap;
    }
}

XBYTE *VxConvertBitmap(BITMAP_HANDLE Bitmap, VxImageDescEx &desc) {
    VxOgcBitmap *bitmap = (VxOgcBitmap *)Bitmap;
    if (!bitmap)
        return NULL;

    VxImageDescEx src;
    VxOgcBitmapDesc(bitmap, src);
    const size_t size = (size_t)src.BytesPerLine * (size_t)src.Height;
    XBYTE *image = new XBYTE[size];
    memcpy(image, src.Image, size);

    desc = src;
    desc.Image = image;
    return image;
}

BITMAP_HANDLE VxConvertBitmapTo24(BITMAP_HANDLE Bitmap) {
    // Bitmaps are always kept as 32-bit; callers only read them back.
    return Bitmap;
}

XBOOL VxCopyBitmap(BITMAP_HANDLE Bitmap, const VxImageDescEx &desc) {
    VxOgcBitmap *bitmap = (VxOgcBitmap *)Bitmap;
    if (!bitmap || !desc.Image)
        return FALSE;

    VxImageDescEx src;
    VxOgcBitmapDesc(bitmap, src);
    VxDoBlit(src, desc);
    return TRUE;
}

// ============================================================================
// System information
// ============================================================================

VX_OSINFO VxGetOs() {
    return VXOS_WII;
}

XBOOL VxGetMemoryStatus(VxMemoryStatus *status) {
    if (!status)
        return FALSE;
    memset(status, 0, sizeof(VxMemoryStatus));

    // MEM1 (24 MB) and MEM2 (64 MB) both back the heap.
    const uint64_t total = (uint64_t)SYS_GetArena1Size() + (uint64_t)SYS_GetArena2Size();
    const struct mallinfo info = mallinfo();
    status->TotalPhysical = total + (uint64_t)info.arena;
    status->AvailablePhysical = total + (uint64_t)info.fordblks;
    status->TotalVirtual = status->TotalPhysical;
    status->AvailableVirtual = status->AvailablePhysical;
    if (status->TotalPhysical > 0)
        status->MemoryLoad = (XDWORD)(100 - (status->AvailablePhysical * 100) / status->TotalPhysical);
    return TRUE;
}

// ============================================================================
// Font Functions: no system fonts; the game draws text from its own textures
// ============================================================================

FONT_HANDLE VxCreateFont(const char *FontName, int FontSize, int Weight, XBOOL italic, XBOOL underline) {
    (void)FontName;
    (void)FontSize;
    (void)Weight;
    (void)italic;
    (void)underline;
    return NULL;
}

XBOOL VxGetFontInfo(FONT_HANDLE Font, VXFONTINFO &desc) {
    (void)Font;
    desc = VXFONTINFO();
    return FALSE;
}

XBOOL VxDrawBitmapText(BITMAP_HANDLE Bitmap, FONT_HANDLE Font, const char *string, CKRECT *rect, XDWORD Align, XDWORD BkColor, XDWORD FontColor) {
    (void)Bitmap;
    (void)Font;
    (void)string;
    (void)rect;
    (void)Align;
    (void)BkColor;
    (void)FontColor;
    return FALSE;
}

void VxDeleteFont(FONT_HANDLE Font) {
    (void)Font;
}
