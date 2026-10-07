// Textures: Virtools images converted to GX tiled formats.
//
// Opaque textures use RGB565 (half the memory of RGBA8); textures with alpha
// use RGBA8 to keep smooth alpha edges. GX samples power-of-two sizes up to
// 1024, so other sizes are resized on upload.

#include "CKGXRasterizer.h"

#include <malloc.h>
#include <string.h>

namespace
{
    const int kMaxTextureSize = 1024;

    u16 NextPowerOfTwo(int value)
    {
        int result = 1;
        while (result < value && result < kMaxTextureSize)
            result <<= 1;
        return (u16)result;
    }

    int LevelCount(int width, int height)
    {
        int levels = 1;
        while (width > 1 || height > 1)
        {
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
            ++levels;
        }
        return levels;
    }

    u32 LevelOffset(const CKGXTexture &texture, int level)
    {
        u32 offset = 0;
        for (int i = 0; i < level; ++i)
        {
            const u16 w = (u16)(texture.Width >> i ? texture.Width >> i : 1);
            const u16 h = (u16)(texture.Height >> i ? texture.Height >> i : 1);
            offset += GX_GetTexBufferSize(w, h, texture.Format, GX_FALSE, 0);
        }
        return offset;
    }

    // Writes ARGB pixels into a tiled level (4x4 texel blocks).
    void EncodeRegion(u8 *level, int levelWidth, u8 format, const u32 *pixels, int pitch,
                      int x0, int y0, int width, int height)
    {
        const int blocksPerRow = (levelWidth + 3) / 4;
        for (int y = 0; y < height; ++y)
        {
            const u32 *row = pixels + (size_t)y * pitch;
            const int ty = y0 + y;
            for (int x = 0; x < width; ++x)
            {
                const int tx = x0 + x;
                const u32 argb = row[x];
                const int block = (ty >> 2) * blocksPerRow + (tx >> 2);
                const int texel = ((ty & 3) << 2) | (tx & 3);
                if (format == GX_TF_RGBA8)
                {
                    // 64-byte block: A,R pairs then G,B pairs.
                    u8 *base = level + block * 64 + texel * 2;
                    base[0] = (u8)(argb >> 24);
                    base[1] = (u8)(argb >> 16);
                    base[32] = (u8)(argb >> 8);
                    base[33] = (u8)argb;
                }
                else
                {
                    const u16 rgb565 = (u16)(((argb >> 8) & 0xF800) | ((argb >> 5) & 0x07E0) | ((argb >> 3) & 0x001F));
                    u8 *base = level + block * 32 + texel * 2;
                    base[0] = (u8)(rgb565 >> 8);
                    base[1] = (u8)rgb565;
                }
            }
        }
    }

    // Converts any Virtools image to 32-bit ARGB at the requested size.
    bool ToARGB(const VxImageDescEx &image, int width, int height, XArray<u32> &out)
    {
        VxImageDescEx argb;
        VxPixelFormat2ImageDesc(_32_ARGB8888, argb);
        argb.Width = image.Width;
        argb.Height = image.Height;
        argb.BytesPerLine = image.Width * 4;

        XArray<u32> converted;
        converted.Resize(image.Width * image.Height);
        argb.Image = (XBYTE *)converted.Begin();
        VxDoBlit(image, argb);
        if (image.AlphaMask == 0)
        {
            // No alpha in the source: make it opaque.
            for (int i = 0; i < converted.Size(); ++i)
                converted[i] |= 0xFF000000u;
        }

        if (width == image.Width && height == image.Height)
        {
            out.Swap(converted);
            return true;
        }

        VxImageDescEx resized = argb;
        resized.Width = width;
        resized.Height = height;
        resized.BytesPerLine = width * 4;
        out.Resize(width * height);
        resized.Image = (XBYTE *)out.Begin();
        VxResizeImage32(argb, resized);
        return true;
    }

    u8 WrapMode(CKDWORD mode)
    {
        switch (mode)
        {
        case VXTEXTURE_ADDRESSMIRROR: return GX_MIRROR;
        case VXTEXTURE_ADDRESSCLAMP:
        case VXTEXTURE_ADDRESSBORDER:
        case VXTEXTURE_ADDRESSMIRRORONCE: return GX_CLAMP;
        case VXTEXTURE_ADDRESSWRAP:
        default: return GX_REPEAT;
        }
    }
}

