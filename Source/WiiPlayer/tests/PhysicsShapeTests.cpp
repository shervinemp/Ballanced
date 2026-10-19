// The collision surface cache (WiiPhysicsShapes.h): objects with the same
// geometry share one built surface, a surface built again is the same as the
// one reused, unused surfaces are kept within a budget, and surfaces come back
// from the cache files as they were built, unless the files do not check out.

#include "TestFramework.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "CKAll.h"
#include "CKIpionManager.h"
#include "WiiPhysicsShapes.h"
#include "ivp_compact_surface.hxx"
#include "ivp_surman_polygon.hxx"

using namespace wiiphysicsshapes;

namespace
{
    const char kFolder[] = "sd:/wiitests/Cache/";
    const char kDataFile[] = "sd:/wiitests/Cache/PhysicsShapes.dat";
    const char kIndexFile[] = "sd:/wiitests/Cache/PhysicsShapes.idx";

    void LogLine(const char *text)
    {
        wiitest::Log("  %s", text);
    }

    unsigned int NoMemory()
    {
        return 0;
    }

    void Use(Mode mode, unsigned int budget, bool verify)
    {
        Settings settings;
        settings.CacheMode = mode;
        settings.MemoryBudget = budget;
        settings.MemoryReserve = 0;
        settings.FreeMemory = NULL;
        settings.Folder = kFolder;
        settings.Verify = verify;
        settings.LogShapes = false;
        settings.Log = LogLine;
        Configure(settings);
    }

