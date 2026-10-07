#ifndef BALLANCE_WII_DLFCN_H
#define BALLANCE_WII_DLFCN_H

/*
 * The Wii has no dynamic loader: every module is linked into the executable.
 * This header lets POSIX code that probes for modules compile; every lookup fails.
 */

#define RTLD_LAZY   0x0001
#define RTLD_NOW    0x0002
#define RTLD_GLOBAL 0x0100
#define RTLD_LOCAL  0x0000
#define RTLD_NOLOAD 0x0004

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

static inline void *dlopen(const char *file, int mode) {
    (void)file;
    (void)mode;
    return 0;
}

static inline void *dlsym(void *handle, const char *name) {
    (void)handle;
    (void)name;
    return 0;
}

static inline int dlclose(void *handle) {
    (void)handle;
    return 0;
}

static inline const char *dlerror(void) {
    return "dynamic loading is not available on the Wii";
}

static inline int dladdr(const void *address, Dl_info *info) {
    (void)address;
    (void)info;
    return 0;
}

#endif /* BALLANCE_WII_DLFCN_H */
