// Vertex submission: immediate mode through the GX FIFO.

#include "CKGXRasterizer.h"

#include "WiiSystem.h"

#include <math.h>
#include <string.h>

namespace
{
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
