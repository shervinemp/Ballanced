#include "WiiPhysicsShapes.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unordered_map>
#include <vector>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include "CKAll.h"

#include "ivp_physics.hxx"
#include "ivp_compact_ledge.hxx"
#include "ivp_compact_surface.hxx"
#include "ivp_surbuild_ledge_soup.hxx"
#include "ivp_surbuild_pointsoup.hxx"

// Identifies the physics engine sources and compiler this was built with
// (cmake/WiiPort.cmake): cache files written by another build are discarded.
#ifndef WII_PHYSICS_SHAPES_BUILDER
#define WII_PHYSICS_SHAPES_BUILDER "unknown"
#endif

namespace wiiphysicsshapes
{
namespace
{
    // Bump when the key or the file layout changes.
    const u32 kKeyVersion = 1;
    const u32 kFormatVersion = 2;

    // Cache files: a data file of records (a record header, then the surface)
    // and an index of the records, each opening with a FileHeader. Records
    // start on 32-byte boundaries, so they read straight into aligned memory.
    const char kDataFile[] = "PhysicsShapes.dat";
    const char kIndexFile[] = "PhysicsShapes.idx";
    const u32 kDataMagic = 0x42505344;   // "BPSD"
    const u32 kIndexMagic = 0x42505349;  // "BPSI"
    const u32 kRecordMagic = 0x53485031; // "SHP1"
    const u32 kMaxDataBytes = 64u << 20;
    const u32 kWriteChunk = 32u << 10;   // The longest a read waits for a write
    const u32 kPending = 0xFFFFFFFFu;    // Offset of a record not written yet
    const u32 kShutdownSeconds = 5;
    const u8 kWriterPriority = 20;       // Below the game's threads: it writes while they wait
    const u32 kReadSample = 32u << 10;   // Bytes read before reads are judged by their speed

    struct FileHeader
    {
        u32 Magic;
        u32 Version;
        Key Builder;     // WII_PHYSICS_SHAPES_BUILDER
        Key Fingerprint; // A reference surface as this build makes it
        u32 Layout;      // Sizes of the compact structures
        u32 Stamp;       // Pairs a data file with its index
        u32 Reserved[4];
    };
    struct RecordHeader
    {
        u32 Magic;
        u32 Size;
        Key Id;
        u32 Check;
        u32 BuildMicroseconds; // How long the surface took to build
    };
    struct IndexEntry
    {
        Key Id;
        u32 Offset;
        u32 Size;
        u32 Check;
        u32 BuildMicroseconds;
    };
    typedef char FileHeaderSize[sizeof(FileHeader) == 64 ? 1 : -1];
    typedef char RecordHeaderSize[sizeof(RecordHeader) == 32 ? 1 : -1];
    typedef char IndexEntrySize[sizeof(IndexEntry) == 32 ? 1 : -1];

    inline u32 Rotl(u32 x, int r)
    {
        return (x << r) | (x >> (32 - r));
    }

    inline u32 Fmix(u32 h)
    {
        h ^= h >> 16;
        h *= 0x85ebca6b;
        h ^= h >> 13;
        h *= 0xc2b2ae35;
        h ^= h >> 16;
        return h;
    }

    // MurmurHash3 (x86, 128-bit) over a stream of 32-bit words.
    class Hasher
    {
    public:
        Hasher() : m_Count(0), m_Words(0)
        {
            m_H[0] = m_H[1] = m_H[2] = m_H[3] = 0x5eed1e55;
        }

        inline void Word(u32 k)
        {
            m_Block[m_Count++] = k;
            ++m_Words;
            if (m_Count == 4)
            {
                Mix();
                m_Count = 0;
            }
        }

        inline void Float(float f)
        {
            u32 bits;
            memcpy(&bits, &f, sizeof(bits));
            Word(bits);
        }

        void Words(const void *data, u32 count)
        {
            const u32 *words = (const u32 *)data;
            for (u32 i = 0; i < count; ++i)
                Word(words[i]);
        }

        Key Finish()
        {
            u32 h1 = m_H[0], h2 = m_H[1], h3 = m_H[2], h4 = m_H[3];
            u32 k;
            switch (m_Count)
            {
            case 3:
                k = m_Block[2] * kC3;
                h3 ^= Rotl(k, 17) * kC4;
                // fall through
            case 2:
                k = m_Block[1] * kC2;
                h2 ^= Rotl(k, 16) * kC3;
                // fall through
            case 1:
                k = m_Block[0] * kC1;
                h1 ^= Rotl(k, 15) * kC2;
                break;
            default:
                break;
            }
            const u32 length = m_Words * 4;
            h1 ^= length;
            h2 ^= length;
            h3 ^= length;
            h4 ^= length;
            h1 += h2 + h3 + h4;
            h2 += h1;
            h3 += h1;
            h4 += h1;
            h1 = Fmix(h1);
            h2 = Fmix(h2);
            h3 = Fmix(h3);
            h4 = Fmix(h4);
            h1 += h2 + h3 + h4;
            h2 += h1;
            h3 += h1;
            h4 += h1;
            Key key = {{h1, h2, h3, h4}};
            return key;
        }

