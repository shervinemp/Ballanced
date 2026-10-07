// Vertex submission: immediate mode through the GX FIFO.

#include "CKGXRasterizer.h"

#include "WiiSystem.h"

#include <malloc.h>
#include <math.h>
#include <string.h>

// libogc's console font: 256 characters of 8x16 pixels, one byte per row,
// leftmost pixel in the top bit.
extern "C" u8 console_font_8x16[];

namespace
{
    // Printable ASCII (32..127) from the console font, 16 characters per row
    // of a 128x96 intensity texture.
    const int kFontColumns = 16;
    const int kFontWidth = kFontColumns * 8;
    const int kFontHeight = 6 * 16;

    u8 *OverlayFont()
    {
        static u8 *s_Font = NULL;
        if (s_Font)
            return s_Font;
        s_Font = (u8 *)memalign(32, kFontWidth * kFontHeight);
        if (!s_Font)
            return NULL;
        for (int y = 0; y < kFontHeight; ++y)
        {
            for (int x = 0; x < kFontWidth; ++x)
            {
                const int c = 32 + (y / 16) * kFontColumns + x / 8;
                const bool set = (console_font_8x16[c * 16 + y % 16] & (0x80 >> (x % 8))) != 0;
                // I8 textures are stored in 8x4 tiles.
                const int tile = (y / 4) * (kFontWidth / 8) + x / 8;
                s_Font[tile * 32 + (y % 4) * 8 + x % 8] = set ? 0xFF : 0x00;
            }
        }
        DCFlushRange(s_Font, kFontWidth * kFontHeight);
        return s_Font;
    }

    void OverlayQuad(float x0, float y0, float x1, float y1, u32 color)
    {
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position3f32(x0, y0, 0.0f);
        GX_Color1u32(color);
        GX_Position3f32(x1, y0, 0.0f);
        GX_Color1u32(color);
        GX_Position3f32(x1, y1, 0.0f);
        GX_Color1u32(color);
        GX_Position3f32(x0, y1, 0.0f);
        GX_Color1u32(color);
        GX_End();
    }
    u8 GXPrimitive(VXPRIMITIVETYPE type)
    {
        switch (type)
        {
        case VX_POINTLIST: return GX_POINTS;
        case VX_LINELIST: return GX_LINES;
        case VX_LINESTRIP: return GX_LINESTRIP;
        case VX_TRIANGLESTRIP: return GX_TRIANGLESTRIP;
        case VX_TRIANGLEFAN: return GX_TRIANGLEFAN;
        case VX_TRIANGLELIST:
        default: return GX_TRIANGLES;
        }
    }

    CKDWORD PrimitiveCount(VXPRIMITIVETYPE type, int count)
    {
        switch (type)
        {
        case VX_POINTLIST: return (CKDWORD)count;
        case VX_LINELIST: return (CKDWORD)(count / 2);
        case VX_LINESTRIP: return count > 1 ? (CKDWORD)(count - 1) : 0;
        case VX_TRIANGLELIST: return (CKDWORD)(count / 3);
        case VX_TRIANGLESTRIP:
        case VX_TRIANGLEFAN: return count > 2 ? (CKDWORD)(count - 2) : 0;
        default: return 0;
        }
    }

    // The largest GX_Begin batch that keeps whole primitives together.
    int BatchSize(VXPRIMITIVETYPE type)
    {
        switch (type)
        {
        case VX_TRIANGLELIST: return 65535;   // multiple of 3
        case VX_LINELIST: return 65534;       // multiple of 2
        case VX_POINTLIST: return 65535;
        default: return 0;                    // strips and fans cannot be split
        }
    }

    // Direct3D colours are ARGB dwords; GX reads R, G, B, A bytes.
    inline u32 ToRGBA(u32 argb)
    {
        return (argb << 8) | (argb >> 24);
    }
}

void CKGXRasterizerContext::BuildVertexSource(CKDWORD format, const CKBYTE *data, CKDWORD stride,
                                              const CKBYTE *texcoordDims, VertexSource &source) const
{
    CKRSTVertexLayout layout;
    CKRSTGetVertexLayout(format, texcoordDims, &layout);

    memset(&source, 0, sizeof(source));
    source.Format = format;
    source.Position = data + layout.PositionOffset;
    source.PositionStride = stride;
    if (layout.NormalOffset >= 0)
    {
        source.Normal = data + layout.NormalOffset;
        source.NormalStride = stride;
    }
    if (layout.DiffuseOffset >= 0)
    {
        source.Diffuse = data + layout.DiffuseOffset;
        source.DiffuseStride = stride;
    }
    for (int i = 0; i < layout.TexcoordCount; ++i)
    {
        source.Texcoord[i] = data + layout.TexcoordOffset[i];
        source.TexcoordStride[i] = stride;
    }
    source.TexcoordCount = layout.TexcoordCount;
}

