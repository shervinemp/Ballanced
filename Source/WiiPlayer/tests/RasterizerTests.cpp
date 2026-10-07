// GX rasterizer pixels: fixed-function cases with the Direct3D results the
// desktop rasterizers produce, read back from the embedded frame buffer.

#include "TestFramework.h"

#include "CKRasterizer.h"
#include "VxMath.h"
#include "WiiSystem.h"

#include <gccore.h>
#include <stdlib.h>
#include <string.h>

extern void CKGXRasterizerGetInfo(CKRasterizerInfo *info);

namespace
{
    const int kWidth = 640;
    const int kHeight = 480;
    const int kTolerance = 24;
    int g_Window;

    CKDWORD FloatBits(float value)
    {
        CKDWORD bits;
        memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    struct Rgb
    {
        int r, g, b;
    };

    Rgb ReadPixel(CKRasterizerContext *ctx, int x, int y)
    {
        CKRECT rect = {x, y, x + 1, y + 1};
        CKDWORD pixel = 0;
        VxImageDescEx image;
        image.Image = (XBYTE *)&pixel;
        ctx->CopyToMemoryBuffer(&rect, VXBUFFER_BACKBUFFER, image);
        Rgb rgb = {(int)(pixel >> 16 & 0xFF), (int)(pixel >> 8 & 0xFF), (int)(pixel & 0xFF)};
        return rgb;
    }

    bool Near(const Rgb &c, int r, int g, int b)
    {
        return abs(c.r - r) <= kTolerance && abs(c.g - g) <= kTolerance && abs(c.b - b) <= kTolerance;
    }

#define EXPECT_PIXEL(ctx, px, py, er, eg, eb, what)                                                      do                                                                                                   {                                                                                                        const Rgb _c = ReadPixel((ctx), (px), (py));                                                         WT_CHECK(Near(_c, (er), (eg), (eb)), "%s: (%d,%d) is %d,%d,%d, expected %d,%d,%d", (what),                   (px), (py), _c.r, _c.g, _c.b, (er), (eg), (eb));                                        } while (0)

    void ResetStages(CKRasterizerContext *ctx)
    {
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage)
        {
            ctx->SetTexture(0, stage);
            for (CKDWORD tss = CKRST_TSS_OP; tss < (CKDWORD)CKRST_TSS_MAXSTATE; ++tss)
                ctx->SetTextureStageState(stage, (CKRST_TEXTURESTAGESTATETYPE)tss,
                                          tss == (CKDWORD)CKRST_TSS_TEXCOORDINDEX ? (CKDWORD)stage : 0);
            VxMatrix identity;
            Vx3DMatrixIdentity(identity);
            ctx->SetTransformMatrix((VXMATRIX_TYPE)(VXMATRIX_TEXTURE0 + stage), identity);
        }
    }

    // Vertex colours straight to the frame buffer: no lighting, depth or blending.
    void SetDiffuseState(CKRasterizerContext *ctx)
    {
        ctx->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_COLORVERTEX, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
        ctx->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_FOGENABLE, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
        ResetStages(ctx);
        ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
        ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
        ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
        VxMatrix identity;
        Vx3DMatrixIdentity(identity);
        ctx->SetTransformMatrix(VXMATRIX_WORLD, identity);
        ctx->SetTransformMatrix(VXMATRIX_VIEW, identity);
        ctx->SetTransformMatrix(VXMATRIX_PROJECTION, identity);
    }

    void BeginFrame(CKRasterizerContext *ctx, CKDWORD clearColor, const VxMatrix *projection = NULL)
    {
        VxMatrix identity;
        Vx3DMatrixIdentity(identity);
        ctx->SetTransformMatrix(VXMATRIX_PROJECTION, projection ? *projection : identity);
        WT_CHECK(ctx->Clear(CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH, clearColor, 1.0f, 0, 0, NULL), "Clear");
        WT_CHECK(ctx->BeginScene(), "BeginScene");
    }

    void EndFrame(CKRasterizerContext *ctx)
    {
        WT_CHECK(ctx->EndScene(), "EndScene");
        WT_CHECK(ctx->BackToFront(FALSE), "BackToFront");
    }