CKBOOL CKGXRasterizerContext::CreateTexture(const CKTextureDesc *desc, CKDWORD *outHandle)
{
    if (outHandle)
        *outHandle = 0;
    if (!CanWork() || !desc || !outHandle || desc->Format.Width <= 0 || desc->Format.Height <= 0)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (desc->Flags & (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP))
        Diag(CKRST_DIAG_APPROX_TEXTURE_OP); // Stored as their first face / slice.

    CKGXResource *resource = new CKGXResource();
    memset(resource, 0, sizeof(CKGXResource));
    resource->Type = CKRST_OBJ_TEXTURE;
    CKGXTexture &texture = resource->Texture;
    texture.Desc = *desc;
    texture.Desc.Flags |= CKRST_TEXTURE_VALID;
    if (texture.Desc.Depth == 0)
        texture.Desc.Depth = 1;
    if (texture.Desc.MipMapCount == 0)
        texture.Desc.MipMapCount = 1;

    texture.Width = NextPowerOfTwo(desc->Format.Width);
    texture.Height = NextPowerOfTwo(desc->Format.Height);
    const CKBOOL renderTarget = (desc->Flags & CKRST_TEXTURE_RENDERTARGET) != 0;
    texture.HasAlpha = renderTarget || (desc->Flags & CKRST_TEXTURE_ALPHA) || desc->Format.AlphaMask != 0;
    texture.Format = texture.HasAlpha ? GX_TF_RGBA8 : GX_TF_RGB565;

    const int maxLevels = LevelCount(texture.Width, texture.Height);
    if (renderTarget)
        texture.Levels = 1;
    else if (texture.Desc.MipMapCount == CKRST_MIPMAP_GENERATE)
    {
        texture.Levels = (u8)maxLevels;
        texture.GenerateMips = TRUE;
    }
    else
        texture.Levels = (u8)(texture.Desc.MipMapCount < (CKDWORD)maxLevels ? texture.Desc.MipMapCount : (CKDWORD)maxLevels);
    if (texture.Levels == 0)
        texture.Levels = 1;

    texture.DataSize = GX_GetTexBufferSize(texture.Width, texture.Height, texture.Format,
                                           texture.Levels > 1 ? GX_TRUE : GX_FALSE, texture.Levels - 1);
    texture.Data = memalign(32, texture.DataSize);
    if (!texture.Data)
    {
        delete resource;
        return FALSE;
    }
    memset(texture.Data, 0, texture.DataSize);
    DCFlushRange(texture.Data, texture.DataSize);
    texture.Loaded = TRUE;
    return Insert(resource, outHandle);
}

void CKGXRasterizerContext::WaitForTextureUse(CKGXTexture &texture)
{
    // Draws already queued this frame may still sample the old contents.
    if (texture.UsedThisFrame)
    {
        GX_DrawDone();
        for (int i = 0; i < m_Resources.Size(); ++i)
        {
            if (m_Resources[i] && m_Resources[i]->Type == CKRST_OBJ_TEXTURE)
                m_Resources[i]->Texture.UsedThisFrame = FALSE;
        }
    }
}