CKBOOL CKGXRasterizerContext::Submit(VXPRIMITIVETYPE type, const VertexSource &source, CKDWORD vertexCount,
                                     const CKWORD *indices, int indexCount, CKDWORD baseVertex)
{
    const int count = indices ? indexCount : (int)vertexCount;
    ++m_FrameDrawCalls;
    m_FramePrimitives += PrimitiveCount(type, count);
    m_FrameOpen = TRUE;
    if (m_Options.DebugFlags & CKRST_DEBUG_IFH)
        return TRUE;

    const CKBOOL pretransformed = (source.Format & CKRST_DP_TRANSFORM) == 0;
    CKBOOL lit = FALSE;
    ApplyViewport(pretransformed);
    ApplyTransforms(pretransformed);
    ApplyChannels(source, pretransformed, &lit);
    ApplyTextureStages(source, pretransformed, lit);
    ApplyPixelState(pretransformed);

    for (int i = 0; i < 8; ++i)
    {
        if (m_RenderStates[VXRENDERSTATE_WRAP0 + i])
        {
            Diag(CKRST_DIAG_IGNORE_WRAP);
            break;
        }
    }

    // Vertex layout. Normals also feed sphere-map texture generation.
    const bool sendNormal = !pretransformed && source.Normal != NULL;
    const bool sendColor = source.Diffuse != NULL;
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    if (sendNormal)
    {
        GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    }
    if (sendColor)
    {
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    }
    int texcoordSets = 0;
    for (int i = 0; i < source.TexcoordCount && i < 8; ++i)
    {
        if (!source.Texcoord[i])
            break;
        GX_SetVtxDesc(GX_VA_TEX0 + i, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0 + i, GX_TEX_ST, GX_F32, 0);
        texcoordSets = i + 1;
    }

    auto vertexIndex = [&](int i) -> CKDWORD {
        CKDWORD index = indices ? (CKDWORD)indices[i] : (CKDWORD)i;
        index += baseVertex;
        return index < vertexCount ? index : 0;
    };
    // Position and texture coordinates from one vertex, normal and colour
    // from another (the same one unless flat shading).
    auto emit = [&](CKDWORD index, CKDWORD attributes) {
        const float *position = (const float *)(source.Position + (size_t)index * source.PositionStride);
        GX_Position3f32(position[0], position[1], position[2]);
        if (sendNormal)
        {
            const float *normal = (const float *)(source.Normal + (size_t)attributes * source.NormalStride);
            GX_Normal3f32(normal[0], normal[1], normal[2]);
        }
        if (sendColor)
            GX_Color1u32(ToRGBA(*(const u32 *)(source.Diffuse + (size_t)attributes * source.DiffuseStride)));
        for (int set = 0; set < texcoordSets; ++set)
        {
            const float *uv = (const float *)(source.Texcoord[set] + (size_t)index * source.TexcoordStride[set]);
            GX_TexCoord2f32(uv[0], uv[1]);
        }
    };

    // GX always interpolates. Direct3D flat shading gives each triangle the
    // colour of its first vertex (the second for fans), so send separate
    // triangles that repeat it; strips keep their alternating winding.
    const bool flat = m_RenderStates[VXRENDERSTATE_SHADEMODE] == VXSHADE_FLAT && (sendColor || sendNormal) &&
                      (type == VX_TRIANGLELIST || type == VX_TRIANGLESTRIP || type == VX_TRIANGLEFAN);
    if (flat)
    {
        const int triangles = (int)PrimitiveCount(type, count);
        const int perBatch = 65535 / 3;
        for (int first = 0; first < triangles; first += perBatch)
        {
            const int n = (triangles - first) < perBatch ? (triangles - first) : perBatch;
            GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)(n * 3));
            for (int t = first; t < first + n; ++t)
            {
                int v[3];
                int provoking;
                if (type == VX_TRIANGLESTRIP)
                {
                    v[0] = (t & 1) ? t + 1 : t;
                    v[1] = (t & 1) ? t : t + 1;
                    v[2] = t + 2;
                    provoking = t;
                }
                else if (type == VX_TRIANGLEFAN)
                {
                    v[0] = 0;
                    v[1] = t + 1;
                    v[2] = t + 2;
                    provoking = t + 1;
                }
                else
                {
                    v[0] = 3 * t;
                    v[1] = 3 * t + 1;
                    v[2] = 3 * t + 2;
                    provoking = 3 * t;
                }
                const CKDWORD attributes = vertexIndex(provoking);
                for (int k = 0; k < 3; ++k)
                    emit(vertexIndex(v[k]), attributes);
            }
            GX_End();
        }
        return TRUE;
    }

    const u8 primitive = GXPrimitive(type);
    int batch = BatchSize(type);
    if (batch == 0)
    {
        batch = count;
        if (batch > 65535)
        {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            batch = 65535;
        }
    }

    for (int start = 0; start < count; start += batch)
    {
        const int n = (count - start) < batch ? (count - start) : batch;
        GX_Begin(primitive, GX_VTXFMT0, (u16)n);
        for (int i = 0; i < n; ++i)
        {
            const CKDWORD index = vertexIndex(start + i);
            emit(index, index);
        }
        GX_End();
    }
    return TRUE;
}