    CKBOOL DrawColored(CKRasterizerContext *ctx, VXPRIMITIVETYPE type, const VxVector *positions, const CKDWORD *colors,
                       int count)
    {
        VxDrawPrimitiveData data;
        memset(&data, 0, sizeof(data));
        data.VertexCount = count;
        data.Flags = CKRST_DP_TR_VC;
        data.PositionPtr = const_cast<VxVector *>(positions);
        data.PositionStride = sizeof(VxVector);
        data.ColorPtr = const_cast<CKDWORD *>(colors);
        data.ColorStride = sizeof(CKDWORD);
        return ctx->DrawPrimitive(type, NULL, 0, &data);
    }

    CKBOOL DrawTriangle(CKRasterizerContext *ctx, const VxVector positions[3], CKDWORD color)
    {
        const CKDWORD colors[3] = {color, color, color};
        return DrawColored(ctx, VX_TRIANGLELIST, positions, colors, 3);
    }

    // A screen-covering quad with texture coordinates 0..1 left to right, top to bottom.
    CKBOOL DrawTexturedQuad(CKRasterizerContext *ctx, CKDWORD color)
    {
        VxVector positions[4] = {VxVector(-1, 1, 0.5f), VxVector(1, 1, 0.5f), VxVector(1, -1, 0.5f),
                                 VxVector(-1, -1, 0.5f)};
        CKDWORD colors[4] = {color, color, color, color};
        float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        VxDrawPrimitiveData data;
        memset(&data, 0, sizeof(data));
        data.VertexCount = 4;
        data.Flags = CKRST_DP_TR_CL_VCT;
        data.PositionPtr = positions;
        data.PositionStride = sizeof(VxVector);
        data.ColorPtr = colors;
        data.ColorStride = sizeof(CKDWORD);
        data.TexCoordPtr = uvs;
        data.TexCoordStride = sizeof(uvs[0]);
        return ctx->DrawPrimitive(VX_TRIANGLEFAN, NULL, 0, &data);
    }

    CKDWORD CreateTexture(CKRasterizerContext *ctx, VX_PIXELFORMAT format, int width, int height,
                          const CKDWORD *pixels, CKDWORD flags = 0)
    {
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(format, desc.Format);
        desc.Format.Width = width;
        desc.Format.Height = height;
        desc.Format.BytesPerLine = width * desc.Format.BitsPerPixel / 8;
        desc.MipMapCount = 1;
        desc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | flags;
        CKDWORD handle = 0;
        WT_CHECK(ctx->CreateTexture(&desc, &handle) && handle, "CreateTexture %dx%d", width, height);
        if (handle && pixels)
        {
            VxImageDescEx image;
            VxPixelFormat2ImageDesc(_32_ARGB8888, image);
            image.Width = width;
            image.Height = height;
            image.BytesPerLine = width * 4;
            image.Image = (XBYTE *)pixels;
            WT_CHECK(ctx->LoadTexture(handle, image, 0, CKRST_CUBEFACE_XPOS, NULL), "LoadTexture");
        }
        return handle;
    }

