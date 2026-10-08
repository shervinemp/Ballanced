// Virtools data and the game's scripts name files with Windows backslashes
// ("3D Entities\Menu.nmo", "sd:/apps/ballance\Sounds\"), but libfat only
// splits paths on '/'. The linker sends the C library's path functions here
// first (--wrap, see Source/WiiPlayer/CMakeLists.txt), so the engine, the
// plugins and the player can all use either separator.
//
// Link this file into each executable, like MainStack.c: the C library is
// scanned after the project's static libraries.

#include <dirent.h>
#include <reent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define WII_PATH_MAX 1024

static const char *ToSlashes(const char *path, char *buffer)
{
    if (!path || !strchr(path, '\\'))
        return path;
    const size_t length = strlen(path);
    if (length >= WII_PATH_MAX)
        return path;
    for (size_t i = 0; i <= length; ++i)
        buffer[i] = path[i] == '\\' ? '/' : path[i];
    return buffer;
}

int __real__open_r(struct _reent *r, const char *file, int flags, int mode);
int __wrap__open_r(struct _reent *r, const char *file, int flags, int mode)
{
    char buffer[WII_PATH_MAX];
    return __real__open_r(r, ToSlashes(file, buffer), flags, mode);
}

int __real__stat_r(struct _reent *r, const char *file, struct stat *st);
int __wrap__stat_r(struct _reent *r, const char *file, struct stat *st)
{
    char buffer[WII_PATH_MAX];
    return __real__stat_r(r, ToSlashes(file, buffer), st);
}

int __real_lstat(const char *path, struct stat *st);
int __wrap_lstat(const char *path, struct stat *st)
{
    char buffer[WII_PATH_MAX];
    return __real_lstat(ToSlashes(path, buffer), st);
}

int __real__unlink_r(struct _reent *r, const char *name);
int __wrap__unlink_r(struct _reent *r, const char *name)
{
    char buffer[WII_PATH_MAX];
    return __real__unlink_r(r, ToSlashes(name, buffer));
}

int __real__rename_r(struct _reent *r, const char *existing, const char *newName);
int __wrap__rename_r(struct _reent *r, const char *existing, const char *newName)
{
    char existingBuffer[WII_PATH_MAX];
    char newBuffer[WII_PATH_MAX];
    return __real__rename_r(r, ToSlashes(existing, existingBuffer), ToSlashes(newName, newBuffer));
}

int __real__rmdir_r(struct _reent *r, const char *name);
int __wrap__rmdir_r(struct _reent *r, const char *name)
{
    char buffer[WII_PATH_MAX];
    return __real__rmdir_r(r, ToSlashes(name, buffer));
}

int __real_mkdir(const char *path, mode_t mode);
int __wrap_mkdir(const char *path, mode_t mode)
{
    char buffer[WII_PATH_MAX];
    return __real_mkdir(ToSlashes(path, buffer), mode);
}

DIR *__real_opendir(const char *path);
DIR *__wrap_opendir(const char *path)
{
    char buffer[WII_PATH_MAX];
    return __real_opendir(ToSlashes(path, buffer));
}

int __real_chdir(const char *path);
int __wrap_chdir(const char *path)
{
    char buffer[WII_PATH_MAX];
    return __real_chdir(ToSlashes(path, buffer));
}

int __real_statvfs(const char *path, struct statvfs *buf);
int __wrap_statvfs(const char *path, struct statvfs *buf)
{
    char buffer[WII_PATH_MAX];
    return __real_statvfs(ToSlashes(path, buffer), buf);
}