// A screen-space rectangle with fixed state, for clears.
void CKGXRasterizerContext::DrawScreenQuad(float x0, float y0, float x1, float y1, float z, CKDWORD color,
                                           CKBOOL writeColor, CKBOOL writeDepth)
{
    const int width = m_Target ? m_TargetWidth : m_Width;
    const int height = m_Target ? m_TargetHeight : m_Height;
    GX_SetViewport(0.0f, 0.0f, (f32)width, (f32)height, 0.0f, 1.0f);
    GX_SetScissor(0, 0, width, height);
    ApplyTransforms(TRUE);

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    GX_SetZMode(GX_TRUE, GX_ALWAYS, writeDepth ? GX_TRUE : GX_FALSE);
    GX_SetZCompLoc(GX_TRUE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetColorUpdate(writeColor ? GX_TRUE : GX_FALSE);
    GX_SetAlphaUpdate(writeColor ? GX_TRUE : GX_FALSE);
    GX_SetCullMode(GX_CULL_NONE);
    GXColor none = {0, 0, 0, 0};
    GX_SetFog(GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, none);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    const u32 rgba = ToRGBA(color);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x0, y0, z);
    GX_Color1u32(rgba);
    GX_Position3f32(x1, y0, z);
    GX_Color1u32(rgba);
    GX_Position3f32(x1, y1, z);
    GX_Color1u32(rgba);
    GX_Position3f32(x0, y1, z);
    GX_Color1u32(rgba);
    GX_End();

    GX_SetColorUpdate(GX_TRUE);
}

