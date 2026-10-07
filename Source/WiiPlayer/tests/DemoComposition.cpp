// A small composition for trying the player without the game files: a level
// with a camera, a light, a floor and a cube that a Rotate script spins.
// Saved as sd:/apps/ballance-demo/base.cmo; put the player's boot.dol next to
// it to run it.

#include "TestFramework.h"

#include "CKAll.h"

#include <stdio.h>
#include <sys/stat.h>

namespace
{
    const char *kFolder = "sd:/apps/ballance-demo";
    const char *kPath = "sd:/apps/ballance-demo/base.cmo";
    const CKGUID kRotateGuid(0xffffffee, 0xeeffffff);

    CKMaterial *CreateMaterial(CKContext *context, const char *name, const VxColor &diffuse)
    {
        CKMaterial *material = (CKMaterial *)context->CreateObject(CKCID_MATERIAL, (CKSTRING)name);
        material->SetDiffuse(diffuse);
        material->SetAmbient(VxColor(diffuse.r * 0.3f, diffuse.g * 0.3f, diffuse.b * 0.3f, 1.0f));
        material->SetSpecular(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
        material->SetEmissive(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
        return material;
    }

    // An axis-aligned box with one normal per face.
    CKMesh *CreateBox(CKContext *context, const char *name, const VxVector &size, CKMaterial *material)
    {
        static const float faces[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        CKMesh *mesh = (CKMesh *)context->CreateObject(CKCID_MESH, (CKSTRING)name);
        mesh->SetVertexCount(24);
        mesh->SetFaceCount(12);
        for (int f = 0; f < 6; ++f)
        {
            const VxVector n(faces[f][0], faces[f][1], faces[f][2]);
            // Two axes spanning the face, oriented so the corners run clockwise
            // seen from outside (Direct3D front faces).
            VxVector u = (n.x != 0.0f) ? VxVector(0, 0, n.x) : (n.y != 0.0f ? VxVector(n.y, 0, 0) : VxVector(-n.z, 0, 0));
            VxVector v = CrossProduct(u, n);
            const VxVector corners[4] = {n - u + v, n + u + v, n + u - v, n - u - v};
            for (int c = 0; c < 4; ++c)
            {
                VxVector position(corners[c].x * size.x, corners[c].y * size.y, corners[c].z * size.z);
                VxVector normal = n;
                mesh->SetVertexPosition(f * 4 + c, &position);
                mesh->SetVertexNormal(f * 4 + c, &normal);
                mesh->SetVertexTextureCoordinates(f * 4 + c, (c == 1 || c == 2) ? 1.0f : 0.0f, (c >= 2) ? 1.0f : 0.0f);
            }
            mesh->SetFaceVertexIndex(f * 2, f * 4, f * 4 + 1, f * 4 + 2);
            mesh->SetFaceVertexIndex(f * 2 + 1, f * 4, f * 4 + 2, f * 4 + 3);
            mesh->SetFaceMaterial(f * 2, material);
            mesh->SetFaceMaterial(f * 2 + 1, material);
        }
        return mesh;
    }

    CK3dEntity *CreateEntity(CKContext *context, CKLevel *level, const char *name, CKMesh *mesh, const VxVector &position)
    {
        CK3dEntity *entity = (CK3dEntity *)context->CreateObject(CKCID_3DENTITY, (CKSTRING)name);
        entity->SetCurrentMesh(mesh);
        entity->SetPosition(&position);
        level->AddObject(entity);
        return entity;
    }

    CKBehaviorLink *Link(CKContext *context, CKBehaviorIO *from, CKBehaviorIO *to, int delay)
    {
        CKBehaviorLink *link = (CKBehaviorLink *)context->CreateObject(CKCID_BEHAVIORLINK, (CKSTRING) "link");
        link->SetInBehaviorIO(from);
        link->SetOutBehaviorIO(to);
        link->SetInitialActivationDelay(delay);
        link->SetActivationDelay(delay);
        return link;
    }

    // A script on the entity: its start input runs Rotate, which re-runs every frame.
    bool AddSpinScript(CKContext *context, CK3dEntity *entity)
    {
        CKBehavior *script = (CKBehavior *)context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Spin Script");
        script->SetType(CKBEHAVIORTYPE_SCRIPT);
        script->UseGraph();
        if (script->GetInputCount() == 0)
            script->CreateInput((CKSTRING) "Start");
        CKERROR err = entity->AddScript(script);
        if (err != CK_OK)
        {
            wiitest::Log("AddScript failed (%d)", err);
            return false;
        }

        CKBehavior *rotate = (CKBehavior *)context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Rotate");
        err = rotate->InitFromGuid(kRotateGuid);
        if (err != CK_OK)
        {
            wiitest::Log("Rotate prototype not found (%d)", err);
            return false;
        }
        err = script->AddSubBehavior(rotate);
        if (err != CK_OK)
        {
            wiitest::Log("AddSubBehavior failed (%d)", err);
            return false;
        }
        // One degree a frame around the vertical axis.
        CKParameterIn *angle = rotate->GetInputParameter(1);
        if (angle && angle->GetRealSource())
        {
            float radians = PI / 180.0f;
            angle->GetRealSource()->SetValue(&radians);
        }
        script->AddSubBehaviorLink(Link(context, script->GetInput(0), rotate->GetInput(0), 0));
        script->AddSubBehaviorLink(Link(context, rotate->GetOutput(0), rotate->GetInput(0), 1));
        return true;
    }
}

void BuildDemoComposition(CKContext *context)
{
    wiitest::BeginSuite("Demo composition");
    if (!WT_CHECK(context != NULL, "context"))
    {
        wiitest::EndSuite();
        return;
    }
    context->Reset();
    context->ClearAll();

    CKLevel *level = (CKLevel *)context->CreateObject(CKCID_LEVEL, (CKSTRING) "Demo Level");
    context->SetCurrentLevel(level);

    CKCamera *camera = (CKCamera *)context->CreateObject(CKCID_CAMERA, (CKSTRING) "Demo Camera");
    VxVector cameraPosition(0.0f, 4.0f, -9.0f);
    VxVector target(0.0f, 0.5f, 0.0f);
    camera->SetPosition(&cameraPosition);
    camera->LookAt(&target);
    camera->SetFov(0.9f);
    camera->SetFrontPlane(0.5f);
    camera->SetBackPlane(200.0f);
    level->AddObject(camera);

    CKLight *light = (CKLight *)context->CreateObject(CKCID_LIGHT, (CKSTRING) "Demo Light");
    light->SetType(VX_LIGHTDIREC);
    light->SetColor(VxColor(1.0f, 1.0f, 0.95f, 1.0f));
    VxVector lightPosition(-5.0f, 10.0f, -6.0f);
    VxVector origin(0.0f, 0.0f, 0.0f);
    light->SetPosition(&lightPosition);
    light->LookAt(&origin);
    level->AddObject(light);

    CKMaterial *floorMaterial = CreateMaterial(context, "Demo Floor", VxColor(0.2f, 0.55f, 0.25f, 1.0f));
    CKMaterial *cubeMaterial = CreateMaterial(context, "Demo Cube", VxColor(0.9f, 0.25f, 0.15f, 1.0f));
    CKMesh *floorMesh = CreateBox(context, "Demo Floor Mesh", VxVector(6.0f, 0.1f, 6.0f), floorMaterial);
    CKMesh *cubeMesh = CreateBox(context, "Demo Cube Mesh", VxVector(1.0f, 1.0f, 1.0f), cubeMaterial);
    CreateEntity(context, level, "Demo Floor", floorMesh, VxVector(0.0f, -0.1f, 0.0f));
    CK3dEntity *cube = CreateEntity(context, level, "Demo Cube", cubeMesh, VxVector(0.0f, 1.0f, 0.0f));
    WT_CHECK(AddSpinScript(context, cube), "spin script");
    level->AddObject(cube->GetScript(0));

    mkdir(kFolder, 0777);
    CKFile *file = context->CreateCKFile();
    CKERROR err = file->StartSave((CKSTRING)kPath);
    WT_CHECK(err == CK_OK, "StartSave %d", err);
    file->SaveObject(level);
    err = file->EndSave();
    WT_CHECK(err == CK_OK, "EndSave %d", err);
    context->DeleteCKFile(file);
    struct stat st;
    WT_CHECK(stat(kPath, &st) == 0 && st.st_size > 0, "%s written", kPath);
    wiitest::Log("Demo composition saved to %s", kPath);

    context->ClearAll();
    wiitest::EndSuite();
}