    private:
        static const u32 kC1 = 0x239b961b, kC2 = 0xab0e9789, kC3 = 0x38b34ae5, kC4 = 0xa1e38b93;

        void Mix()
        {
            u32 k1 = m_Block[0], k2 = m_Block[1], k3 = m_Block[2], k4 = m_Block[3];
            u32 &h1 = m_H[0], &h2 = m_H[1], &h3 = m_H[2], &h4 = m_H[3];
            h1 ^= Rotl(k1 * kC1, 15) * kC2;
            h1 = (Rotl(h1, 19) + h2) * 5 + 0x561ccd1b;
            h2 ^= Rotl(k2 * kC2, 16) * kC3;
            h2 = (Rotl(h2, 17) + h3) * 5 + 0x0bcaa747;
            h3 ^= Rotl(k3 * kC3, 17) * kC4;
            h3 = (Rotl(h3, 15) + h4) * 5 + 0x96cd1c35;
            h4 ^= Rotl(k4 * kC4, 18) * kC1;
            h4 = (Rotl(h4, 13) + h1) * 5 + 0x32ac3b17;
        }

        u32 m_H[4];
        u32 m_Block[4];
        int m_Count;
        u32 m_Words;
    };

    struct KeyHash
    {
        size_t operator()(const Key &key) const { return key.Words[0]; }
    };

    // A surface in memory.
    struct Entry
    {
        Key Id;
        IVP_Compact_Surface *Surface;
        void *Allocation; // What to free: the surface, or the record it was read with
        u32 Size;
        u32 BuildMicroseconds;
        int Users;        // Objects using it, and the writer while it is queued
        Entry *Prev, *Next; // Unused entries, least recently used first
    };

    struct Stored
    {
        u32 Offset; // kPending while the writer has it
        u32 Size;
        u32 Check;
        u32 BuildMicroseconds;
    };

    struct State
    {
        bool Initialized;
        Settings Config;
        char Folder[256];

        mutex_t Lock; // Everything below but the files
        std::unordered_map<Key, Entry *, KeyHash> Entries;
        std::unordered_map<const IVP_Compact_Surface *, Entry *> BySurface;
        Entry *UnusedFirst, *UnusedLast;
        u32 BytesInUse, BytesUnused;
        Stats Counters;

        // Cache files.
        bool StorageTried, StorageOpen;
        volatile bool Writable;
        int DataFd, IndexFd;
        u32 DataSize; // Where the next record goes (the writer's)
        u64 ReadBytes, ReadMicroseconds; // Records read so far, for their speed
        mutex_t FileLock; // DataFd: the game reads while the writer writes
        std::unordered_map<Key, Stored, KeyHash> StoredShapes;

        // The writer thread.
        bool WriterRunning;
        lwp_t Writer;
        sem_t Pending;
        std::vector<Entry *> Queue;
        volatile bool Stopping;
        u64 Deadline;
        volatile int WriteError; // errno of a failed write, for the game's thread to log
    };

    State g;

    void Logf(const char *format, ...)
    {
        if (!g.Config.Log)
            return;
        char text[256];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        g.Config.Log(text);
    }

    void EnsureInit()
    {
        if (g.Initialized)
            return;
        g.Initialized = true;
        LWP_MutexInit(&g.Lock, false);
        LWP_MutexInit(&g.FileLock, false);
        g.Config.CacheMode = MEMORY;
        g.Config.MemoryBudget = 0;
        g.Config.FreeMemory = NULL;
        g.DataFd = g.IndexFd = -1;
    }

    // --- Hashing ---

    void HashPositions(Hasher &hasher, CKMesh *mesh)
    {
        const int count = mesh->GetVertexCount();
        CKDWORD stride = 0;
        const CKBYTE *positions = count > 0 ? (const CKBYTE *)mesh->GetPositionsPtr(&stride) : NULL;
        if (!positions)
        {
            hasher.Word(0);
            return;
        }
        hasher.Word((u32)count);
        for (int i = 0; i < count; ++i, positions += stride)
            hasher.Words(positions, 3);
    }

    void HashLedge(Hasher &hasher, const IVP_Compact_Surface *surface, const IVP_Compact_Ledge *ledge)
    {
        const char *base = (const char *)ledge;
        const IVP_Compact_Poly_Point *points = ledge->get_point_array();
        const int triangles = ledge->get_n_triangles();
        hasher.Word((u32)(base - (const char *)surface));
        hasher.Word((u32)((const char *)points - base));
        hasher.Word((u32)ledge->get_size());
        hasher.Word((u32)triangles);
        hasher.Word(const_cast<IVP_Compact_Ledge *>(ledge)->is_compact());
        if (ledge->is_terminal())
            hasher.Word((u32)ledge->get_client_data());
        else
            hasher.Word(0x80000000u | (u32)((const char *)ledge->get_ledgetree_node() - base));

        const IVP_Compact_Triangle *triangle = ledge->get_first_triangle();
        for (int t = 0; t < triangles; ++t, triangle = triangle->get_next_tri())
        {
            hasher.Word((u32)triangle->get_tri_index() | ((u32)triangle->get_pierce_index() << 12) |
                        ((u32)triangle->get_material_index() << 24) | ((u32)triangle->get_is_virtual() << 31));
            for (int e = 0; e < 3; ++e)
            {
                const IVP_Compact_Edge *edge = triangle->get_edge(e);
                const int index = edge->get_start_point_index();
                hasher.Word((u32)index | (((u32)edge->get_opposite_index() & 0x7fff) << 16) |
                            ((u32)edge->get_is_virtual() << 31));
                const IVP_Compact_Poly_Point *point = &points[index];
                hasher.Float(point->k[0]);
                hasher.Float(point->k[1]);
                hasher.Float(point->k[2]);
            }
        }
    }