// System screens over the picture: the on-screen keyboard and the HOME menu.
void CKGXRasterizerContext::DrawOverlay()
{
    int total = 0;
    for (int layer = 0; layer < wiisystem::OVERLAY_LAYER_COUNT; ++layer)
        total += wiisystem::GetOverlay((wiisystem::OverlayLayer)layer, NULL);
    u8 *font = total > 0 ? OverlayFont() : NULL;
    if (!font)
        return;

    GX_SetViewport(0.0f, 0.0f, (f32)m_Width, (f32)m_Height, 0.0f, 1.0f);
    GX_SetScissor(0, 0, m_Width, m_Height);
    ApplyTransforms(TRUE);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTevStages(1);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetCullMode(GX_CULL_NONE);
    GXColor none = {0, 0, 0, 0};
    GX_SetFog(GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, none);

    GXTexObj fontObject;
    GX_InitTexObj(&fontObject, font, kFontWidth, kFontHeight, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&fontObject, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&fontObject, GX_TEXMAP0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    for (int layer = 0; layer < wiisystem::OVERLAY_LAYER_COUNT; ++layer)
    {
        const wiisystem::OverlayItem *items = NULL;
        const int count = wiisystem::GetOverlay((wiisystem::OverlayLayer)layer, &items);
        for (int i = 0; i < count; ++i)
        {
            const wiisystem::OverlayItem &item = items[i];

            // Box: fill, then a two-pixel border.
            GX_SetNumTexGens(0);
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
            GX_ClearVtxDesc();
            GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
            GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
            GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
            GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
            if (item.Fill & 0xFF)
                OverlayQuad(item.X0, item.Y0, item.X1, item.Y1, item.Fill);
            if (item.Border & 0xFF)
            {
                const float b = 2.0f;
                OverlayQuad(item.X0, item.Y0, item.X1, item.Y0 + b, item.Border);
                OverlayQuad(item.X0, item.Y1 - b, item.X1, item.Y1, item.Border);
                OverlayQuad(item.X0, item.Y0 + b, item.X0 + b, item.Y1 - b, item.Border);
                OverlayQuad(item.X1 - b, item.Y0 + b, item.X1, item.Y1 - b, item.Border);
            }

            // Text, centered: the glyph texture's intensity is the coverage.
            const int length = (int)strnlen(item.Text, sizeof(item.Text));
            if (length == 0 || !(item.TextColor & 0xFF))
                continue;
            GX_SetNumTexGens(1);
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
            GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
            GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

            const float scale = item.TextScale > 0.0f ? item.TextScale : 1.0f;
            const float cw = 8.0f * scale;
            const float ch = 16.0f * scale;
            float x = floorf((item.X0 + item.X1 - length * cw) * 0.5f);
            const float y = floorf((item.Y0 + item.Y1 - ch) * 0.5f);
            GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(length * 4));
            for (int c = 0; c < length; ++c, x += cw)
            {
                int glyph = (u8)item.Text[c] - 32;
                if (glyph < 0 || glyph >= kFontColumns * 6)
                    glyph = '?' - 32;
                const float u0 = (float)(glyph % kFontColumns * 8) / kFontWidth;
                const float v0 = (float)(glyph / kFontColumns * 16) / kFontHeight;
                const float u1 = u0 + 8.0f / kFontWidth;
                const float v1 = v0 + 16.0f / kFontHeight;
                GX_Position3f32(x, y, 0.0f);
                GX_Color1u32(item.TextColor);
                GX_TexCoord2f32(u0, v0);
                GX_Position3f32(x + cw, y, 0.0f);
                GX_Color1u32(item.TextColor);
                GX_TexCoord2f32(u1, v0);
                GX_Position3f32(x + cw, y + ch, 0.0f);
                GX_Color1u32(item.TextColor);
                GX_TexCoord2f32(u1, v1);
                GX_Position3f32(x, y + ch, 0.0f);
                GX_Color1u32(item.TextColor);
                GX_TexCoord2f32(u0, v1);
            }
            GX_End();
        }
    }
}

// The Wii Remote pointer, drawn over the picture while the game shows its cursor.
void CKGXRasterizerContext::DrawPointer()
{
    float x, y, angle;
    if (!wiisystem::GetPointer(&x, &y, &angle))
        return;

    // Arrow outline (tip at the origin), in pixels.
    static const float kArrow[][2] = {
        {0.0f, 0.0f}, {0.0f, 19.0f}, {5.0f, 15.0f}, {9.0f, 23.0f},
        {12.0f, 21.5f}, {8.5f, 13.5f}, {14.0f, 13.5f},
    };
    // Triangles of the (concave) arrow.
    static const int kTriangles[][3] = {
        {0, 1, 2}, {0, 2, 5}, {0, 5, 6}, {2, 3, 4}, {2, 4, 5},
    };

    const float radians = angle * (float)M_PI / 180.0f;
    const float cs = cosf(radians);
    const float sn = sinf(radians);

    GX_SetViewport(0.0f, 0.0f, (f32)m_Width, (f32)m_Height, 0.0f, 1.0f);
    GX_SetScissor(0, 0, m_Width, m_Height);
    ApplyTransforms(TRUE);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetCullMode(GX_CULL_NONE);
    GXColor none = {0, 0, 0, 0};
    GX_SetFog(GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, none);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    // A dark, slightly larger copy first gives the white arrow an outline.
    const struct
    {
        float Scale;
        float OffsetX;
        float OffsetY;
        u32 Color;
    } passes[] = {
        {1.18f, -1.2f, -1.6f, 0x202020FF},
        {1.0f, 0.0f, 0.0f, 0xFFFFFFFF},
    };
    const int triangleCount = (int)(sizeof(kTriangles) / sizeof(kTriangles[0]));
    for (size_t pass = 0; pass < sizeof(passes) / sizeof(passes[0]); ++pass)
    {
        GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)(triangleCount * 3));
        for (int t = 0; t < triangleCount; ++t)
        {
            for (int v = 0; v < 3; ++v)
            {
                const float px = kArrow[kTriangles[t][v]][0] * passes[pass].Scale + passes[pass].OffsetX;
                const float py = kArrow[kTriangles[t][v]][1] * passes[pass].Scale + passes[pass].OffsetY;
                GX_Position3f32(x + px * cs - py * sn, y + px * sn + py * cs, 0.0f);
                GX_Color1u32(passes[pass].Color);
            }
        }
        GX_End();
    }
}
