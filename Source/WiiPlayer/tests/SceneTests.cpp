// The render engine on the GX rasterizer: a camera, a light, a lit mesh and a
// 2D entity rendered through a CKRenderContext, the way the game draws.

#include "TestFramework.h"

#include "CKAll.h"

#include <gccore.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    int g_Window;

    struct Rgb
    {
        int r, g, b;
    };

    Rgb ReadPixel(CKRenderContext *rc, int x, int y)
    {
        VxRect rect((float)x, (float)y, (float)(x + 1), (float)(y + 1));
        CKDWORD pixel = 0;
        VxImageDescEx desc;
        desc.Image = (XBYTE *)&pixel;
        rc->DumpToMemory(&rect, VXBUFFER_BACKBUFFER, desc);
        Rgb rgb = {(int)(pixel >> 16 & 0xFF), (int)(pixel >> 8 & 0xFF), (int)(pixel & 0xFF)};
        return rgb;
    }

    bool Near(const Rgb &c, int r, int g, int b)
    {
        return abs(c.r - r) <= 24 && abs(c.g - g) <= 24 && abs(c.b - b) <= 24;
    }

    void ExpectPixel(CKRenderContext *rc, int x, int y, int r, int g, int b, const char *what)
    {
        const Rgb c = ReadPixel(rc, x, y);
        WT_CHECK(Near(c, r, g, b), "%s: (%d,%d) is %d,%d,%d, expected %d,%d,%d", what, x, y, c.r, c.g, c.b, r, g, b);
    }

    CKMaterial *CreateMaterial(CKContext *context, const char *name, const VxColor &diffuse)
    {
        CKMaterial *material = (CKMaterial *)context->CreateObject(CKCID_MATERIAL, (CKSTRING)name);
        material->SetDiffuse(diffuse);
        material->SetAmbient(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
        material->SetSpecular(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
        material->SetEmissive(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
        return material;
    }

    // A 4x4 quad in the z = 0 plane, facing a camera on the -z side.
    CK3dEntity *CreateQuad(CKContext *context, CKMaterial *material)
    {
        CKMesh *mesh = (CKMesh *)context->CreateObject(CKCID_MESH, (CKSTRING) "WiiSceneQuadMesh");
        mesh->SetVertexCount(4);
        VxVector corners[4] = {VxVector(-2, 2, 0), VxVector(2, 2, 0), VxVector(2, -2, 0), VxVector(-2, -2, 0)};
        for (int i = 0; i < 4; ++i)
        {
            mesh->SetVertexPosition(i, &corners[i]);
            VxVector normal(0, 0, -1);
            mesh->SetVertexNormal(i, &normal);
        }
        mesh->SetFaceCount(2);
        mesh->SetFaceVertexIndex(0, 0, 1, 2);
        mesh->SetFaceVertexIndex(1, 0, 2, 3);
        mesh->SetFaceMaterial(0, material);
        mesh->SetFaceMaterial(1, material);
        mesh->BuildFaceNormals();

        CK3dEntity *entity = (CK3dEntity *)context->CreateObject(CKCID_3DENTITY, (CKSTRING) "WiiSceneQuad");
        entity->SetCurrentMesh(mesh);
        return entity;
    }
}

void RunSceneTests(CKContext *context)
{
    wiitest::BeginSuite("Render engine scene");
    CKRenderManager *renderManager = context ? context->GetRenderManager() : NULL;
    WT_CHECK(renderManager != NULL, "render manager");
    if (!renderManager)
    {
        wiitest::EndSuite();
        return;
    }
    WT_CHECK(renderManager->GetRenderDriverCount() > 0, "render drivers %d", renderManager->GetRenderDriverCount());
    CKRECT rect = {0, 0, 640, 480};
    CKRenderContext *rc = renderManager->CreateRenderContext(&g_Window, 0, &rect, FALSE, 32);
    if (!WT_CHECK(rc != NULL, "render context"))
    {
        wiitest::EndSuite();
        return;
    }
    WT_CHECK(rc->GetWidth() == 640 && rc->GetHeight() == 480, "render context size %dx%d", rc->GetWidth(),
             rc->GetHeight());

    CKMaterial *background = rc->GetBackgroundMaterial();
    if (background)
        background->SetDiffuse(VxColor(0.0f, 0.0f, 0.5f, 1.0f));

    CKCamera *camera = (CKCamera *)context->CreateObject(CKCID_CAMERA, (CKSTRING) "WiiSceneCamera");
    VxVector cameraPosition(0, 0, -10);
    VxVector origin(0, 0, 0);
    camera->SetPosition(&cameraPosition);
    camera->LookAt(&origin);
    camera->SetFov(0.8f);
    camera->SetFrontPlane(1.0f);
    camera->SetBackPlane(100.0f);
    rc->AttachViewpointToCamera(camera);

    CKLight *light = (CKLight *)context->CreateObject(CKCID_LIGHT, (CKSTRING) "WiiSceneLight");
    light->SetType(VX_LIGHTDIREC);
    light->SetColor(VxColor(1.0f, 1.0f, 1.0f, 1.0f));
    light->SetPosition(&cameraPosition);
    light->LookAt(&origin);
    rc->AddObject(light);

    CKMaterial *red = CreateMaterial(context, "WiiSceneRed", VxColor(1.0f, 0.0f, 0.0f, 1.0f));
    CK3dEntity *quad = CreateQuad(context, red);
    rc->AddObject(quad);

    CKMaterial *green = CreateMaterial(context, "WiiSceneGreen", VxColor(0.0f, 1.0f, 0.0f, 1.0f));
    CK2dEntity *panel = (CK2dEntity *)context->CreateObject(CKCID_2DENTITY, (CKSTRING) "WiiScenePanel");
    panel->SetRect(VxRect(10.0f, 10.0f, 60.0f, 60.0f));
    panel->SetMaterial(green);
    // 2D entities draw when they belong to the current scene; without a level,
    // an interface object stands in.
    panel->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
    rc->AddObject(panel);

    for (int frame = 0; frame < 3; ++frame)
        WT_CHECK(rc->Render() == CK_OK, "Render frame %d", frame);

    ExpectPixel(rc, 320, 240, 255, 0, 0, "lit quad in the middle");
    ExpectPixel(rc, 320, 20, 0, 0, 128, "background above the quad");
    ExpectPixel(rc, 600, 400, 0, 0, 128, "background in the corner");
    ExpectPixel(rc, 35, 35, 0, 255, 0, "2D entity");

    // Hold the frame so it can be inspected.
    for (int i = 0; i < 120; ++i)
        VIDEO_WaitVSync();

    // Turned half-way the quad shows less light, turned around it is culled.
    VxQuaternion quarter(VxVector(0, 1, 0), PI / 3.0f);
    quad->SetQuaternion(&quarter);
    rc->Render();
    ExpectPixel(rc, 320, 240, 128, 0, 0, "quad at 60 degrees gets half the light");
    VxQuaternion half(VxVector(0, 1, 0), PI);
    quad->SetQuaternion(&half);
    rc->Render();
    ExpectPixel(rc, 320, 240, 0, 0, 128, "quad turned away is culled");

    renderManager->DestroyRenderContext(rc);
    wiitest::EndSuite();
}
