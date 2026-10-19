#ifndef WIIPHYSICSSHAPES_H
#define WIIPHYSICSSHAPES_H

#include "VxVector.h"

class CKMesh;
class IVP_Compact_Surface;

// The collision surfaces Physicalize builds, kept by the geometry they come
// from.
//
// physics_RT builds a level's collision surfaces (IVP compact surfaces) from
// its meshes each time the level loads: over a third of the game's work while
// Level 12 loads, in Dolphin. A compact surface is one block of memory that
// refers to itself only by offsets, and nothing changes it once it is built,
// so one built surface can serve every object with the same geometry, now and
// the next time the level loads. Surfaces are keyed by a 128-bit hash of what
// the build reads: the vertices and faces of the meshes, which of them are
// convex or concave, and the object's scale.
//
// Surfaces stay in memory while objects use them, and up to a budget of
// unused ones (from the level loaded before) after that. With STORAGE they are
// also written to a cache folder by a background thread and read back on the
// next start, when reading is quicker than building was. Cache files written
// by another build of the physics engine are discarded, and a record that does
// not check out is built again.
namespace wiiphysicsshapes
{
    enum Mode
    {
        OFF,     // Build every surface, as the PC does
        MEMORY,  // Keep surfaces in memory
        STORAGE  // In memory and in cache files
    };

    struct Settings
    {
        Mode CacheMode;
        unsigned int MemoryBudget;     // Bytes of surfaces no object uses that stay in memory
        unsigned int MemoryReserve;    // ... unless the heap has less than this left
        unsigned int (*FreeMemory)();  // Bytes the heap can still hand out (or NULL)
        const char *Folder;            // Cache files (STORAGE), e.g. "sd:/apps/ballance/Wii/Cache/"
        bool Verify;                   // Build reused surfaces anyway and compare them (slow)
        bool LogShapes;                // Log a line for each surface
        void (*Log)(const char *text);
    };

    // Applies settings. Cache files open when the first surface is needed.
    void Configure(const Settings &settings);
    // Finishes writing the cache files (for a few seconds at most) and closes
    // them. Call before unmounting the storage device.
    void Shutdown();

    // What CKIpionManager::CreatePhysicsObjectOnParameters builds a surface from.
    struct Source
    {
        int ConvexCount;
        CKMesh **Convexes;
        int ConcaveCount;
        CKMesh **Concaves;
        VxVector Scale;
    };
    typedef IVP_Compact_Surface *(*BuildFunction)(const Source &source, void *user);

    // The surface built from source: one in memory, one from the cache files
    // or build(source, user). Hand it back with Release, which also frees
    // surfaces the cache did not make.
    IVP_Compact_Surface *Acquire(const Source &source, const char *name, BuildFunction build, void *user);
    void Release(IVP_Compact_Surface *surface);

    struct Stats
    {
        unsigned int Built;       // Built from meshes
        unsigned int FromMemory;  // Reused from memory
        unsigned int FromStorage; // Read from the cache files
        unsigned int Skipped;     // Records built instead, as that was quicker than reading them
        unsigned int Rejected;    // Records in the cache files that did not check out
        unsigned int Written;     // Records written to the cache files
        unsigned int Verified;    // Reused surfaces compared with a fresh build
        unsigned int Mismatched;  // ... that differed from it
        unsigned long long BuildMicroseconds;
        unsigned long long MemoryMicroseconds;
        unsigned long long StorageMicroseconds;
        unsigned int BytesInUse;  // Surfaces objects use
        unsigned int BytesUnused; // Surfaces kept for later
        unsigned int StoredShapes;
    };
    void GetStats(Stats &stats);

    // Diagnostics and tests.
    struct Key
    {
        unsigned int Words[4];
    };
    inline bool operator==(const Key &a, const Key &b)
    {
        return a.Words[0] == b.Words[0] && a.Words[1] == b.Words[1] && a.Words[2] == b.Words[2] &&
               a.Words[3] == b.Words[3];
    }
    Key ComputeKey(const Source &source);
    // Hash of a surface's structure: its tree of spheres and boxes, its
    // ledges, triangles and points, and the offsets between them.
    Key HashSurface(const IVP_Compact_Surface *surface);
    // Frees the surfaces no object uses.
    void DropUnused();
}

#endif // WIIPHYSICSSHAPES_H
