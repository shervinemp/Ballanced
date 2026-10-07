// Physics through physics_RT and IVP: a ball dropped on a fixed box falls,
// lands and rests on top of it, and a ball rolls down a slope.

#include "TestFramework.h"

#include "CKAll.h"
#include "CKIpionManager.h"

#include <math.h>

namespace
{
    // An axis-aligned box mesh centred on the origin with half extents size.
    CKMesh *CreateBoxMesh(CKContext *context, const char *name, const VxVector &size)
    {
        static const float corners[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                            {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
        static const int faces[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                         {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
        CKMesh *mesh = (CKMesh *)context->CreateObject(CKCID_MESH, (CKSTRING)name);
        mesh->SetVertexCount(8);
        for (int i = 0; i < 8; ++i)
        {
            VxVector position(corners[i][0] * size.x, corners[i][1] * size.y, corners[i][2] * size.z);
            mesh->SetVertexPosition(i, &position);
        }
        mesh->SetFaceCount(12);
        for (int i = 0; i < 12; ++i)
            mesh->SetFaceVertexIndex(i, faces[i][0], faces[i][1], faces[i][2]);
        mesh->BuildFaceNormals();
        return mesh;
    }

    CK3dEntity *CreateFixedBox(CKContext *context, CKIpionManager *physics, const char *name, const VxVector &size,
                               const VxVector &position, const VxQuaternion *rotation)
    {
        CKMesh *mesh = CreateBoxMesh(context, name, size);
        CK3dEntity *entity = (CK3dEntity *)context->CreateObject(CKCID_3DENTITY, (CKSTRING)name);
        entity->SetCurrentMesh(mesh);
        entity->SetPosition(&position);
        if (rotation)
            entity->SetQuaternion(rotation);
        IVP_Material *material = new IVP_Material_Simple(0.7f, 0.2f);
        const int err = physics->CreatePhysicsObjectOnParameters(entity, 1, &mesh, 0, NULL, NULL, 0, NULL, 0.0f,
                                                                (CKSTRING)name, NULL, TRUE, material, 1.0f, (CKSTRING) "",
                                                                FALSE, TRUE, TRUE, 0.0f, 0.0f);
        WT_CHECK(err == CK_OK, "%s physicalized (%d)", name, err);
        if (err == CK_OK)
            physics->OwnMaterial(entity, material);
        else
            delete material;
        return entity;
    }

    CK3dEntity *CreateBall(CKContext *context, CKIpionManager *physics, const char *name, float radius,
                           const VxVector &position, CKBOOL fixed = FALSE)
    {
        CK3dEntity *entity = (CK3dEntity *)context->CreateObject(CKCID_3DENTITY, (CKSTRING)name);
        entity->SetPosition(&position);
        IVP_Material *material = new IVP_Material_Simple(0.7f, 0.2f);
        VxVector center(0.0f, 0.0f, 0.0f);
        const int err = physics->CreatePhysicsObjectOnParameters(entity, 0, NULL, 1, &center, &radius, 0, NULL, radius,
                                                                (CKSTRING)name, NULL, fixed, material, 1.0f,
                                                                (CKSTRING) "", FALSE, TRUE, TRUE, 0.1f, 0.1f);
        WT_CHECK(err == CK_OK, "%s physicalized (%d)", name, err);
        if (err == CK_OK)
            physics->OwnMaterial(entity, material);
        else
            delete material;
        return entity;
    }

    void Simulate(CKIpionManager *physics, int frames)
    {
        for (int i = 0; i < frames; ++i)
            physics->Simulate(1000.0f / 60.0f);
    }
}

void RunPhysicsTests(CKContext *context)
{
    wiitest::BeginSuite("Physics");
    CKIpionManager *physics = context ? (CKIpionManager *)context->GetManagerByGuid(TT_PHYSICS_MANAGER_GUID) : NULL;
    if (!WT_CHECK(physics != NULL, "physics manager"))
    {
        wiitest::EndSuite();
        return;
    }
    context->Reset();
    context->ClearAll();
    // Playing a level creates the physics world; there is no level here.
    physics->CreateEnvironment();

    // Objects collide only when they name a collision surface, as the
    // Physicalize building block always does; the name also keys the cache of
    // built surfaces. A ball dropped on a fixed ball comes to rest on top of it (or rolls off).
    CreateBall(context, physics, "PhysicsAnchor", 2.0f, VxVector(30.0f, 0.0f, 0.0f), TRUE);
    CK3dEntity *dropped = CreateBall(context, physics, "PhysicsDropped", 0.5f, VxVector(30.0f, 5.0f, 0.0f));

    // A floor whose top is at y = 0, and a ball of radius 0.5 dropped from 5.
    CK3dEntity *floor = CreateFixedBox(context, physics, "PhysicsFloor", VxVector(10.0f, 0.5f, 10.0f),
                                       VxVector(0.0f, -0.5f, 0.0f), NULL);
    WT_CHECK(physics->GetPhysicsObject(floor) != NULL, "floor physics object");
    CK3dEntity *ball = CreateBall(context, physics, "PhysicsBall", 0.5f, VxVector(0.0f, 5.0f, 0.0f));

    VxVector position;
    Simulate(physics, 90);
    dropped->GetPosition(&position);
    WT_CHECK(position.y > 1.0f, "ball lands on the fixed ball: y = %f, x = %f", position.y, position.x);
    ball->GetPosition(&position);
    WT_CHECK(position.y < 4.5f, "ball falls: y = %f after a second and a half", position.y);
    Simulate(physics, 180);
    ball->GetPosition(&position);
    WT_CHECK(fabsf(position.y - 0.5f) < 0.1f, "ball rests on the floor: y = %f", position.y);
    WT_CHECK(fabsf(position.x) < 0.1f && fabsf(position.z) < 0.1f, "ball stays put: x = %f, z = %f", position.x,
             position.z);

    // On a 20 degree slope falling towards +x, a ball rolls that way.
    VxQuaternion tilt(VxVector(0.0f, 0.0f, 1.0f), -20.0f * PI / 180.0f);
    CreateFixedBox(context, physics, "PhysicsSlope", VxVector(10.0f, 0.5f, 3.0f), VxVector(0.0f, 2.0f, 10.0f), &tilt);
    CK3dEntity *roller = CreateBall(context, physics, "PhysicsRoller", 0.5f, VxVector(-2.0f, 4.0f, 10.0f));
    Simulate(physics, 120);
    roller->GetPosition(&position);
    WT_CHECK(fabsf(position.x + 2.0f) > 1.0f, "ball rolls down the slope: x = %f", position.x);
    WT_CHECK(fabsf(position.z - 10.0f) < 0.5f, "ball rolls straight: z = %f", position.z);

    context->Reset();
    context->ClearAll();
    wiitest::EndSuite();
}