CKBOOL CKGXRasterizerContext::LoadTexture(CKDWORD handle, const VxImageDescEx &image, int mipLevel,
                                          CKRST_CUBEFACE face, const CKRECT *region)
{
    CKGXResource *resource = Find(CKRST_OBJ_TEXTURE, handle);
    if (!CanWork() || !resource)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (!image.Image || image.Width <= 0 || image.Height <= 0 || mipLevel < 0 || mipLevel >= 32)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (face != CKRST_CUBEFACE_XPOS)
        return TRUE; // Only the first cube face / volume slice is kept.

    CKGXTexture &texture = resource->Texture;
    if (mipLevel >= texture.Levels)
        return TRUE; // Levels beyond what GX stores.

    // Level size as the engine sees it, and as stored.
    const int engineWidth = texture.Desc.Format.Width >> mipLevel ? texture.Desc.Format.Width >> mipLevel : 1;
    const int engineHeight = texture.Desc.Format.Height >> mipLevel ? texture.Desc.Format.Height >> mipLevel : 1;
    const int levelWidth = texture.Width >> mipLevel ? texture.Width >> mipLevel : 1;
    const int levelHeight = texture.Height >> mipLevel ? texture.Height >> mipLevel : 1;

    int x = 0, y = 0, width = levelWidth, height = levelHeight;
    if (region)
    {
        // Scale the region from the engine's size to the stored size.
        x = region->left * levelWidth / engineWidth;
        y = region->top * levelHeight / engineHeight;
        width = (region->right * levelWidth + engineWidth - 1) / engineWidth - x;
        height = (region->bottom * levelHeight + engineHeight - 1) / engineHeight - y;
        if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > levelWidth || y + height > levelHeight)
        {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
    }

    XArray<u32> pixels;
    if (!ToARGB(image, width, height, pixels))
        return FALSE;

    WaitForTextureUse(texture);
    u8 *data = (u8 *)texture.Data;
    EncodeRegion(data + LevelOffset(texture, mipLevel), levelWidth, texture.Format, pixels.Begin(), width,
                 x, y, width, height);

    // Build the rest of the chain from a full level 0.
    if (texture.GenerateMips && mipLevel == 0 && !region && texture.Levels > 1)
    {
        XArray<u32> current;
        current.Swap(pixels);
        int w = levelWidth, h = levelHeight;
        for (int level = 1; level < texture.Levels; ++level)
        {
            VxImageDescEx source;
            VxPixelFormat2ImageDesc(_32_ARGB8888, source);
            source.Width = w;
            source.Height = h;
            source.BytesPerLine = w * 4;
            source.Image = (XBYTE *)current.Begin();

            const int nw = w > 1 ? w / 2 : 1;
            const int nh = h > 1 ? h / 2 : 1;
            XArray<u32> next;
            next.Resize(nw * nh);
            if (w > 1 && h > 1)
            {
                VxGenerateMipMap(source, (XBYTE *)next.Begin());
            }
            else
            {
                // One dimension is already 1, so the level is a single row or
                // column: average neighbouring pairs.
                const int last = current.Size() - 1;
                for (int i = 0; i < nw * nh; ++i)
                {
                    const u32 a = current[2 * i < last ? 2 * i : last];
                    const u32 b = current[2 * i + 1 < last ? 2 * i + 1 : last];
                    next[i] = ((a >> 1) & 0x7F7F7F7Fu) + ((b >> 1) & 0x7F7F7F7Fu);
                }
            }
            EncodeRegion(data + LevelOffset(texture, level), nw, texture.Format, next.Begin(), nw, 0, 0, nw, nh);
            current.Swap(next);
            w = nw;
            h = nh;
        }
    }

    DCFlushRange(texture.Data, texture.DataSize);
    texture.Loaded = TRUE;
    m_InvalidateTextures = TRUE;
    ++m_FrameTextureUploads;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::BindTexture(int stage, int texmap)
{
    CKGXResource *resource = Find(CKRST_OBJ_TEXTURE, m_Textures[stage]);
    if (!resource || !resource->Texture.Data)
        return FALSE;
    CKGXTexture &texture = resource->Texture;

    CKDWORD state[CKRST_TSS_MAXSTATE];
    for (int i = 0; i < CKRST_TSS_MAXSTATE; ++i)
        state[i] = (m_StageSetMasks[stage] & (UINT64_C(1) << i)) ? m_StageStates[stage][i]
                                                                 : CKRSTDefaultTextureStageStateValue(stage, (CKRST_TEXTURESTAGESTATETYPE)i);
    const CKDWORD address = state[CKRST_TSS_ADDRESS];
    const u8 wrapS = WrapMode(state[CKRST_TSS_ADDRESSU] ? state[CKRST_TSS_ADDRESSU] : address);
    const u8 wrapT = WrapMode(state[CKRST_TSS_ADDRESSV] ? state[CKRST_TSS_ADDRESSV] : address);

    const bool mipmapped = texture.Levels > 1 && !m_Options.DisableMipmaps;
    u8 magFilter = state[CKRST_TSS_MAGFILTER] == VXTEXTUREFILTER_NEAREST ? GX_NEAR : GX_LINEAR;
    u8 minFilter;
    switch (state[CKRST_TSS_MINFILTER])
    {
    case VXTEXTUREFILTER_NEAREST: minFilter = GX_NEAR; break;
    case VXTEXTUREFILTER_MIPNEAREST: minFilter = mipmapped ? GX_NEAR_MIP_NEAR : GX_NEAR; break;
    case VXTEXTUREFILTER_MIPLINEAR: minFilter = mipmapped ? GX_NEAR_MIP_LIN : GX_NEAR; break;
    case VXTEXTUREFILTER_LINEARMIPNEAREST: minFilter = mipmapped ? GX_LIN_MIP_NEAR : GX_LINEAR; break;
    case VXTEXTUREFILTER_LINEARMIPLINEAR:
    case VXTEXTUREFILTER_ANISOTROPIC: minFilter = mipmapped ? GX_LIN_MIP_LIN : GX_LINEAR; break;
    case VXTEXTUREFILTER_LINEAR:
    default: minFilter = GX_LINEAR; break;
    }
    if (m_Options.DisableTextureFiltering)
    {
        magFilter = GX_NEAR;
        minFilter = mipmapped ? GX_NEAR_MIP_NEAR : GX_NEAR;
    }

    GXTexObj object;
    GX_InitTexObj(&object, texture.Data, texture.Width, texture.Height, texture.Format, wrapS, wrapT,
                  mipmapped ? GX_TRUE : GX_FALSE);
    if (mipmapped)
    {
        float bias;
        const CKDWORD rawBias = state[CKRST_TSS_MIPMAPLODBIAS];
        memcpy(&bias, &rawBias, sizeof(bias));
        u8 anisotropy = GX_ANISO_1;
        if (state[CKRST_TSS_MINFILTER] == VXTEXTUREFILTER_ANISOTROPIC || m_Options.ForceAnisotropicFiltering)
            anisotropy = state[CKRST_TSS_MAXANISOTROPY] >= 4 || m_Options.ForceAnisotropicFiltering ? GX_ANISO_4 : GX_ANISO_2;
        GX_InitTexObjLOD(&object, minFilter, magFilter, 0.0f, (f32)(texture.Levels - 1), bias, GX_FALSE, GX_TRUE, anisotropy);
    }
    else
    {
        GX_InitTexObjFilterMode(&object, minFilter, magFilter);
    }
    GX_LoadTexObj(&object, (u8)texmap);
    texture.UsedThisFrame = TRUE;
    return TRUE;
}