    // An n x n grid of triangles over [-half, half] in x and z, with bumps.
    CKMesh *CreateGround(CKContext *context, const char *name, int n, float half)
    {
        CKMesh *mesh = (CKMesh *)context->CreateObject(CKCID_MESH, (CKSTRING)name);
        mesh->SetVertexCount((n + 1) * (n + 1));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j)
            {
                VxVector vertex(-half + 2.0f * half * i / n, ((i * 7 + j * 13) % 11) * 0.05f,
                                -half + 2.0f * half * j / n);
                mesh->SetVertexPosition(i * (n + 1) + j, &vertex);
            }
        mesh->SetFaceCount(2 * n * n);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                const int a = i * (n + 1) + j, b = a + 1, c = a + n + 1, d = c + 1;
                mesh->SetFaceVertexIndex(2 * (i * n + j), a, b, d);
                mesh->SetFaceVertexIndex(2 * (i * n + j) + 1, a, d, c);
            }
        return mesh;
    }

    // A fixed object whose collision surface is built from mesh, as Physicalize does.
    CK3dEntity *Physicalize(CKContext *context, CKIpionManager *physics, const char *name, CKMesh *mesh,
                            bool concave, const VxVector &scale, const VxVector &position)
    {
        CK3dEntity *entity = (CK3dEntity *)context->CreateObject(CKCID_3DENTITY, (CKSTRING)name);
        entity->SetCurrentMesh(mesh);
        entity->SetPosition(&position);
        entity->SetScale(&scale);
        IVP_Material *material = new IVP_Material_Simple(0.7f, 0.2f);
        const int err = physics->CreatePhysicsObjectOnParameters(
            entity, concave ? 0 : 1, concave ? NULL : &mesh, 0, NULL, NULL, concave ? 1 : 0, concave ? &mesh : NULL,
            0.0f, (CKSTRING)name, NULL, TRUE, material, 1.0f, (CKSTRING) "", FALSE, TRUE, TRUE, 0.0f, 0.0f);
        WT_CHECK(err == CK_OK, "%s physicalized (%d)", name, err);
        if (err == CK_OK)
            physics->OwnMaterial(entity, material);
        else
            delete material;
        return entity;
    }

    CK3dEntity *DropBall(CKContext *context, CKIpionManager *physics, const char *name, const VxVector &position)
    {
        CK3dEntity *entity = (CK3dEntity *)context->CreateObject(CKCID_3DENTITY, (CKSTRING)name);
        entity->SetPosition(&position);
        IVP_Material *material = new IVP_Material_Simple(0.7f, 0.2f);
        VxVector center(0.0f, 0.0f, 0.0f);
        float radius = 0.5f;
        const int err = physics->CreatePhysicsObjectOnParameters(entity, 0, NULL, 1, &center, &radius, 0, NULL, radius,
                                                                (CKSTRING)name, NULL, FALSE, material, 1.0f,
                                                                (CKSTRING) "", FALSE, TRUE, TRUE, 0.1f, 0.1f);
        WT_CHECK(err == CK_OK, "%s physicalized (%d)", name, err);
        if (err == CK_OK)
            physics->OwnMaterial(entity, material);
        else
            delete material;
        return entity;
    }

    const IVP_Compact_Surface *SurfaceOf(CKIpionManager *physics, CK3dEntity *entity)
    {
        PhysicsObject *object = physics->GetPhysicsObject(entity);
        if (!object || !object->m_RealObject)
            return NULL;
        IVP_SurfaceManager_Polygon *manager = (IVP_SurfaceManager_Polygon *)object->m_RealObject->get_surface_manager();
        return manager ? manager->get_compact_surface() : NULL;
    }

    // Unloads everything, as leaving a level does.
    void Restart(CKContext *context, CKIpionManager *physics)
    {
        context->Reset();
        context->ClearAll();
        physics->CreateEnvironment();
    }

    long FileSize(const char *path)
    {
        struct stat st;
        return stat(path, &st) == 0 ? (long)st.st_size : -1;
    }

    // Inverts a byte of a file.
    bool Damage(const char *path, long offset)
    {
        const int fd = open(path, O_RDWR);
        if (fd < 0)
            return false;
        unsigned char byte = 0;
        bool ok = lseek(fd, offset, SEEK_SET) == offset && read(fd, &byte, 1) == 1;
        byte ^= 0xFF;
        ok = ok && lseek(fd, offset, SEEK_SET) == offset && write(fd, &byte, 1) == 1;
        return close(fd) == 0 && ok;
    }

    bool WriteWord(const char *path, long offset, unsigned int value)
    {
        const int fd = open(path, O_RDWR);
        if (fd < 0)
            return false;
        const bool ok = lseek(fd, offset, SEEK_SET) == offset && write(fd, &value, sizeof(value)) == sizeof(value);
        return close(fd) == 0 && ok;
    }

    void TestKeys(CKContext *context)
    {
        CKMesh *one = CreateGround(context, "ShapeKeyOne", 6, 4.0f);
        CKMesh *two = CreateGround(context, "ShapeKeyTwo", 6, 4.0f);
        const Source a = {0, NULL, 1, &one, VxVector(1.0f, 1.0f, 1.0f)};
        const Source b = {0, NULL, 1, &two, VxVector(1.0f, 1.0f, 1.0f)};
        WT_CHECK(ComputeKey(a) == ComputeKey(b), "two meshes with the same geometry have one key");
        Source scaled = a;
        scaled.Scale.x = 2.0f;
        WT_CHECK(!(ComputeKey(scaled) == ComputeKey(a)), "the object's scale changes the key");
        const Source convex = {1, &one, 0, NULL, VxVector(1.0f, 1.0f, 1.0f)};
        WT_CHECK(!(ComputeKey(convex) == ComputeKey(a)), "a convex mesh has another key than a concave one");
        VxVector vertex;
        two->GetVertexPosition(17, &vertex);
        const VxVector original = vertex;
        vertex.y += 0.001f;
        two->SetVertexPosition(17, &vertex);
        WT_CHECK(!(ComputeKey(b) == ComputeKey(a)), "moving a vertex changes the key");
        two->SetVertexPosition(17, const_cast<VxVector *>(&original));
        two->SetFaceVertexIndex(5, 0, 1, 2);
        WT_CHECK(!(ComputeKey(b) == ComputeKey(a)), "changing a face changes the key");
    }

    void TestMemory(CKContext *context, CKIpionManager *physics)
    {
        const VxVector one(1.0f, 1.0f, 1.0f);
        Stats before, after;

        // Objects with the same geometry share one surface; another scale is another surface.
        Use(MEMORY, 0, false);
        GetStats(before);
        CKMesh *mesh1 = CreateGround(context, "ShapeGround1", 10, 6.0f);
        CKMesh *mesh2 = CreateGround(context, "ShapeGround2", 10, 6.0f);
        CK3dEntity *first = Physicalize(context, physics, "ShapeFirst", mesh1, true, one, VxVector(0.0f, 0.0f, 0.0f));
        CK3dEntity *second = Physicalize(context, physics, "ShapeSecond", mesh2, true, one, VxVector(0.0f, 0.0f, 30.0f));
        CK3dEntity *wide = Physicalize(context, physics, "ShapeWide", mesh1, true, VxVector(2.0f, 1.0f, 1.0f),
                                       VxVector(0.0f, 0.0f, 60.0f));
        GetStats(after);
        WT_CHECK(after.Built - before.Built == 2 && after.FromMemory - before.FromMemory == 1,
                 "two surfaces built, one shared: %u built, %u reused", after.Built - before.Built,
                 after.FromMemory - before.FromMemory);
        WT_CHECK(SurfaceOf(physics, first) != NULL && SurfaceOf(physics, first) == SurfaceOf(physics, second),
                 "objects with the same geometry use one surface");
        WT_CHECK(SurfaceOf(physics, wide) != SurfaceOf(physics, first), "a scaled object has its own surface");

        // A ball dropped on the shared surface stays on it.
        CK3dEntity *ball = DropBall(context, physics, "ShapeBall", VxVector(0.6f, 3.0f, 30.6f));
        for (int i = 0; i < 240; ++i)
            physics->Simulate(1000.0f / 60.0f);
        VxVector position;
        ball->GetPosition(&position);
        WT_CHECK(position.y > 0.45f && position.y < 1.5f, "ball rests on the shared surface: y = %f", position.y);

        // Building it again makes the same surface.
        Use(MEMORY, 0, true);
        GetStats(before);
        Physicalize(context, physics, "ShapeThird", mesh2, true, one, VxVector(0.0f, 0.0f, 90.0f));
        GetStats(after);
        WT_CHECK(after.Verified - before.Verified == 1 && after.Mismatched == before.Mismatched,
                 "a reused surface is the one a fresh build makes (%u compared, %u differ)",
                 after.Verified - before.Verified, after.Mismatched - before.Mismatched);
        Use(MEMORY, 0, false);

        // With no budget nothing is kept once the objects go.
        Restart(context, physics);
        GetStats(after);
        WT_CHECK(after.BytesInUse == 0 && after.BytesUnused == 0, "no surfaces kept without a budget: %u + %u bytes",
                 after.BytesInUse, after.BytesUnused);

        // With a budget, loading the "level" again reuses its surfaces.
        Use(MEMORY, 4u << 20, false);
        mesh1 = CreateGround(context, "ShapeGround1", 10, 6.0f);
        first = Physicalize(context, physics, "ShapeFirst", mesh1, true, one, VxVector(0.0f, 0.0f, 0.0f));
        const Key tree = HashSurface(SurfaceOf(physics, first));
        Restart(context, physics);
        GetStats(after);
        WT_CHECK(after.BytesInUse == 0 && after.BytesUnused > 0, "the unused surface is kept: %u bytes",
                 after.BytesUnused);
        GetStats(before);
        mesh1 = CreateGround(context, "ShapeGround1", 10, 6.0f);
        first = Physicalize(context, physics, "ShapeFirst", mesh1, true, one, VxVector(0.0f, 0.0f, 0.0f));
        GetStats(after);
        WT_CHECK(after.FromMemory - before.FromMemory == 1 && after.Built == before.Built,
                 "loading again reuses the kept surface");
        WT_CHECK(HashSurface(SurfaceOf(physics, first)) == tree, "the kept surface is unchanged");

        // Kept surfaces give way to a new one when memory runs short.
        Restart(context, physics);
        Settings shortOfMemory;
        shortOfMemory.CacheMode = MEMORY;
        shortOfMemory.MemoryBudget = 4u << 20;
        shortOfMemory.MemoryReserve = 1;
        shortOfMemory.FreeMemory = NoMemory;
        shortOfMemory.Folder = kFolder;
        shortOfMemory.Verify = false;
        shortOfMemory.LogShapes = false;
        shortOfMemory.Log = LogLine;
        Configure(shortOfMemory);
        GetStats(before);
        CKMesh *small = CreateGround(context, "ShapeSmall", 4, 2.0f);
        Physicalize(context, physics, "ShapeSmall", small, true, one, VxVector(0.0f, 0.0f, 0.0f));
        GetStats(after);
        WT_CHECK(before.BytesUnused > 0 && after.BytesUnused == 0 && after.BytesInUse > 0,
                 "kept surfaces are dropped when memory is short: %u bytes kept before, %u after", before.BytesUnused,
                 after.BytesUnused);
        Use(MEMORY, 0, false);
        Restart(context, physics);
        DropUnused();
        GetStats(after);
        WT_CHECK(after.BytesUnused == 0, "unused surfaces can be dropped");
    }

    void TestStorage(CKContext *context, CKIpionManager *physics)
    {
        const VxVector one(1.0f, 1.0f, 1.0f);
        const VxVector origin(0.0f, 0.0f, 0.0f);
        Stats before, after;
        Shutdown();
        unlink(kDataFile);
        unlink(kIndexFile);

        // A surface built is written to the cache files.
        Use(STORAGE, 0, false);
        GetStats(before);
        CKMesh *mesh = CreateGround(context, "ShapeStored", 12, 6.0f);
        CK3dEntity *entity = Physicalize(context, physics, "ShapeStored", mesh, true, one, origin);
        const IVP_Compact_Surface *built = SurfaceOf(physics, entity);
        const Key tree = built ? HashSurface(built) : Key();
        const long size = built ? built->get_size() : 0;
        Shutdown();
        GetStats(after);
        WT_CHECK(after.Built - before.Built == 1 && after.Written - before.Written == 1,
                 "a built surface is written to the cache files (%u written)", after.Written - before.Written);
        const long record = 32 + ((size + 31) & ~31L);
        WT_CHECK(FileSize(kDataFile) == 64 + record && FileSize(kIndexFile) == 64 + 32,
                 "cache files hold one record: %ld and %ld bytes", FileSize(kDataFile), FileSize(kIndexFile));

        // On the next start it comes back from them as it was built.
        Restart(context, physics);
        DropUnused();
        Use(STORAGE, 0, true);
        GetStats(before);
        mesh = CreateGround(context, "ShapeStored", 12, 6.0f);
        entity = Physicalize(context, physics, "ShapeStored", mesh, true, one, origin);
        GetStats(after);
        WT_CHECK(after.FromStorage - before.FromStorage == 1 && after.Built == before.Built,
                 "the surface is read from the cache files");
        WT_CHECK(SurfaceOf(physics, entity) && HashSurface(SurfaceOf(physics, entity)) == tree,
                 "the stored surface is the one that was built");
        WT_CHECK(after.Verified - before.Verified == 1 && after.Mismatched == before.Mismatched,
                 "the stored surface is the one a fresh build makes");
        CK3dEntity *ball = DropBall(context, physics, "ShapeStoredBall", VxVector(0.6f, 3.0f, 0.6f));
        for (int i = 0; i < 240; ++i)
            physics->Simulate(1000.0f / 60.0f);
        VxVector position;
        ball->GetPosition(&position);
        WT_CHECK(position.y > 0.45f && position.y < 1.5f, "ball rests on the stored surface: y = %f", position.y);
        Use(STORAGE, 0, false);

        // A damaged record is built again (and written again).
        Restart(context, physics);
        DropUnused();
        Shutdown();
        WT_CHECK(Damage(kDataFile, 64 + 32 + size / 2), "damage the stored surface");
        GetStats(before);
        mesh = CreateGround(context, "ShapeStored", 12, 6.0f);
        entity = Physicalize(context, physics, "ShapeStored", mesh, true, one, origin);
        GetStats(after);
        WT_CHECK(after.Rejected - before.Rejected == 1 && after.Built - before.Built == 1,
                 "a damaged record is rejected and the surface built");
        WT_CHECK(SurfaceOf(physics, entity) && HashSurface(SurfaceOf(physics, entity)) == tree,
                 "the rebuilt surface is the same");
        Shutdown();
        GetStats(after);
        WT_CHECK(after.Written - before.Written == 1, "the rebuilt surface is written again");

        // Cache files from another build are started afresh.
        Restart(context, physics);
        DropUnused();
        WT_CHECK(Damage(kIndexFile, 8), "mark the cache files as another build's");
        GetStats(before);
        mesh = CreateGround(context, "ShapeStored", 12, 6.0f);
        Physicalize(context, physics, "ShapeStored", mesh, true, one, origin);
        GetStats(after);
        WT_CHECK(after.FromStorage == before.FromStorage && after.Built - before.Built == 1,
                 "another build's cache files are not read");
        Shutdown();
        WT_CHECK(FileSize(kDataFile) == 64 + record && FileSize(kIndexFile) == 64 + 32,
                 "the cache files start afresh: %ld and %ld bytes", FileSize(kDataFile), FileSize(kIndexFile));

        // Once reads have shown their speed, a surface that builds faster than
        // it would read is built: here one whose build is marked as instant.
        Restart(context, physics);
        DropUnused();
        Shutdown();
        unlink(kDataFile);
        unlink(kIndexFile);
        CKMesh *large = CreateGround(context, "ShapeLarge", 30, 15.0f);
        Physicalize(context, physics, "ShapeLarge", large, true, one, origin);
        CKMesh *quick = CreateGround(context, "ShapeQuick", 4, 2.0f);
        Physicalize(context, physics, "ShapeQuick", quick, true, one, VxVector(0.0f, 0.0f, 40.0f));
        Shutdown();
        // The second index entry's build time, in its last word.
        WT_CHECK(WriteWord(kIndexFile, 64 + 32 + 28, 1), "mark a stored surface as instant to build");
        Restart(context, physics);
        DropUnused();
        GetStats(before);
        large = CreateGround(context, "ShapeLarge", 30, 15.0f);
        Physicalize(context, physics, "ShapeLarge", large, true, one, origin);
        quick = CreateGround(context, "ShapeQuick", 4, 2.0f);
        Physicalize(context, physics, "ShapeQuick", quick, true, one, VxVector(0.0f, 0.0f, 40.0f));
        GetStats(after);
        WT_CHECK(after.FromStorage - before.FromStorage == 1 && after.Skipped - before.Skipped == 1 &&
                     after.Built - before.Built == 1,
                 "a surface quicker to build than to read is built: %u read, %u built rather than read",
                 after.FromStorage - before.FromStorage, after.Skipped - before.Skipped);

        Restart(context, physics);
        DropUnused();
        Shutdown();
        unlink(kDataFile);
        unlink(kIndexFile);
    }
}

void RunPhysicsShapeTests(CKContext *context, bool storage)
{
    wiitest::BeginSuite("Physics shapes");
    CKIpionManager *physics = context ? (CKIpionManager *)context->GetManagerByGuid(TT_PHYSICS_MANAGER_GUID) : NULL;
    if (!WT_CHECK(physics != NULL, "physics manager"))
    {
        wiitest::EndSuite();
        return;
    }
    Restart(context, physics);
    DropUnused();

    TestKeys(context);
    TestMemory(context, physics);
    if (storage)
        TestStorage(context, physics);
    else
        wiitest::Log("No storage: cache file tests skipped");

    // The defaults again for whatever runs next.
    Restart(context, physics);
    Use(MEMORY, 0, false);
    DropUnused();
    wiitest::EndSuite();
}
