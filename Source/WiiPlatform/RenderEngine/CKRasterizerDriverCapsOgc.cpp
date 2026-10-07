#include "CKRasterizerDriverCaps.h"

// Discovery data for the Wii before the GX context exists. The GX driver
// publishes its final caps once the GPU is initialized.

static void AddDisplayMode(XArray<VxDisplayMode> &displayModes, int width, int height, int refreshRate)
{
    VxDisplayMode mode;
    mode.Width = width;
    mode.Height = height;
    mode.Bpp = 32;
    mode.RefreshRate = refreshRate;
    if (!displayModes.IsHere(mode))
        displayModes.PushBack(mode);
}

static void AddTextureFormat(XClassArray<CKTextureDesc> &textureFormats, VX_PIXELFORMAT format, CKDWORD flags)
{
    CKTextureDesc texture;
    texture.Flags = CKRST_TEXTURE_VALID | flags;
    VxPixelFormat2ImageDesc(format, texture.Format);
    textureFormats.PushBack(texture);
}

void CKRSTInitializeDriverCaps(
    XArray<VxDisplayMode> &displayModes,
    XClassArray<CKTextureDesc> &textureFormats,
    CKRasterizerNativeCapsDesc &caps)
{
    // The embedded frame buffer is 640 wide; PAL 50 Hz pictures are 528 lines tall.
    AddDisplayMode(displayModes, 640, 480, 60);
    AddDisplayMode(displayModes, 640, 528, 50);

    // Formats the GX texture unit samples natively (after tiling).
    AddTextureFormat(textureFormats, _32_ARGB8888, CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA);
    AddTextureFormat(textureFormats, _16_RGB565, CKRST_TEXTURE_RGB);
    AddTextureFormat(textureFormats, _16_ARGB1555, CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA);
    AddTextureFormat(textureFormats, _16_ARGB4444, CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA);

    caps = CKRasterizerNativeCapsDesc();
    caps.MaxTextureSize = 1024;
    caps.MaxTextureStages = 8;
    caps.MaxAnisotropy = 4;
    caps.MaxUserClipPlanes = 0;
    caps.MaxVertexBlendMatrices = 4;
    caps.MaxMSAASamples = 1;
    caps.MaxPointSize = 1.0f;
    caps.MaxLights = 8;
}