    void SetTextureState(CKRasterizerContext *ctx, CKDWORD texture)
    {
        SetDiffuseState(ctx);
        ctx->SetTexture(texture, 0);
        ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
        ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
        ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
        ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
        ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
        ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSWRAP);
    }

    const VxVector kCenterTriangle[3] = {VxVector(-0.9f, -0.9f, 0.5f), VxVector(0.9f, -0.9f, 0.5f),
                                         VxVector(0.0f, 0.9f, 0.5f)};

    void TestClearAndOrientation(CKRasterizerContext *ctx)
    {
        SetDiffuseState(ctx);
        BeginFrame(ctx, 0xFF336699);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 0x33, 0x66, 0x99, "clear colour");
        EXPECT_PIXEL(ctx, 2, 2, 0x33, 0x66, 0x99, "clear colour in the corner");

        // +y is up and +x is right in clip space.
        const VxVector upper[3] = {VxVector(-0.9f, 0.1f, 0.5f), VxVector(0.9f, 0.1f, 0.5f), VxVector(0.0f, 0.9f, 0.5f)};
        const VxVector left[3] = {VxVector(-0.9f, -0.9f, 0.5f), VxVector(-0.1f, 0.0f, 0.5f), VxVector(-0.9f, 0.9f, 0.5f)};
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, upper, 0xFFFF0000);
        DrawTriangle(ctx, left, 0xFF00FF00);
        EndFrame(ctx);
        // Keep this frame on screen for a moment so it can be inspected.
        for (int i = 0; i < 120; ++i)
            VIDEO_WaitVSync();
        EXPECT_PIXEL(ctx, 320, 100, 255, 0, 0, "triangle in the upper half");
        EXPECT_PIXEL(ctx, 320, 380, 0, 0, 0, "lower half stays clear");
        EXPECT_PIXEL(ctx, 60, 240, 0, 255, 0, "triangle on the left");
        EXPECT_PIXEL(ctx, 580, 240, 0, 0, 0, "right side stays clear");
    }

    void TestShading(CKRasterizerContext *ctx)
    {
        SetDiffuseState(ctx);
        const CKDWORD colors[3] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF};
        BeginFrame(ctx, 0xFF000000);
        DrawColored(ctx, VX_TRIANGLELIST, kCenterTriangle, colors, 3);
        EndFrame(ctx);
        // Gouraud: near each corner its own colour.
        EXPECT_PIXEL(ctx, 60, 430, 255, 0, 0, "gouraud near the red corner");
        EXPECT_PIXEL(ctx, 580, 430, 0, 255, 0, "gouraud near the green corner");
        EXPECT_PIXEL(ctx, 320, 40, 0, 0, 255, "gouraud near the blue corner");

        // Direct3D flat shading uses the first vertex.
        ctx->SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
        BeginFrame(ctx, 0xFF000000);
        DrawColored(ctx, VX_TRIANGLELIST, kCenterTriangle, colors, 3);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 255, 0, 0, "flat shading takes the first vertex");
    }

    void TestCulling(CKRasterizerContext *ctx)
    {
        // Clockwise on screen: bottom-left, top, bottom-right.
        const VxVector clockwise[3] = {VxVector(-0.5f, -0.5f, 0.5f), VxVector(0.0f, 0.5f, 0.5f), VxVector(0.5f, -0.5f, 0.5f)};
        const VxVector counter[3] = {clockwise[0], clockwise[2], clockwise[1]};

        SetDiffuseState(ctx);
        ctx->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CCW);
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, clockwise, 0xFFFF0000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 255, 0, 0, "CCW culling keeps clockwise triangles");
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, counter, 0xFFFF0000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 0, 0, 0, "CCW culling drops counter-clockwise triangles");

        ctx->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CW);
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, clockwise, 0xFFFF0000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 0, 0, 0, "CW culling drops clockwise triangles");
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, counter, 0xFFFF0000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 255, 0, 0, "CW culling keeps counter-clockwise triangles");
    }

    void TestDepth(CKRasterizerContext *ctx)
    {
        VxVector nearTriangle[3], farTriangle[3];
        for (int i = 0; i < 3; ++i)
        {
            nearTriangle[i] = kCenterTriangle[i];
            nearTriangle[i].z = 0.2f;
            farTriangle[i] = kCenterTriangle[i];
            farTriangle[i].z = 0.8f;
        }
        SetDiffuseState(ctx);
        ctx->SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, nearTriangle, 0xFFFF0000);
        DrawTriangle(ctx, farTriangle, 0xFF00FF00);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 255, 0, 0, "far triangle drawn second stays hidden");
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, farTriangle, 0xFF00FF00);
        DrawTriangle(ctx, nearTriangle, 0xFFFF0000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 255, 0, 0, "near triangle drawn second covers");

        ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, nearTriangle, 0xFFFF0000);
        DrawTriangle(ctx, farTriangle, 0xFF00FF00);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 0, 255, 0, "without depth writes the later triangle covers");
    }

    void TestBlending(CKRasterizerContext *ctx)
    {
        SetDiffuseState(ctx);
        ctx->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA);
        ctx->SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_INVSRCALPHA);
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, kCenterTriangle, 0x80FFFFFF);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 128, 128, 128, "alpha blend");

        ctx->SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_ONE);
        ctx->SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_ONE);
        BeginFrame(ctx, 0xFF404040);
        DrawTriangle(ctx, kCenterTriangle, 0xFF400000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 128, 64, 64, "additive blend");

        ctx->SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_ZERO);
        ctx->SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_SRCCOLOR);
        BeginFrame(ctx, 0xFF808080);
        DrawTriangle(ctx, kCenterTriangle, 0xFFFF8000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 128, 64, 0, "modulate blend");

        SetDiffuseState(ctx);
        ctx->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER);
        ctx->SetRenderState(VXRENDERSTATE_ALPHAREF, 0x80);
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, kCenterTriangle, 0x40FF0000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 0, 0, 0, "alpha test rejects");
        BeginFrame(ctx, 0xFF000000);
        DrawTriangle(ctx, kCenterTriangle, 0xC0FF0000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 255, 0, 0, "alpha test accepts");
    }

    void TestTextures(CKRasterizerContext *ctx)
    {
        const CKDWORD redGreen[2] = {0xFFFF0000, 0xFF00FF00};
        const CKDWORD texture = CreateTexture(ctx, _32_ARGB8888, 2, 1, redGreen);
        SetTextureState(ctx, texture);
        BeginFrame(ctx, 0xFF000000);
        DrawTexturedQuad(ctx, 0xFFFFFFFF);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 160, 240, 255, 0, 0, "texture left texel");
        EXPECT_PIXEL(ctx, 480, 240, 0, 255, 0, "texture right texel");

        // Modulated by a half-grey vertex colour.
        BeginFrame(ctx, 0xFF000000);
        DrawTexturedQuad(ctx, 0xFF808080);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 160, 240, 128, 0, 0, "texture modulated by the vertex colour");

        // Texture matrix: translation in row 3 moves u by half a texture.
        ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
        VxMatrix matrix;
        Vx3DMatrixIdentity(matrix);
        matrix[3][0] = 0.5f;
        ctx->SetTransformMatrix(VXMATRIX_TEXTURE0, matrix);
        BeginFrame(ctx, 0xFF000000);
        DrawTexturedQuad(ctx, 0xFFFFFFFF);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 160, 240, 0, 255, 0, "texture matrix translation, left");
        EXPECT_PIXEL(ctx, 480, 240, 255, 0, 0, "texture matrix translation wraps, right");
        ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE);

        // 16-bit storage keeps the colours.
        const CKDWORD colors[4] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
        const CKDWORD texture565 = CreateTexture(ctx, _16_RGB565, 2, 2, colors);
        SetTextureState(ctx, texture565);
        BeginFrame(ctx, 0xFF000000);
        DrawTexturedQuad(ctx, 0xFFFFFFFF);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 160, 120, 255, 0, 0, "RGB565 texture, top-left");
        EXPECT_PIXEL(ctx, 480, 120, 0, 255, 0, "RGB565 texture, top-right");
        EXPECT_PIXEL(ctx, 160, 360, 0, 0, 255, "RGB565 texture, bottom-left");
        EXPECT_PIXEL(ctx, 480, 360, 255, 255, 255, "RGB565 texture, bottom-right");
        ctx->DeleteObject(texture565, CKRST_OBJ_TEXTURE);

        // Alpha from a 4444 texture blends.
        const CKDWORD halfRed[1] = {0x80FF0000};
        const CKDWORD texture4444 = CreateTexture(ctx, _16_ARGB4444, 1, 1, halfRed);
        SetTextureState(ctx, texture4444);
        ctx->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA);
        ctx->SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_INVSRCALPHA);
        BeginFrame(ctx, 0xFF0000FF);
        DrawTexturedQuad(ctx, 0xFFFFFFFF);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 136, 0, 119, "ARGB4444 texture alpha blend");
        ctx->DeleteObject(texture4444, CKRST_OBJ_TEXTURE);
    }

    void TestFog(CKRasterizerContext *ctx)
    {
        // Fog uses eye-space depth: z = 10 between start 0 and end 20.
        SetDiffuseState(ctx);
        VxMatrix projection;
        Vx3DMatrixIdentity(projection);
        projection[2][2] = 0.01f;
        ctx->SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_NONE);
        ctx->SetRenderState(VXRENDERSTATE_FOGPIXELMODE, VXFOG_LINEAR);
        ctx->SetRenderState(VXRENDERSTATE_FOGSTART, FloatBits(0.0f));
        ctx->SetRenderState(VXRENDERSTATE_FOGEND, FloatBits(20.0f));
        ctx->SetRenderState(VXRENDERSTATE_FOGCOLOR, 0xFF000000);
        const VxVector triangle[3] = {VxVector(-0.9f, -0.9f, 10.0f), VxVector(0.9f, -0.9f, 10.0f),
                                      VxVector(0.0f, 0.9f, 10.0f)};
        BeginFrame(ctx, 0xFF000000, &projection);
        DrawTriangle(ctx, triangle, 0xFFFFFFFF);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 128, 128, 128, "linear fog halfway");
    }

    void TestLighting(CKRasterizerContext *ctx)
    {
        SetDiffuseState(ctx);
        ctx->SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_COLORVERTEX, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_AMBIENT, 0xFF000000);
        CKMaterialData material;
        memset(&material, 0, sizeof(material));
        material.Diffuse = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
        ctx->SetMaterial(&material);

        // A directional light shining into the screen lights a face turned to the viewer.
        CKLightData light;
        memset(&light, 0, sizeof(light));
        light.Type = VX_LIGHTDIREC;
        light.Diffuse = VxColor(1.0f, 0.0f, 0.0f, 1.0f);
        light.Direction = VxVector(0.0f, 0.0f, 1.0f);
        light.Range = 1000.0f;
        light.Attenuation0 = 1.0f;
        ctx->SetLight(0, &light);
        ctx->EnableLight(0, TRUE);

        VxVector normals[3] = {VxVector(0, 0, -1), VxVector(0, 0, -1), VxVector(0, 0, -1)};
        VxDrawPrimitiveData data;
        memset(&data, 0, sizeof(data));
        data.VertexCount = 3;
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT;
        data.PositionPtr = const_cast<VxVector *>(kCenterTriangle);
        data.PositionStride = sizeof(VxVector);
        data.NormalPtr = normals;
        data.NormalStride = sizeof(VxVector);
        BeginFrame(ctx, 0xFF000000);
        ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 255, 0, 0, "directional light on a facing triangle");

        // Turned away, only the ambient term remains.
        for (int i = 0; i < 3; ++i)
            normals[i] = VxVector(0, 0, 1);
        ctx->SetRenderState(VXRENDERSTATE_AMBIENT, 0xFF202020);
        material.Ambient = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
        ctx->SetMaterial(&material);
        BeginFrame(ctx, 0xFF000000);
        ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 32, 32, 32, "ambient only for a face turned away");
        ctx->EnableLight(0, FALSE);
    }

    // Draws triangles lit by light 0 with normals facing the viewer.
    void DrawLitTriangles(CKRasterizerContext *ctx, const VxVector *positions, int count)
    {
        VxVector normals[6];
        for (int i = 0; i < count; ++i)
            normals[i] = VxVector(0, 0, -1);
        VxDrawPrimitiveData data;
        memset(&data, 0, sizeof(data));
        data.VertexCount = count;
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT;
        data.PositionPtr = const_cast<VxVector *>(positions);
        data.PositionStride = sizeof(VxVector);
        data.NormalPtr = normals;
        data.NormalStride = sizeof(VxVector);
        ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data);
    }

    void TestPointAndSpotLights(CKRasterizerContext *ctx)
    {
        SetDiffuseState(ctx);
        ctx->SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_COLORVERTEX, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_AMBIENT, 0xFF000000);
        CKMaterialData material;
        memset(&material, 0, sizeof(material));
        material.Diffuse = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
        ctx->SetMaterial(&material);

        // A point light at the eye, half a unit in front of the triangle.
        // Lighting is per vertex: the base corners get N.L = 0.366 and the
        // apex 0.486, so the centre, halfway up, is 0.426.
        CKLightData light;
        memset(&light, 0, sizeof(light));
        light.Type = VX_LIGHTPOINT;
        light.Diffuse = VxColor(1.0f, 0.0f, 0.0f, 1.0f);
        light.Position = VxVector(0.0f, 0.0f, 0.0f);
        light.Range = 1000.0f;
        light.Attenuation0 = 1.0f;
        ctx->SetLight(0, &light);
        ctx->EnableLight(0, TRUE);
        BeginFrame(ctx, 0xFF000000);
        DrawLitTriangles(ctx, kCenterTriangle, 3);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 240, 109, 0, 0, "point light, per-vertex falloff");

        // A spot light at the eye pointing into the screen with a 120 degree
        // cone: a small triangle on its axis is lit, one 76 degrees off it
        // (which a point light would reach) is not.
        light.Type = VX_LIGHTSPOT;
        light.Direction = VxVector(0.0f, 0.0f, 1.0f);
        light.OuterSpotCone = 120.0f * PI / 180.0f;
        light.InnerSpotCone = 100.0f * PI / 180.0f;
        light.Falloff = 1.0f;
        ctx->SetLight(0, &light);
        const VxVector triangles[6] = {VxVector(-0.05f, -0.05f, 0.5f), VxVector(0.05f, -0.05f, 0.5f),
                                       VxVector(0.0f, 0.05f, 0.5f),    VxVector(0.75f, -0.05f, 0.2f),
                                       VxVector(0.85f, -0.05f, 0.2f),  VxVector(0.8f, 0.05f, 0.2f)};
        BeginFrame(ctx, 0xFF000000);
        DrawLitTriangles(ctx, triangles, 6);
        EndFrame(ctx);
        const Rgb onAxis = ReadPixel(ctx, 320, 240);
        WT_CHECK(onAxis.r > 200 && onAxis.g < 8, "spot light on its axis: %d,%d,%d", onAxis.r, onAxis.g, onAxis.b);
        EXPECT_PIXEL(ctx, 576, 240, 0, 0, 0, "spot light outside its cone");
        ctx->EnableLight(0, FALSE);
    }

    void TestPretransformed(CKRasterizerContext *ctx)
    {
        SetDiffuseState(ctx);
        float positions[4][4] = {{100, 100, 0.5f, 1}, {200, 100, 0.5f, 1}, {200, 200, 0.5f, 1}, {100, 200, 0.5f, 1}};
        CKDWORD colors[4] = {0xFFFF0000, 0xFFFF0000, 0xFFFF0000, 0xFFFF0000};
        VxDrawPrimitiveData data;
        memset(&data, 0, sizeof(data));
        data.VertexCount = 4;
        data.Flags = CKRST_DP_CL_VC;
        data.PositionPtr = positions;
        data.PositionStride = sizeof(positions[0]);
        data.ColorPtr = colors;
        data.ColorStride = sizeof(CKDWORD);
        BeginFrame(ctx, 0xFF000000);
        ctx->DrawPrimitive(VX_TRIANGLEFAN, NULL, 0, &data);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 150, 150, 255, 0, 0, "screen-space quad inside");
        EXPECT_PIXEL(ctx, 98, 150, 0, 0, 0, "screen-space quad left edge");
        EXPECT_PIXEL(ctx, 150, 202, 0, 0, 0, "screen-space quad bottom edge");
    }

    // System screens over the picture: filled and outlined boxes, see-through
    // fills and centered text from the console font.
    void TestOverlay(CKRasterizerContext *ctx)
    {
        wiisystem::OverlayItem items[3];
        items[0] = wiisystem::MakeOverlayItem(100, 100, 300, 200, 0x0000FFFF, 0x00FF00FF);
        items[1] = wiisystem::MakeOverlayItem(400, 100, 560, 228, 0, 0, "W", 0xFFFFFFFF, 4.0f);
        items[2] = wiisystem::MakeOverlayItem(100, 300, 300, 400, 0x00000080, 0);
        wiisystem::SetOverlay(wiisystem::OVERLAY_HOME_MENU, items, 3);
        BeginFrame(ctx, 0xFFFF0000);
        EndFrame(ctx);
        wiisystem::SetOverlay(wiisystem::OVERLAY_HOME_MENU, NULL, 0);

        EXPECT_PIXEL(ctx, 200, 150, 0, 0, 255, "overlay fill");
        EXPECT_PIXEL(ctx, 100, 150, 0, 255, 0, "overlay border");
        EXPECT_PIXEL(ctx, 50, 50, 255, 0, 0, "outside the overlay");
        EXPECT_PIXEL(ctx, 200, 350, 127, 0, 0, "half-transparent fill");

        // "W" at four times 8x16, centered: 32x64 pixels from (464,132).
        int lit = 0, background = 0, other = 0;
        for (int y = 132; y < 196; y += 2)
        {
            for (int x = 464; x < 496; x += 2)
            {
                const Rgb c = ReadPixel(ctx, x, y);
                if (Near(c, 255, 255, 255))
                    ++lit;
                else if (Near(c, 255, 0, 0))
                    ++background;
                else
                    ++other;
            }
        }
        WT_CHECK(lit > 40 && background > 100 && other == 0, "text: %d lit, %d background, %d other pixels", lit,
                 background, other);
        EXPECT_PIXEL(ctx, 460, 164, 255, 0, 0, "left of the text");
        EXPECT_PIXEL(ctx, 500, 164, 255, 0, 0, "right of the text");

        // Nothing is drawn once the overlay is cleared.
        BeginFrame(ctx, 0xFFFF0000);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 200, 150, 255, 0, 0, "cleared overlay");
    }

    void TestRenderToTexture(CKRasterizerContext *ctx)
    {
        const CKDWORD target = CreateTexture(ctx, _32_ARGB8888, 64, 64, NULL, CKRST_TEXTURE_RENDERTARGET);
        SetDiffuseState(ctx);
        WT_CHECK(ctx->SetTargetTexture(target, 64, 64, CKRST_CUBEFACE_XPOS), "SetTargetTexture");
        WT_CHECK(ctx->Clear(CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH, 0xFF0000FF, 1.0f, 0, 0, NULL), "target clear");
        WT_CHECK(ctx->BeginScene(), "target BeginScene");
        const VxVector upper[3] = {VxVector(-1.0f, 0.0f, 0.5f), VxVector(1.0f, 0.0f, 0.5f), VxVector(0.0f, 1.0f, 0.5f)};
        DrawTriangle(ctx, upper, 0xFFFF0000);
        WT_CHECK(ctx->EndScene(), "target EndScene");
        WT_CHECK(ctx->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "restore the back buffer");

        SetTextureState(ctx, target);
        BeginFrame(ctx, 0xFF000000);
        DrawTexturedQuad(ctx, 0xFFFFFFFF);
        EndFrame(ctx);
        EXPECT_PIXEL(ctx, 320, 200, 255, 0, 0, "rendered texture, drawn triangle");
        EXPECT_PIXEL(ctx, 40, 440, 0, 0, 255, "rendered texture, cleared area");
        ctx->DeleteObject(target, CKRST_OBJ_TEXTURE);
    }
}