    void HashNode(Hasher &hasher, const IVP_Compact_Surface *surface, const IVP_Compact_Ledgetree_Node *node)
    {
        hasher.Word((u32)node->offset_right_node);
        hasher.Word((u32)node->offset_compact_ledge);
        hasher.Float(node->center.k[0]);
        hasher.Float(node->center.k[1]);
        hasher.Float(node->center.k[2]);
        hasher.Float(node->radius);
        hasher.Word((u32)node->box_sizes[0] | ((u32)node->box_sizes[1] << 8) | ((u32)node->box_sizes[2] << 16) |
                    ((u32)node->free_0 << 24));
        if (node->is_terminal())
        {
            HashLedge(hasher, surface, node->get_compact_ledge());
            return;
        }
        if (node->offset_compact_ledge)
            HashLedge(hasher, surface, node->get_compact_hull());
        HashNode(hasher, surface, node->left_son());
        HashNode(hasher, surface, node->right_son());
    }

    u32 Checksum(const IVP_Compact_Surface *surface, u32 size)
    {
        Hasher hasher;
        hasher.Words(surface, size / 4);
        return hasher.Finish().Words[0];
    }

    // A small level floor and a convex block, as this build makes them.
    Key ReferenceFingerprint()
    {
        IVP_SurfaceBuilder_Ledge_Soup builder;
        const int n = 12;
        IVP_U_Point grid[(n + 1) * (n + 1)];
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j)
                grid[i * (n + 1) + j].set(i * 1.5f - 9.0f, ((i * 7 + j * 13) % 11) * 0.25f, j * 1.25f - 7.5f);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                const int a = i * (n + 1) + j, b = a + 1, c = a + n + 1, d = c + 1;
                const int faces[2][3] = {{a, b, d}, {a, d, c}};
                for (int f = 0; f < 2; ++f)
                {
                    IVP_U_Vector<IVP_U_Point> points(3);
                    for (int v = 0; v < 3; ++v)
                        points.add(&grid[faces[f][v]]);
                    IVP_Compact_Ledge *ledge = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&points);
                    if (ledge)
                        builder.insert_ledge(ledge);
                }
            }
        static const float block[9][3] = {{0, 4, 0}, {2, 4, 0}, {2, 6, 0}, {0, 6, 0}, {0, 4, 3},
                                          {2, 4, 3}, {2, 5.5f, 3}, {0.5f, 6, 3}, {1, 6.5f, 1.5f}};
        IVP_U_Point corners[9];
        IVP_U_Vector<IVP_U_Point> hull(9);
        for (int i = 0; i < 9; ++i)
        {
            corners[i].set(block[i][0], block[i][1], block[i][2]);
            hull.add(&corners[i]);
        }
        IVP_Compact_Ledge *ledge = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&hull);
        if (ledge)
            builder.insert_ledge(ledge);

        IVP_Compact_Surface *surface = builder.compile();
        Key key = {{0, 0, 0, 0}};
        if (surface)
        {
            key = HashSurface(surface);
            ivp_free_aligned(surface);
        }
        return key;
    }

    Key BuilderId()
    {
        Hasher hasher;
        const char *id = WII_PHYSICS_SHAPES_BUILDER;
        for (const char *c = id; *c; ++c)
            hasher.Word((u8)*c);
        return hasher.Finish();
    }

    // --- Memory ---

    void Unlink(Entry *entry)
    {
        if (entry->Prev)
            entry->Prev->Next = entry->Next;
        else
            g.UnusedFirst = entry->Next;
        if (entry->Next)
            entry->Next->Prev = entry->Prev;
        else
            g.UnusedLast = entry->Prev;
        entry->Prev = entry->Next = NULL;
    }

    void Free(Entry *entry)
    {
        g.Entries.erase(entry->Id);
        g.BySurface.erase(entry->Surface);
        ivp_free_aligned(entry->Allocation);
        delete entry;
    }

    // With g.Lock held.
    void EvictUnused(u32 budget)
    {
        while (g.BytesUnused > budget && g.UnusedFirst)
        {
            Entry *entry = g.UnusedFirst;
            Unlink(entry);
            g.BytesUnused -= entry->Size;
            Free(entry);
        }
    }

    // With g.Lock held.
    void Use(Entry *entry)
    {
        if (entry->Users++ == 0)
        {
            Unlink(entry);
            g.BytesUnused -= entry->Size;
            g.BytesInUse += entry->Size;
        }
    }

    // With g.Lock held.
    void Unuse(Entry *entry)
    {
        if (--entry->Users > 0)
            return;
        g.BytesInUse -= entry->Size;
        g.BytesUnused += entry->Size;
        entry->Prev = g.UnusedLast;
        entry->Next = NULL;
        if (g.UnusedLast)
            g.UnusedLast->Next = entry;
        else
            g.UnusedFirst = entry;
        g.UnusedLast = entry;
        EvictUnused(g.Config.CacheMode == OFF ? 0 : g.Config.MemoryBudget);
    }

    // With g.Lock held: a new entry with one user.
    Entry *Insert(const Key &id, IVP_Compact_Surface *surface, void *allocation)
    {
        Entry *entry = new Entry;
        entry->Id = id;
        entry->Surface = surface;
        entry->Allocation = allocation;
        entry->Size = (u32)surface->get_size();
        entry->BuildMicroseconds = 0;
        entry->Users = 1;
        entry->Prev = entry->Next = NULL;
        g.Entries[id] = entry;
        g.BySurface[surface] = entry;
        g.BytesInUse += entry->Size;
        return entry;
    }

    // --- Cache files ---

    bool ReadAll(int fd, void *buffer, u32 size)
    {
        char *p = (char *)buffer;
        while (size > 0)
        {
            const int got = read(fd, p, size);
            if (got <= 0)
                return false;
            p += got;
            size -= (u32)got;
        }
        return true;
    }

    bool WriteAll(int fd, const void *buffer, u32 size)
    {
        const char *p = (const char *)buffer;
        while (size > 0)
        {
            const int put = write(fd, p, size);
            if (put <= 0)
                return false;
            p += put;
            size -= (u32)put;
        }
        return true;
    }

    void MakeFolders(const char *folder)
    {
        char path[256];
        strncpy(path, folder, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        // Skip the device ("sd:/"), then create each folder on the way.
        char *p = strchr(path, ':');
        p = p ? p + 1 : path;
        while (*p == '/' || *p == '\\')
            ++p;
        for (; *p; ++p)
        {
            if (*p != '/' && *p != '\\')
                continue;
            const char separator = *p;
            *p = '\0';
            mkdir(path, 0777);
            *p = separator;
        }
    }

    u32 Layout()
    {
        return (u32)sizeof(IVP_Compact_Surface) | ((u32)sizeof(IVP_Compact_Ledge) << 8) |
               ((u32)sizeof(IVP_Compact_Ledgetree_Node) << 16) | ((u32)sizeof(IVP_Compact_Poly_Point) << 24);
    }

    bool Matches(const FileHeader &header, const FileHeader &expected, u32 magic)
    {
        return header.Magic == magic && header.Version == expected.Version && header.Builder == expected.Builder &&
               header.Fingerprint == expected.Fingerprint && header.Layout == expected.Layout;
    }

    void CloseStorage()
    {
        if (g.DataFd >= 0)
            close(g.DataFd);
        if (g.IndexFd >= 0)
            close(g.IndexFd);
        g.DataFd = g.IndexFd = -1;
        g.StorageOpen = g.Writable = false;
        g.StoredShapes.clear();
    }

    // Opens the cache files, or starts them afresh when they are missing or
    // come from another build. On the game's thread, before the writer starts.
    bool OpenStorage()
    {
        if (g.StorageTried)
            return g.StorageOpen;
        g.StorageTried = true;

        char dataPath[300], indexPath[300];
        snprintf(dataPath, sizeof(dataPath), "%s%s", g.Folder, kDataFile);
        snprintf(indexPath, sizeof(indexPath), "%s%s", g.Folder, kIndexFile);
        MakeFolders(g.Folder);

        FileHeader expected;
        memset(&expected, 0, sizeof(expected));
        expected.Version = kFormatVersion;
        expected.Builder = BuilderId();
        expected.Fingerprint = ReferenceFingerprint();
        expected.Layout = Layout();

        g.IndexFd = open(indexPath, O_RDWR | O_CREAT, 0666);
        g.DataFd = open(dataPath, O_RDWR | O_CREAT, 0666);
        if (g.IndexFd < 0 || g.DataFd < 0)
        {
            Logf("Physics shapes: cannot open the cache files in %s (%d)", g.Folder, errno);
            CloseStorage();
            return false;
        }

        FileHeader index, data;
        const off_t indexSize = lseek(g.IndexFd, 0, SEEK_END);
        const off_t dataSize = lseek(g.DataFd, 0, SEEK_END);
        bool valid = indexSize >= (off_t)sizeof(FileHeader) && dataSize >= (off_t)sizeof(FileHeader) &&
                     lseek(g.IndexFd, 0, SEEK_SET) == 0 && ReadAll(g.IndexFd, &index, sizeof(index)) &&
                     lseek(g.DataFd, 0, SEEK_SET) == 0 && ReadAll(g.DataFd, &data, sizeof(data)) &&
                     Matches(index, expected, kIndexMagic) && Matches(data, expected, kDataMagic) &&
                     index.Stamp == data.Stamp;

        u32 count = 0;
        if (valid)
        {
            count = (u32)(indexSize - sizeof(FileHeader)) / sizeof(IndexEntry);
            std::vector<IndexEntry> entries(count);
            valid = count == 0 || ReadAll(g.IndexFd, &entries[0], count * sizeof(IndexEntry));
            for (u32 i = 0; valid && i < count; ++i)
            {
                // Later records of a surface replace earlier ones.
                const IndexEntry &entry = entries[i];
                if ((entry.Offset & 31) == 0 && entry.Offset >= sizeof(FileHeader) && (entry.Size & 3) == 0 &&
                    entry.Size >= sizeof(IVP_Compact_Surface) && entry.Size <= kMaxDataBytes &&
                    entry.Offset + sizeof(RecordHeader) + entry.Size <= (u32)dataSize)
                {
                    Stored stored = {entry.Offset, entry.Size, entry.Check, entry.BuildMicroseconds};
                    g.StoredShapes[entry.Id] = stored;
                }
            }
            // New index entries go after the last whole one, and records on
            // the next 32-byte boundary (after a record cut short).
            lseek(g.IndexFd, sizeof(FileHeader) + count * sizeof(IndexEntry), SEEK_SET);
            g.DataSize = (u32)dataSize;
            if (g.DataSize & 31)
            {
                static const char zeros[32] = {0};
                const u32 gap = 32 - (g.DataSize & 31);
                valid = lseek(g.DataFd, g.DataSize, SEEK_SET) == (off_t)g.DataSize && WriteAll(g.DataFd, zeros, gap);
                g.DataSize += gap;
            }
        }
        if (!valid)
        {
            if (indexSize > 0 || dataSize > 0)
                Logf("Physics shapes: the cache files in %s are from another build; starting them afresh",
                     g.Folder);
            close(g.IndexFd);
            close(g.DataFd);
            g.StoredShapes.clear();
            g.IndexFd = open(indexPath, O_RDWR | O_CREAT | O_TRUNC, 0666);
            g.DataFd = open(dataPath, O_RDWR | O_CREAT | O_TRUNC, 0666);
            expected.Stamp = (u32)gettime();
            expected.Magic = kIndexMagic;
            bool ok = g.IndexFd >= 0 && WriteAll(g.IndexFd, &expected, sizeof(expected));
            expected.Magic = kDataMagic;
            ok = ok && g.DataFd >= 0 && WriteAll(g.DataFd, &expected, sizeof(expected));
            if (!ok)
            {
                Logf("Physics shapes: cannot write the cache files in %s (%d)", g.Folder, errno);
                CloseStorage();
                return false;
            }
            fsync(g.IndexFd);
            fsync(g.DataFd);
            g.DataSize = sizeof(FileHeader);
        }
        g.StorageOpen = true;
        g.Writable = true;
        g.ReadBytes = g.ReadMicroseconds = 0;
        Logf("Physics shapes: %u in the cache files in %s", (unsigned)g.StoredShapes.size(), g.Folder);
        return true;
    }

    // The surface stored for id, or NULL. On the game's thread.
    //
    // Reading takes longer the larger a surface is, while building depends on
    // how many pieces it has and how they are made: a level's floors are quick
    // to build for their size, small convex pieces are slow. A record is read
    // only when that is expected to be quicker than its build was, judged by
    // how fast records have been read so far. Dolphin reads its SD card at
    // once; a console reads a few megabytes a second, so there large floors
    // may well be built instead.
    Entry *ReadStored(const Key &id)
    {
        LWP_MutexLock(g.Lock);
        std::unordered_map<Key, Stored, KeyHash>::iterator it = g.StoredShapes.find(id);
        const bool found = it != g.StoredShapes.end() && it->second.Offset != kPending;
        const Stored stored = found ? it->second : Stored();
        const bool slower = found && g.ReadBytes >= kReadSample &&
                            (u64)stored.Size * g.ReadMicroseconds > (u64)stored.BuildMicroseconds * g.ReadBytes;
        if (slower)
            ++g.Counters.Skipped;
        LWP_MutexUnlock(g.Lock);
        if (!found || slower)
            return NULL;

        const u64 start = gettime();
        const u32 total = sizeof(RecordHeader) + stored.Size;
        void *allocation = ivp_malloc_aligned(total, 32);
        if (!allocation)
            return NULL;
        LWP_MutexLock(g.FileLock);
        bool ok = lseek(g.DataFd, stored.Offset, SEEK_SET) == (off_t)stored.Offset &&
                  ReadAll(g.DataFd, allocation, total);
        LWP_MutexUnlock(g.FileLock);

        const RecordHeader *record = (const RecordHeader *)allocation;
        IVP_Compact_Surface *surface = (IVP_Compact_Surface *)(record + 1);
        ok = ok && record->Magic == kRecordMagic && record->Size == stored.Size && record->Id == id &&
             record->Check == stored.Check && Checksum(surface, stored.Size) == stored.Check &&
             (u32)surface->get_size() == stored.Size && surface->offset_ledgetree_root > 0 &&
             (u32)surface->offset_ledgetree_root < stored.Size;

        LWP_MutexLock(g.Lock);
        g.ReadBytes += total;
        g.ReadMicroseconds += ticks_to_microsecs(gettime() - start);
        Entry *entry = NULL;
        if (ok)
        {
            entry = Insert(id, surface, allocation);
            entry->BuildMicroseconds = stored.BuildMicroseconds;
        }
        else
        {
            g.StoredShapes.erase(id);
            ++g.Counters.Rejected;
        }
        LWP_MutexUnlock(g.Lock);
        if (!ok)
        {
            ivp_free_aligned(allocation);
            Logf("Physics shapes: a record in the cache files did not check out; building the surface");
        }
        return entry;
    }

    enum WriteResult
    {
        WRITTEN,
        FULL,  // The data file is as large as it may grow
        FAILED
    };

    // Appends a record for entry. On the writer's thread.
    WriteResult WriteRecord(const Entry *entry, IndexEntry &index)
    {
        RecordHeader record;
        record.Magic = kRecordMagic;
        record.Size = entry->Size;
        record.Id = entry->Id;
        record.BuildMicroseconds = entry->BuildMicroseconds;
        const u32 offset = g.DataSize;
        const u32 padded = (entry->Size + 31) & ~31u;
        if (offset + sizeof(record) + padded > kMaxDataBytes)
            return FULL;
        record.Check = Checksum(entry->Surface, entry->Size);

        static const char zeros[32] = {0};
        bool ok = true;
        for (u32 done = 0; ok && done < sizeof(record) + padded;)
        {
            const char *data;
            u32 size;
            if (done == 0)
            {
                data = (const char *)&record;
                size = sizeof(record);
            }
            else if (done - sizeof(record) < entry->Size)
            {
                const u32 at = done - sizeof(record);
                data = (const char *)entry->Surface + at;
                size = entry->Size - at < kWriteChunk ? entry->Size - at : kWriteChunk;
            }
            else
            {
                data = zeros;
                size = padded - entry->Size;
            }
            LWP_MutexLock(g.FileLock);
            ok = lseek(g.DataFd, offset + done, SEEK_SET) == (off_t)(offset + done) && WriteAll(g.DataFd, data, size);
            LWP_MutexUnlock(g.FileLock);
            done += size;
        }
        if (!ok)
            return FAILED;
        g.DataSize = offset + sizeof(record) + padded;
        index.Id = entry->Id;
        index.Offset = offset;
        index.Size = entry->Size;
        index.Check = record.Check;
        index.BuildMicroseconds = record.BuildMicroseconds;
        return WRITTEN;
    }

    // Writes the surfaces the game queues, while the game's threads wait for
    // the GPU or the TV. A record is indexed once it is on the card.
    void *WriterMain(void *)
    {
        for (;;)
        {
            LWP_SemWait(g.Pending);
            std::vector<Entry *> batch;
            LWP_MutexLock(g.Lock);
            batch.swap(g.Queue);
            const bool stopping = g.Stopping;
            LWP_MutexUnlock(g.Lock);

            std::vector<IndexEntry> written;
            std::vector<bool> done(batch.size(), false);
            for (size_t i = 0; i < batch.size(); ++i)
            {
                if (!g.Writable || (g.Stopping && gettime() > g.Deadline))
                    break;
                IndexEntry index;
                const WriteResult result = WriteRecord(batch[i], index);
                if (result == WRITTEN)
                {
                    written.push_back(index);
                    done[i] = true;
                }
                else if (result == FAILED)
                {
                    g.Writable = false;
                    g.WriteError = errno ? errno : EIO;
                }
            }
            if (!written.empty())
            {
                // The records first, then the index that points at them.
                LWP_MutexLock(g.FileLock);
                fsync(g.DataFd);
                LWP_MutexUnlock(g.FileLock);
                if (WriteAll(g.IndexFd, &written[0], written.size() * sizeof(IndexEntry)))
                {
                    fsync(g.IndexFd);
                }
                else
                {
                    g.Writable = false;
                    g.WriteError = errno ? errno : EIO;
                    written.clear();
                    for (size_t i = 0; i < done.size(); ++i)
                        done[i] = false;
                }
            }

            LWP_MutexLock(g.Lock);
            size_t next = 0;
            for (size_t i = 0; i < batch.size(); ++i)
            {
                Entry *entry = batch[i];
                if (done[i])
                {
                    const IndexEntry &index = written[next++];
                    Stored stored = {index.Offset, index.Size, index.Check, index.BuildMicroseconds};
                    g.StoredShapes[entry->Id] = stored;
                    ++g.Counters.Written;
                }
                else
                {
                    std::unordered_map<Key, Stored, KeyHash>::iterator it = g.StoredShapes.find(entry->Id);
                    if (it != g.StoredShapes.end() && it->second.Offset == kPending)
                        g.StoredShapes.erase(it);
                }
                Unuse(entry);
            }
            LWP_MutexUnlock(g.Lock);
            if (stopping)
                break;
        }
        return NULL;
    }

    void ReportWriteError()
    {
        if (g.WriteError)
        {
            Logf("Physics shapes: writing the cache files in %s failed (%d); no more will be written", g.Folder,
                 g.WriteError);
            g.WriteError = 0;
        }
    }

    // Queues entry for the cache files. With g.Lock held.
    bool QueueWrite(Entry *entry)
    {
        if (!g.Writable || g.Stopping || g.StoredShapes.count(entry->Id))
            return false;
        if (!g.WriterRunning)
        {
            LWP_SemInit(&g.Pending, 0, 0x7fffffff);
            if (LWP_CreateThread(&g.Writer, WriterMain, NULL, NULL, 16 * 1024, kWriterPriority) != 0)
            {
                LWP_SemDestroy(g.Pending);
                g.Writable = false;
                return false;
            }
            g.WriterRunning = true;
        }
        Stored pending = {kPending, entry->Size, 0, 0};
        g.StoredShapes[entry->Id] = pending;
        ++entry->Users;
        g.Queue.push_back(entry);
        return true;
    }

    void Verify(const Entry *entry, const Source &source, BuildFunction build, void *user, const char *name)
    {
        IVP_Compact_Surface *fresh = build(source, user);
        const bool same = fresh && (u32)fresh->get_size() == entry->Size &&
                          HashSurface(fresh) == HashSurface(entry->Surface);
        const bool sameBytes = same && memcmp(fresh, entry->Surface, entry->Size) == 0;
        LWP_MutexLock(g.Lock);
        ++g.Counters.Verified;
        if (!same)
            ++g.Counters.Mismatched;
        LWP_MutexUnlock(g.Lock);
        if (!same)
            Logf("Physics shapes: %s differs from a fresh build", name ? name : "");
        else if (!sameBytes)
            Logf("Physics shapes: %s has the structure of a fresh build but other padding bytes", name ? name : "");
        if (fresh)
            ivp_free_aligned(fresh);
    }
}