void RunRasterizerTests()
{
    wiitest::BeginSuite("GX rasterizer");
    CKRasterizerInfo info;
    CKGXRasterizerGetInfo(&info);
    WT_CHECK(info.InterfaceRevision == CKRST_INTERFACE_REVISION, "interface revision");
    CKRasterizer *rasterizer = info.StartFct ? info.StartFct((WIN_HANDLE)&g_Window) : NULL;
    CKRasterizerDriver *driver = rasterizer && rasterizer->GetDriverCount() > 0 ? rasterizer->GetDriver(0) : NULL;
    CKRasterizerContext *ctx = driver ? driver->CreateContext() : NULL;
    if (!WT_CHECK(ctx != NULL, "context") ||
        !WT_CHECK(ctx->Create((WIN_HANDLE)&g_Window, 0, 0, kWidth, kHeight, 32, FALSE, 0, 24, 8), "Create"))
    {
        wiitest::EndSuite();
        return;
    }

    TestClearAndOrientation(ctx);
    TestShading(ctx);
    TestCulling(ctx);
    TestDepth(ctx);
    TestBlending(ctx);
    TestTextures(ctx);
    TestFog(ctx);
    TestLighting(ctx);
    TestPointAndSpotLights(ctx);
    TestPretransformed(ctx);
    TestOverlay(ctx);
    TestRenderToTexture(ctx);

    ctx->BeginShutdown();
    driver->DestroyContext(ctx);
    info.CloseFct(rasterizer);
    wiitest::EndSuite();
}