void Configure(const Settings &settings)
{
    EnsureInit();
    const bool sameFolder = settings.Folder && strcmp(settings.Folder, g.Folder) == 0;
    if (g.StorageTried && (settings.CacheMode != STORAGE || !sameFolder))
        Shutdown();
    g.Config = settings;
    if (settings.Folder)
    {
        strncpy(g.Folder, settings.Folder, sizeof(g.Folder) - 1);
        g.Folder[sizeof(g.Folder) - 1] = '\0';
    }
    else
    {
        g.Folder[0] = '\0';
        if (g.Config.CacheMode == STORAGE)
            g.Config.CacheMode = MEMORY;
    }
    g.Config.Folder = g.Folder;
    LWP_MutexLock(g.Lock);
    EvictUnused(g.Config.CacheMode == OFF ? 0 : g.Config.MemoryBudget);
    LWP_MutexUnlock(g.Lock);
}

void Shutdown()
{
    if (!g.Initialized)
        return;
    if (g.WriterRunning)
    {
        LWP_MutexLock(g.Lock);
        g.Stopping = true;
        g.Deadline = gettime() + secs_to_ticks(kShutdownSeconds);
        LWP_MutexUnlock(g.Lock);
        LWP_SemPost(g.Pending);
        LWP_JoinThread(g.Writer, NULL);
        LWP_SemDestroy(g.Pending);
        g.WriterRunning = false;
        // Entries the writer did not get to.
        LWP_MutexLock(g.Lock);
        for (size_t i = 0; i < g.Queue.size(); ++i)
            Unuse(g.Queue[i]);
        g.Queue.clear();
        g.Stopping = false;
        LWP_MutexUnlock(g.Lock);
    }
    ReportWriteError();
    CloseStorage();
    g.StorageTried = false;
}

IVP_Compact_Surface *Acquire(const Source &source, const char *name, BuildFunction build, void *user)
{
    EnsureInit();
    const u64 start = gettime();
    if (g.Config.CacheMode == OFF)
    {
        IVP_Compact_Surface *surface = build(source, user);
        LWP_MutexLock(g.Lock);
        ++g.Counters.Built;
        g.Counters.BuildMicroseconds += ticks_to_microsecs(gettime() - start);
        LWP_MutexUnlock(g.Lock);
        return surface;
    }

    ReportWriteError();
    const Key id = ComputeKey(source);
    enum { FROM_MEMORY, FROM_STORAGE, BUILT } from = FROM_MEMORY;
    LWP_MutexLock(g.Lock);
    std::unordered_map<Key, Entry *, KeyHash>::iterator it = g.Entries.find(id);
    Entry *entry = it != g.Entries.end() ? it->second : NULL;
    if (entry)
        Use(entry);
    // Surfaces kept from the level before give way when memory runs short.
    else if (g.BytesUnused > 0 && g.Config.FreeMemory && g.Config.FreeMemory() < g.Config.MemoryReserve)
        EvictUnused(0);
    LWP_MutexUnlock(g.Lock);

    if (!entry && g.Config.CacheMode == STORAGE && OpenStorage())
    {
        entry = ReadStored(id);
        from = FROM_STORAGE;
    }
    if (!entry)
    {
        from = BUILT;
        const u64 buildStart = gettime();
        IVP_Compact_Surface *surface = build(source, user);
        if (!surface)
            return NULL;
        bool queued = false;
        LWP_MutexLock(g.Lock);
        entry = Insert(id, surface, surface);
        entry->BuildMicroseconds = (u32)ticks_to_microsecs(gettime() - buildStart);
        if (g.Config.CacheMode == STORAGE && g.StorageOpen)
            queued = QueueWrite(entry);
        LWP_MutexUnlock(g.Lock);
        if (queued)
            LWP_SemPost(g.Pending);
    }

    const u32 microseconds = (u32)ticks_to_microsecs(gettime() - start);
    LWP_MutexLock(g.Lock);
    switch (from)
    {
    case FROM_MEMORY:
        ++g.Counters.FromMemory;
        g.Counters.MemoryMicroseconds += microseconds;
        break;
    case FROM_STORAGE:
        ++g.Counters.FromStorage;
        g.Counters.StorageMicroseconds += microseconds;
        break;
    default:
        ++g.Counters.Built;
        g.Counters.BuildMicroseconds += microseconds;
        break;
    }
    LWP_MutexUnlock(g.Lock);

    if (g.Config.Verify && from != BUILT)
        Verify(entry, source, build, user, name);
    if (g.Config.LogShapes)
    {
        static const char *const kFrom[] = {"from memory", "from the cache files", "built"};
        const Key tree = HashSurface(entry->Surface);
        Logf("Physics shape %s: %s, %u bytes, %u us, key %08x%08x, tree %08x%08x%08x%08x", name ? name : "",
             kFrom[from], (unsigned)entry->Size, (unsigned)microseconds, id.Words[0], id.Words[1], tree.Words[0],
             tree.Words[1], tree.Words[2], tree.Words[3]);
    }
    return entry->Surface;
}

void Release(IVP_Compact_Surface *surface)
{
    if (!surface)
        return;
    EnsureInit();
    LWP_MutexLock(g.Lock);
    std::unordered_map<const IVP_Compact_Surface *, Entry *>::iterator it = g.BySurface.find(surface);
    Entry *entry = it != g.BySurface.end() ? it->second : NULL;
    if (entry)
        Unuse(entry);
    LWP_MutexUnlock(g.Lock);
    // Surfaces the cache did not make, such as multi-ball ones.
    if (!entry)
        ivp_free_aligned(surface);
}

void GetStats(Stats &stats)
{
    EnsureInit();
    LWP_MutexLock(g.Lock);
    stats = g.Counters;
    stats.BytesInUse = g.BytesInUse;
    stats.BytesUnused = g.BytesUnused;
    stats.StoredShapes = 0;
    for (std::unordered_map<Key, Stored, KeyHash>::const_iterator it = g.StoredShapes.begin();
         it != g.StoredShapes.end(); ++it)
        if (it->second.Offset != kPending)
            ++stats.StoredShapes;
    LWP_MutexUnlock(g.Lock);
}

Key ComputeKey(const Source &source)
{
    Hasher hasher;
    hasher.Word(kKeyVersion);
    hasher.Float(source.Scale.x);
    hasher.Float(source.Scale.y);
    hasher.Float(source.Scale.z);
    // The build skips missing meshes; which slots they were in does not matter.
    hasher.Word(0x434f4e56); // "CONV"
    for (int i = 0; i < source.ConvexCount; ++i)
    {
        if (!source.Convexes[i])
            continue;
        hasher.Word(1);
        HashPositions(hasher, source.Convexes[i]);
    }
    hasher.Word(0x434f4e43); // "CONC"
    for (int i = 0; i < source.ConcaveCount; ++i)
    {
        CKMesh *mesh = source.Concaves[i];
        if (!mesh)
            continue;
        hasher.Word(2);
        HashPositions(hasher, mesh);
        const int faces = mesh->GetFaceCount();
        const CKWORD *indices = faces > 0 ? mesh->GetFacesIndices() : NULL;
        hasher.Word(indices ? (u32)faces : 0);
        if (!indices)
            continue;
        const int count = faces * 3;
        int i3 = 0;
        for (; i3 + 1 < count; i3 += 2)
            hasher.Word(((u32)indices[i3] << 16) | indices[i3 + 1]);
        if (i3 < count)
            hasher.Word((u32)indices[i3] << 16);
    }
    return hasher.Finish();
}

Key HashSurface(const IVP_Compact_Surface *surface)
{
    Hasher hasher;
    hasher.Float(surface->mass_center.k[0]);
    hasher.Float(surface->mass_center.k[1]);
    hasher.Float(surface->mass_center.k[2]);
    hasher.Float(surface->rotation_inertia.k[0]);
    hasher.Float(surface->rotation_inertia.k[1]);
    hasher.Float(surface->rotation_inertia.k[2]);
    hasher.Float(surface->upper_limit_radius);
    hasher.Word(surface->max_factor_surface_deviation);
    hasher.Word((u32)surface->get_size());
    hasher.Word((u32)surface->offset_ledgetree_root);
    HashNode(hasher, surface, surface->get_compact_ledge_tree_root());
    return hasher.Finish();
}

void DropUnused()
{
    EnsureInit();
    LWP_MutexLock(g.Lock);
    EvictUnused(0);
    LWP_MutexUnlock(g.Lock);
}
}
