// Translation of the Virtools (Direct3D 7-style) fixed-function state into GX.

#include "CKGXRasterizer.h"

#include <math.h>
#include <string.h>

namespace
{
    const float kWidescreenScale = 0.75f; // 4:3 picture shown on a 16:9 set

    GXColor ToGXColor(CKDWORD argb)
    {
        GXColor color = {(u8)(argb >> 16), (u8)(argb >> 8), (u8)argb, (u8)(argb >> 24)};
        return color;
    }

    u8 ToByte(float value)
    {
        if (value <= 0.0f)
            return 0;
        if (value >= 1.0f)
            return 255;
        return (u8)(value * 255.0f + 0.5f);
    }

    GXColor ToGXColor(const VxColor &color)
    {
        GXColor result = {ToByte(color.r), ToByte(color.g), ToByte(color.b), ToByte(color.a)};
        return result;
    }

    float AsFloat(CKDWORD value)
    {
        float result;
        memcpy(&result, &value, sizeof(result));
        return result;
    }

    u8 CompareFunction(CKDWORD cmp)
    {
        switch (cmp)
        {
        case VXCMP_NEVER: return GX_NEVER;
        case VXCMP_LESS: return GX_LESS;
        case VXCMP_EQUAL: return GX_EQUAL;
        case VXCMP_LESSEQUAL: return GX_LEQUAL;
        case VXCMP_GREATER: return GX_GREATER;
        case VXCMP_NOTEQUAL: return GX_NEQUAL;
        case VXCMP_GREATEREQUAL: return GX_GEQUAL;
        case VXCMP_ALWAYS:
        default: return GX_ALWAYS;
        }
    }

    // GX reads source factors against the destination colour and vice versa,
    // so "colour" factors exist only on the side where they are legal.
    u8 SourceBlend(CKDWORD blend)
    {
        switch (blend)
        {
        case VXBLEND_ZERO: return GX_BL_ZERO;
        case VXBLEND_ONE: return GX_BL_ONE;
        case VXBLEND_SRCALPHA:
        case VXBLEND_SRCALPHASAT:
        case VXBLEND_BOTHSRCALPHA: return GX_BL_SRCALPHA;
        case VXBLEND_INVSRCALPHA:
        case VXBLEND_BOTHINVSRCALPHA: return GX_BL_INVSRCALPHA;
        case VXBLEND_DESTALPHA: return GX_BL_DSTALPHA;
        case VXBLEND_INVDESTALPHA: return GX_BL_INVDSTALPHA;
        case VXBLEND_DESTCOLOR: return GX_BL_DSTCLR;
        case VXBLEND_INVDESTCOLOR: return GX_BL_INVDSTCLR;
        case VXBLEND_SRCCOLOR: return GX_BL_ONE;      // Not a GX source factor
        case VXBLEND_INVSRCCOLOR: return GX_BL_ZERO;  // Not a GX source factor
        default: return GX_BL_ONE;
        }
    }

    u8 DestinationBlend(CKDWORD blend, CKDWORD sourceBlend)
    {
        // BOTH* source modes force the matching destination factor.
        if (sourceBlend == VXBLEND_BOTHSRCALPHA)
            return GX_BL_INVSRCALPHA;
        if (sourceBlend == VXBLEND_BOTHINVSRCALPHA)
            return GX_BL_SRCALPHA;
        switch (blend)
        {
        case VXBLEND_ZERO: return GX_BL_ZERO;
        case VXBLEND_ONE: return GX_BL_ONE;
        case VXBLEND_SRCCOLOR: return GX_BL_SRCCLR;
        case VXBLEND_INVSRCCOLOR: return GX_BL_INVSRCCLR;
        case VXBLEND_SRCALPHA: return GX_BL_SRCALPHA;
        case VXBLEND_INVSRCALPHA: return GX_BL_INVSRCALPHA;
        case VXBLEND_DESTALPHA: return GX_BL_DSTALPHA;
        case VXBLEND_INVDESTALPHA: return GX_BL_INVDSTALPHA;
        case VXBLEND_DESTCOLOR: return GX_BL_ONE;      // Not a GX destination factor
        case VXBLEND_INVDESTCOLOR: return GX_BL_ZERO;  // Not a GX destination factor
        default: return GX_BL_ZERO;
        }
    }

    // Virtools uses row vectors (v * World * View) and looks down +Z; GX uses
    // column vectors and looks down -Z.
    void ToGXModelView(const VxMatrix &world, const VxMatrix &view, Mtx out)
    {
        float mv[4][4];
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
                mv[i][j] = world[i][0] * view[0][j] + world[i][1] * view[1][j] +
                           world[i][2] * view[2][j] + world[i][3] * view[3][j];
        }
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 4; ++c)
                out[r][c] = mv[c][r];
        }
        for (int c = 0; c < 4; ++c)
            out[2][c] = -out[2][c];
    }

    // Normals need the inverse transpose; GX does not renormalize them, so the
    // matrix is rescaled to keep unit normals unit length under uniform scale.
    void ToGXNormalMatrix(Mtx modelView, Mtx out)
    {
        Mtx inverse;
        if (!guMtxInverse(modelView, inverse))
        {
            guMtxCopy(modelView, out);
            return;
        }
        guMtxTranspose(inverse, out);
        out[0][3] = out[1][3] = out[2][3] = 0.0f;
        float length = 0.0f;
        for (int r = 0; r < 3; ++r)
            length += sqrtf(out[r][0] * out[r][0] + out[r][1] * out[r][1] + out[r][2] * out[r][2]);
        length /= 3.0f;
        if (length > 0.000001f)
        {
            const float scale = 1.0f / length;
            for (int r = 0; r < 3; ++r)
            {
                for (int c = 0; c < 3; ++c)
                    out[r][c] *= scale;
            }
        }
    }

    // Direct3D projection (row vectors, depth 0..1) to GX (column vectors,
    // camera looking down -Z, depth -1..0).
    u8 ToGXProjection(const VxMatrix &p, Mtx44 out, bool widescreen)
    {
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
                out[r][c] = p[c][r];
        }
        for (int r = 0; r < 4; ++r)
            out[r][2] = -out[r][2];
        for (int c = 0; c < 4; ++c)
            out[2][c] -= out[3][c];

        const bool perspective = p[3][3] == 0.0f;
        if (perspective && widescreen)
        {
            for (int c = 0; c < 4; ++c)
                out[0][c] *= kWidescreenScale;
        }
        return perspective ? GX_PERSPECTIVE : GX_ORTHOGRAPHIC;
    }

    void TransformPoint(const VxMatrix &m, const VxVector &v, guVector &out)
    {
        out.x = v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + m[3][0];
        out.y = v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + m[3][1];
        out.z = -(v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + m[3][2]);
    }

    void TransformDirection(const VxMatrix &m, const VxVector &v, guVector &out)
    {
        out.x = v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0];
        out.y = v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1];
        out.z = -(v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2]);
        guVecNormalize(&out);
    }

    // Default combine of a stage from its legacy TEXTUREMAPBLEND (as CKFFPLib).
    struct StageOps
    {
        CKDWORD ColorOp, ColorArg0, ColorArg1, ColorArg2;
        CKDWORD AlphaOp, AlphaArg0, AlphaArg1, AlphaArg2;
        CKDWORD ResultArg;
    };

    StageOps LegacyBlendOps(CKDWORD blend)
    {
        StageOps ops = {CKRST_TOP_MODULATE, CKRST_TA_CURRENT, CKRST_TA_TEXTURE, CKRST_TA_CURRENT,
                        CKRST_TOP_MODULATE, CKRST_TA_CURRENT, CKRST_TA_TEXTURE, CKRST_TA_CURRENT,
                        CKRST_TA_CURRENT};
        switch (blend & VXTEXTUREBLEND_MASK)
        {
        case VXTEXTUREBLEND_DECAL:
        case VXTEXTUREBLEND_COPY:
            ops.ColorOp = CKRST_TOP_SELECTARG1;
            ops.AlphaOp = CKRST_TOP_SELECTARG1;
            break;
        case VXTEXTUREBLEND_DECALALPHA:
        case VXTEXTUREBLEND_DECALMASK:
            ops.ColorOp = CKRST_TOP_BLENDTEXTUREALPHA;
            ops.AlphaOp = CKRST_TOP_SELECTARG1;
            ops.AlphaArg1 = CKRST_TA_DIFFUSE;
            break;
        case VXTEXTUREBLEND_ADD:
            ops.ColorOp = CKRST_TOP_ADD;
            ops.AlphaOp = CKRST_TOP_SELECTARG1;
            ops.AlphaArg1 = CKRST_TA_CURRENT;
            break;
        case VXTEXTUREBLEND_DOTPRODUCT3:
            ops.ColorOp = CKRST_TOP_DOTPRODUCT3;
            ops.ColorArg2 = CKRST_TA_TFACTOR;
            ops.AlphaOp = CKRST_TOP_SELECTARG1;
            ops.AlphaArg1 = CKRST_TA_CURRENT;
            break;
        default:
            break;
        }
        return ops;
    }

    // One GX TEV stage input set: d + ((1 - c) * a + c * b).
    struct TevInputs
    {
        u8 a, b, c, d;
    };

    enum
    {
        TA_BASE_MASK = 0x0F,
    };
}

// ---------------------------------------------------------------------------
// Transforms

void CKGXRasterizerContext::ApplyTransforms(CKBOOL pretransformed)
{
    if (pretransformed)
    {
        // Screen-space vertices: pixels to clip space, depth 0..1 to -1..0.
        const float width = (float)(m_Target ? m_TargetWidth : m_Width);
        const float height = (float)(m_Target ? m_TargetHeight : m_Height);
        Mtx44 ortho = {
            {2.0f / width, 0.0f, 0.0f, -1.0f},
            {0.0f, -2.0f / height, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f, -1.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        };
        GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
        Mtx identity;
        guMtxIdentity(identity);
        GX_LoadPosMtxImm(identity, GX_PNMTX0);
        GX_LoadNrmMtxImm(identity, GX_PNMTX0);
        GX_SetCurrentMtx(GX_PNMTX0);
        return;
    }

    Mtx44 projection;
    const u8 type = ToGXProjection(m_Matrices[CKRSTMatrixSlot(VXMATRIX_PROJECTION)], projection,
                                   m_Widescreen && !m_Target);
    GX_LoadProjectionMtx(projection, type);

    Mtx modelView, normal;
    ToGXModelView(m_Matrices[CKRSTMatrixSlot(VXMATRIX_WORLD)], m_Matrices[CKRSTMatrixSlot(VXMATRIX_VIEW)], modelView);
    ToGXNormalMatrix(modelView, normal);
    GX_LoadPosMtxImm(modelView, GX_PNMTX0);
    GX_LoadNrmMtxImm(normal, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    if (m_RenderStates[VXRENDERSTATE_VERTEXBLEND] != 0)
        Diag(CKRST_DIAG_APPROX_VERTEX_BLEND_WEIGHTS);
}

// ---------------------------------------------------------------------------
// Lights: GX evaluates them in eye space.

void CKGXRasterizerContext::ApplyLights()
{
    if (!m_LightsDirty)
        return;
    m_LightsDirty = FALSE;

    const VxMatrix &view = m_Matrices[CKRSTMatrixSlot(VXMATRIX_VIEW)];
    for (int i = 0; i < CKRST_MAX_LIGHTS; ++i)
    {
        const CKLightData &light = m_Lights[i];
        GXLightObj object;
        GX_InitLightColor(&object, ToGXColor(light.Diffuse));

        if (light.Type == VX_LIGHTDIREC || light.Type == VX_LIGHTPARA)
        {
            // A light infinitely far away along the reverse of its direction.
            guVector direction;
            TransformDirection(view, light.Direction, direction);
            GX_InitLightPos(&object, -direction.x * 1.0e18f, -direction.y * 1.0e18f, -direction.z * 1.0e18f);
            GX_InitLightAttn(&object, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        }
        else
        {
            guVector position;
            TransformPoint(view, light.Position, position);
            GX_InitLightPos(&object, position.x, position.y, position.z);

            // Legacy Virtools/DX5 intensity coefficients to 1 / (k0 + k1 d + k2 d^2),
            // the conversion the original DX8 rasterizer applied.
            float k0 = light.Attenuation0;
            float k1 = light.Attenuation1;
            float k2 = light.Attenuation2;
            const double sum = (double)light.Attenuation0 + light.Attenuation1 + light.Attenuation2;
            if (sum > 0.0 && light.Range > 0.0f)
            {
                const double constant = 1.0 / sum;
                const double range = light.Range;
                const double linear = (2.0 * light.Attenuation2 + light.Attenuation1) * (constant / range) * constant;
                const double quadratic = constant * light.Attenuation2 * constant / (range * range) +
                                         linear * linear / constant;
                k0 = (float)constant;
                k1 = (float)linear;
                k2 = (float)quadratic;
            }
            if (k0 <= 0.0f && k1 <= 0.0f && k2 <= 0.0f)
                k0 = 1.0f;

            if (light.Type == VX_LIGHTSPOT)
            {
                guVector direction;
                TransformDirection(view, light.Direction, direction);
                GX_InitLightDir(&object, direction.x, direction.y, direction.z);
                const float cutoff = light.OuterSpotCone * 0.5f * 180.0f / (float)M_PI;
                // Angular falloff from the cone, distance falloff from the converted terms.
                GX_InitLightSpot(&object, cutoff > 90.0f ? 90.0f : cutoff, GX_SP_COS2);
                GX_InitLightAttnK(&object, k0, k1, k2);
            }
            else
            {
                GX_InitLightAttn(&object, 1.0f, 0.0f, 0.0f, k0, k1, k2);
            }
        }
        GX_LoadLightObj(&object, (u8)(GX_LIGHT0 << i));
    }
}

// ---------------------------------------------------------------------------
// Colour channels: vertex lighting and material colours.

void CKGXRasterizerContext::ApplyChannels(const VertexSource &source, CKBOOL pretransformed, CKBOOL *litOut)
{
    const CKBOOL hasNormals = !pretransformed && (source.Format & CKRST_DP_LIGHT) && source.Normal;
    const CKBOOL lighting = m_RenderStates[VXRENDERSTATE_LIGHTING] && hasNormals;
    const CKBOOL vertexColor = source.Diffuse != NULL;

    GX_SetNumChans(1);
    if (!lighting)
    {
        // Unlit: vertex colour, or white.
        GXColor white = {255, 255, 255, 255};
        GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, vertexColor ? GX_SRC_VTX : GX_SRC_REG,
                       GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
        GX_SetChanMatColor(GX_COLOR0A0, white);
        *litOut = FALSE;
        return;
    }

    ApplyLights();
    u8 lightMask = 0;
    VxColor lightAmbient(0.0f, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < CKRST_MAX_LIGHTS; ++i)
    {
        if (!m_LightEnabled[i])
            continue;
        lightMask |= (u8)(GX_LIGHT0 << i);
        lightAmbient.r += m_Lights[i].Ambient.r;
        lightAmbient.g += m_Lights[i].Ambient.g;
        lightAmbient.b += m_Lights[i].Ambient.b;
    }

    const CKBOOL diffuseFromVertex = vertexColor && m_RenderStates[VXRENDERSTATE_COLORVERTEX] &&
                                     m_RenderStates[VXRENDERSTATE_DIFFUSEFROMVERTEX];
    GX_SetChanCtrl(GX_COLOR0, GX_ENABLE, GX_SRC_REG, diffuseFromVertex ? GX_SRC_VTX : GX_SRC_REG, lightMask,
                   GX_DF_CLAMP, GX_AF_SPOT);
    GX_SetChanCtrl(GX_ALPHA0, GX_DISABLE, GX_SRC_REG, diffuseFromVertex ? GX_SRC_VTX : GX_SRC_REG,
                   GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetChanMatColor(GX_COLOR0A0, ToGXColor(m_Material.Diffuse));
    GXColor black = {0, 0, 0, 0};
    GX_SetChanAmbColor(GX_COLOR0A0, black);

    // Direct3D adds emissive and ambient light outside the diffuse term; GX
    // multiplies its ambient by the material, so the TEV adds this instead.
    const CKDWORD globalAmbient = m_RenderStates[VXRENDERSTATE_AMBIENT];
    VxColor ambient;
    ambient.r = m_Material.Emissive.r + m_Material.Ambient.r * (((globalAmbient >> 16) & 0xFF) / 255.0f + lightAmbient.r);
    ambient.g = m_Material.Emissive.g + m_Material.Ambient.g * (((globalAmbient >> 8) & 0xFF) / 255.0f + lightAmbient.g);
    ambient.b = m_Material.Emissive.b + m_Material.Ambient.b * ((globalAmbient & 0xFF) / 255.0f + lightAmbient.b);
    ambient.a = 0.0f;
    GX_SetTevKColor(GX_KCOLOR3, ToGXColor(ambient));

    if (m_RenderStates[VXRENDERSTATE_SPECULARENABLE])
        Diag(CKRST_DIAG_APPROX_TEXTURE_OP);
    *litOut = TRUE;
}

// ---------------------------------------------------------------------------
// Texture stages -> TEV

namespace
{
    struct TevBuilder
    {
        int Stage;          // Next GX TEV stage
        bool Lit;           // Diffuse comes from TEVREG2 (lighting + ambient)

        u8 ColorDiffuse(bool alpha) const
        {
            if (Lit)
                return alpha ? GX_CC_A2 : GX_CC_C2;
            return alpha ? GX_CC_RASA : GX_CC_RASC;
        }

        u8 AlphaDiffuse() const
        {
            return Lit ? GX_CA_A2 : GX_CA_RASA;
        }
    };
}

int CKGXRasterizerContext::ApplyTextureStages(const VertexSource &source, CKBOOL pretransformed, CKBOOL lit)
{
    TevBuilder tev;
    tev.Stage = 0;
    tev.Lit = lit;

    const CKDWORD tfactor = m_RenderStates[VXRENDERSTATE_TEXTUREFACTOR];
    GX_SetTevKColor(GX_KCOLOR0, ToGXColor(tfactor));

    // Lighting pre-stage: diffuse = lit colour + ambient/emissive, into TEVREG2.
    if (lit)
    {
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K3);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_KONST, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG2);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG2);
        tev.Stage = 1;
    }

    int texGens = 0;
    bool anyStage = false;
    for (int s = 0; s < CKRST_MAX_TEXTURE_STAGES && tev.Stage < 15; ++s)
    {
        const CKDWORD *st = m_StageStates[s];
        const uint64_t setMask = m_StageSetMasks[s];
        CKGXResource *resource = m_Textures[s] ? Find(CKRST_OBJ_TEXTURE, m_Textures[s]) : NULL;
        const bool hasTexture = resource && resource->Texture.Loaded && resource->Texture.Data;
        const StageOps legacy = LegacyBlendOps(st[CKRST_TSS_TEXTUREMAPBLEND]);

        #define STAGE_SET(state) ((setMask & (UINT64_C(1) << (CKDWORD)(state))) != 0)
        CKDWORD colorOp = st[CKRST_TSS_OP] ? st[CKRST_TSS_OP] : (hasTexture ? legacy.ColorOp : (CKDWORD)CKRST_TOP_DISABLE);
        if (colorOp == CKRST_TOP_DISABLE)
            break;
        CKDWORD alphaOp = st[CKRST_TSS_AOP] ? st[CKRST_TSS_AOP] : (hasTexture ? legacy.AlphaOp : (CKDWORD)CKRST_TOP_DISABLE);
        const CKDWORD colorArg1 = (st[CKRST_TSS_ARG1] || STAGE_SET(CKRST_TSS_ARG1)) ? st[CKRST_TSS_ARG1] : (hasTexture ? legacy.ColorArg1 : (CKDWORD)CKRST_TA_DIFFUSE);
        const CKDWORD colorArg2 = (st[CKRST_TSS_ARG2] || STAGE_SET(CKRST_TSS_ARG2)) ? st[CKRST_TSS_ARG2] : legacy.ColorArg2;
        const CKDWORD colorArg0 = (st[CKRST_TSS_COLORARG0] || STAGE_SET(CKRST_TSS_COLORARG0)) ? st[CKRST_TSS_COLORARG0] : legacy.ColorArg0;
        CKDWORD alphaArg1 = (st[CKRST_TSS_AARG1] || STAGE_SET(CKRST_TSS_AARG1)) ? st[CKRST_TSS_AARG1] : (hasTexture ? legacy.AlphaArg1 : (CKDWORD)CKRST_TA_DIFFUSE);
        const CKDWORD alphaArg2 = (st[CKRST_TSS_AARG2] || STAGE_SET(CKRST_TSS_AARG2)) ? st[CKRST_TSS_AARG2] : legacy.AlphaArg2;
        const CKDWORD alphaArg0 = (st[CKRST_TSS_ALPHAARG0] || STAGE_SET(CKRST_TSS_ALPHAARG0)) ? st[CKRST_TSS_ALPHAARG0] : legacy.AlphaArg0;
        const CKDWORD resultArg = (st[CKRST_TSS_RESULTARG0] || STAGE_SET(CKRST_TSS_RESULTARG0)) ? st[CKRST_TSS_RESULTARG0] : legacy.ResultArg;
        #undef STAGE_SET
        if (alphaOp == CKRST_TOP_DISABLE)
        {
            // A disabled alpha stage passes the current alpha through.
            alphaOp = CKRST_TOP_SELECTARG1;
            alphaArg1 = CKRST_TA_CURRENT;
        }

        const bool first = !anyStage;
        anyStage = true;

        // Texture coordinates for this stage.
        u8 texCoord = GX_TEXCOORDNULL;
        u8 texMap = GX_TEXMAP_NULL;
        if (hasTexture && texGens < 8 && BindTexture(s, GX_TEXMAP0 + s))
        {
            texMap = (u8)(GX_TEXMAP0 + s);
            texCoord = (u8)(GX_TEXCOORD0 + texGens);
            const CKDWORD index = CKRSTTexcoordIndex(st[CKRST_TSS_TEXCOORDINDEX] ? st[CKRST_TSS_TEXCOORDINDEX] : (CKDWORD)s);
            const CKDWORD generation = CKRSTTexcoordGeneration(st[CKRST_TSS_TEXCOORDINDEX]);
            const CKDWORD transformFlags = st[CKRST_TSS_TEXTURETRANSFORMFLAGS];
            const u32 matrixId = GX_TEXMTX0 + 3 * texGens;

            if (generation == CKRST_TEXGEN_SPHEREMAP || generation == CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR ||
                generation == CKRST_TEXGEN_CAMERASPACENORMAL)
            {
                // Sphere mapping from the eye-space normal: uv = n.xy * (0.5, -0.5) + 0.5.
                Mtx modelView, normal;
                ToGXModelView(m_Matrices[CKRSTMatrixSlot(VXMATRIX_WORLD)], m_Matrices[CKRSTMatrixSlot(VXMATRIX_VIEW)], modelView);
                ToGXNormalMatrix(modelView, normal);
                Mtx sphere;
                for (int c = 0; c < 3; ++c)
                {
                    sphere[0][c] = normal[0][c] * 0.5f;
                    sphere[1][c] = -normal[1][c] * 0.5f;
                    sphere[2][c] = 0.0f;
                }
                sphere[0][3] = 0.5f;
                sphere[1][3] = 0.5f;
                sphere[2][3] = 1.0f;
                GX_LoadTexMtxImm(sphere, matrixId, GX_MTX2x4);
                GX_SetTexCoordGen(texCoord, GX_TG_MTX2x4, GX_TG_NRM, matrixId);
                if (generation != CKRST_TEXGEN_SPHEREMAP)
                    Diag(CKRST_DIAG_APPROX_TEXTURE_OP);
            }
            else
            {
                const int set = ((int)index < source.TexcoordCount && source.Texcoord[index]) ? (int)index
                                : (source.TexcoordCount > 0 && source.Texcoord[0] ? 0 : -1);
                if (set < 0)
                {
                    texCoord = GX_TEXCOORDNULL;
                    texMap = GX_TEXMAP_NULL;
                }
                else if (transformFlags & 0xFF)
                {
                    // Direct3D: (u, v, 1) * M; GX: M * (s, t, 1).
                    const VxMatrix &m = m_Matrices[CKRSTMatrixSlot(VXMATRIX_TEXTURE(s))];
                    Mtx texture = {
                        {m[0][0], m[1][0], m[2][0], 0.0f},
                        {m[0][1], m[1][1], m[2][1], 0.0f},
                        {0.0f, 0.0f, 1.0f, 0.0f},
                    };
                    GX_LoadTexMtxImm(texture, matrixId, GX_MTX2x4);
                    GX_SetTexCoordGen(texCoord, GX_TG_MTX2x4, GX_TG_TEX0 + set, matrixId);
                }
                else
                {
                    GX_SetTexCoordGen(texCoord, GX_TG_MTX2x4, GX_TG_TEX0 + set, GX_IDENTITY);
                }
                if (generation != CKRST_TEXGEN_PASSTHRU)
                    Diag(CKRST_DIAG_APPROX_TEXTURE_OP);
            }
            if (texCoord != GX_TEXCOORDNULL)
                ++texGens;
        }
        const bool sampled = texMap != GX_TEXMAP_NULL;

        // Input mapping.
        int colorKonst = -1;
        int alphaKonst = -1;
        auto colorInput = [&](CKDWORD arg) -> u8 {
            const bool alpha = (arg & CKRST_TA_ALPHAREPLICATE) != 0;
            switch (arg & TA_BASE_MASK)
            {
            case CKRST_TA_DIFFUSE: return tev.ColorDiffuse(alpha);
            case CKRST_TA_CURRENT:
                if (first)
                    return tev.ColorDiffuse(alpha);
                return alpha ? GX_CC_APREV : GX_CC_CPREV;
            case CKRST_TA_TEXTURE:
                if (!sampled)
                    return GX_CC_ONE;
                return alpha ? GX_CC_TEXA : GX_CC_TEXC;
            case CKRST_TA_TFACTOR:
            case CKRST_TA_CONSTANT:
                colorKonst = alpha ? GX_TEV_KCSEL_K0_A : GX_TEV_KCSEL_K0;
                return GX_CC_KONST;
            case CKRST_TA_TEMP: return alpha ? GX_CC_A0 : GX_CC_C0;
            case CKRST_TA_SPECULAR:
            default: return GX_CC_ZERO;
            }
        };
        auto alphaInput = [&](CKDWORD arg) -> u8 {
            switch (arg & TA_BASE_MASK)
            {
            case CKRST_TA_DIFFUSE: return tev.AlphaDiffuse();
            case CKRST_TA_CURRENT: return first ? tev.AlphaDiffuse() : GX_CA_APREV;
            case CKRST_TA_TEXTURE:
                if (!sampled)
                {
                    alphaKonst = GX_TEV_KASEL_1;
                    return GX_CA_KONST;
                }
                return GX_CA_TEXA;
            case CKRST_TA_TFACTOR:
            case CKRST_TA_CONSTANT:
                alphaKonst = GX_TEV_KASEL_K0_A;
                return GX_CA_KONST;
            case CKRST_TA_TEMP: return GX_CA_A0;
            case CKRST_TA_SPECULAR:
            default: return GX_CA_ZERO;
            }
        };

        // Complemented arguments (1 - x) are computed by a stage of their own.
        auto complementColor = [&](CKDWORD arg) -> u8 {
            const u8 input = colorInput(arg & ~(CKDWORD)CKRST_TA_COMPLEMENT);
            const u8 stage = (u8)tev.Stage++;
            GX_SetTevOrder(stage, texCoord, texMap, GX_COLOR0A0);
            if (colorKonst >= 0)
                GX_SetTevKColorSel(stage, (u8)colorKonst);
            GX_SetTevColorIn(stage, GX_CC_ONE, GX_CC_ZERO, input, GX_CC_ZERO);
            GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG1);
            GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
            GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG1);
            colorKonst = -1;
            return (arg & CKRST_TA_ALPHAREPLICATE) ? GX_CC_A1 : GX_CC_C1;
        };
        auto complementAlpha = [&](CKDWORD arg) -> u8 {
            const u8 input = alphaInput(arg & ~(CKDWORD)CKRST_TA_COMPLEMENT);
            const u8 stage = (u8)tev.Stage++;
            GX_SetTevOrder(stage, texCoord, texMap, GX_COLOR0A0);
            if (alphaKonst >= 0 && alphaKonst != GX_TEV_KASEL_1)
                Diag(CKRST_DIAG_APPROX_TEXTURE_OP);
            GX_SetTevKAlphaSel(stage, GX_TEV_KASEL_1);
            GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
            GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG1);
            GX_SetTevAlphaIn(stage, GX_CA_KONST, GX_CA_ZERO, input, GX_CA_ZERO);
            GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG1);
            alphaKonst = -1;
            return GX_CA_A1;
        };
        auto colorArg = [&](CKDWORD arg) -> u8 {
            return (arg & CKRST_TA_COMPLEMENT) ? complementColor(arg) : colorInput(arg);
        };
        auto alphaArg = [&](CKDWORD arg) -> u8 {
            return (arg & CKRST_TA_COMPLEMENT) ? complementAlpha(arg) : alphaInput(arg);
        };

        const u8 c1 = colorArg(colorArg1);
        const u8 c2 = colorArg(colorArg2);
        const u8 c0 = colorArg(colorArg0);
        auto c1Alpha = [&]() -> u8 {
            return colorInput((colorArg1 & ~(CKDWORD)CKRST_TA_COMPLEMENT) | CKRST_TA_ALPHAREPLICATE);
        };
        const u8 a1 = alphaArg(alphaArg1);
        const u8 a2 = alphaArg(alphaArg2);
        const u8 a0 = alphaArg(alphaArg0);
        if (tev.Stage >= 16)
            break;

        const u8 stage = (u8)tev.Stage++;
        GX_SetTevOrder(stage, texCoord, texMap, GX_COLOR0A0);

        // Colour operation.
        TevInputs ci = {GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, c1};
        u8 colorTevOp = GX_TEV_ADD;
        u8 colorBias = GX_TB_ZERO;
        u8 colorScale = GX_CS_SCALE_1;
        const u8 diffuseAlpha = tev.ColorDiffuse(true);
        const u8 currentAlpha = first ? diffuseAlpha : GX_CC_APREV;
        switch (colorOp)
        {
        case CKRST_TOP_SELECTARG1: ci = TevInputs{GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, c1}; break;
        case CKRST_TOP_SELECTARG2: ci = TevInputs{GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, c2}; break;
        case CKRST_TOP_MODULATE4X: colorScale = GX_CS_SCALE_4; ci = TevInputs{GX_CC_ZERO, c1, c2, GX_CC_ZERO}; break;
        case CKRST_TOP_MODULATE2X: colorScale = GX_CS_SCALE_2; ci = TevInputs{GX_CC_ZERO, c1, c2, GX_CC_ZERO}; break;
        case CKRST_TOP_ADD: ci = TevInputs{c2, GX_CC_ZERO, GX_CC_ZERO, c1}; break;
        case CKRST_TOP_ADDSIGNED: colorBias = GX_TB_SUBHALF; ci = TevInputs{c2, GX_CC_ZERO, GX_CC_ZERO, c1}; break;
        case CKRST_TOP_ADDSIGNED2X: colorBias = GX_TB_SUBHALF; colorScale = GX_CS_SCALE_2; ci = TevInputs{c2, GX_CC_ZERO, GX_CC_ZERO, c1}; break;
        case CKRST_TOP_SUBTRACT: colorTevOp = GX_TEV_SUB; ci = TevInputs{c2, GX_CC_ZERO, GX_CC_ZERO, c1}; break;
        case CKRST_TOP_ADDSMOOTH: ci = TevInputs{c2, GX_CC_ZERO, c1, c1}; break;
        case CKRST_TOP_BLENDDIFFUSEALPHA: ci = TevInputs{c2, c1, diffuseAlpha, GX_CC_ZERO}; break;
        case CKRST_TOP_BLENDTEXTUREALPHA: ci = TevInputs{c2, c1, sampled ? (u8)GX_CC_TEXA : (u8)GX_CC_ONE, GX_CC_ZERO}; break;
        case CKRST_TOP_BLENDFACTORALPHA:
            colorKonst = GX_TEV_KCSEL_K0_A;
            ci = TevInputs{c2, c1, GX_CC_KONST, GX_CC_ZERO};
            break;
        case CKRST_TOP_BLENDTEXTUREALPHAPM: ci = TevInputs{c2, GX_CC_ZERO, sampled ? (u8)GX_CC_TEXA : (u8)GX_CC_ONE, c1}; break;
        case CKRST_TOP_BLENDCURRENTALPHA: ci = TevInputs{c2, c1, currentAlpha, GX_CC_ZERO}; break;
        case CKRST_TOP_MODULATEALPHA_ADDCOLOR: ci = TevInputs{GX_CC_ZERO, c2, c1Alpha(), c1}; break;
        case CKRST_TOP_MODULATECOLOR_ADDALPHA: ci = TevInputs{GX_CC_ZERO, c1, c2, c1Alpha()}; break;
        case CKRST_TOP_MODULATEINVALPHA_ADDCOLOR: ci = TevInputs{c2, GX_CC_ZERO, c1Alpha(), c1}; break;
        case CKRST_TOP_MODULATEINVCOLOR_ADDALPHA: ci = TevInputs{c2, GX_CC_ZERO, c1, c1Alpha()}; break;
        case CKRST_TOP_MULTIPLYADD: ci = TevInputs{GX_CC_ZERO, c1, c2, c0}; break;
        case CKRST_TOP_LERP: ci = TevInputs{c2, c1, c0, GX_CC_ZERO}; break;
        case CKRST_TOP_MODULATE:
            ci = TevInputs{GX_CC_ZERO, c1, c2, GX_CC_ZERO};
            break;
        default:
            // DOTPRODUCT3, bump mapping and premodulation have no TEV equivalent here.
            Diag(CKRST_DIAG_APPROX_TEXTURE_OP);
            ci = TevInputs{GX_CC_ZERO, c1, c2, GX_CC_ZERO};
            break;
        }

        // Alpha operation.
        TevInputs ai = {GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, a1};
        u8 alphaTevOp = GX_TEV_ADD;
        u8 alphaBias = GX_TB_ZERO;
        u8 alphaScale = GX_CS_SCALE_1;
        const u8 diffuseAlphaA = tev.AlphaDiffuse();
        const u8 currentAlphaA = first ? diffuseAlphaA : GX_CA_APREV;
        switch (alphaOp)
        {
        case CKRST_TOP_SELECTARG1: ai = TevInputs{GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, a1}; break;
        case CKRST_TOP_SELECTARG2: ai = TevInputs{GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, a2}; break;
        case CKRST_TOP_MODULATE4X: alphaScale = GX_CS_SCALE_4; ai = TevInputs{GX_CA_ZERO, a1, a2, GX_CA_ZERO}; break;
        case CKRST_TOP_MODULATE2X: alphaScale = GX_CS_SCALE_2; ai = TevInputs{GX_CA_ZERO, a1, a2, GX_CA_ZERO}; break;
        case CKRST_TOP_ADD: ai = TevInputs{a2, GX_CA_ZERO, GX_CA_ZERO, a1}; break;
        case CKRST_TOP_ADDSIGNED: alphaBias = GX_TB_SUBHALF; ai = TevInputs{a2, GX_CA_ZERO, GX_CA_ZERO, a1}; break;
        case CKRST_TOP_ADDSIGNED2X: alphaBias = GX_TB_SUBHALF; alphaScale = GX_CS_SCALE_2; ai = TevInputs{a2, GX_CA_ZERO, GX_CA_ZERO, a1}; break;
        case CKRST_TOP_SUBTRACT: alphaTevOp = GX_TEV_SUB; ai = TevInputs{a2, GX_CA_ZERO, GX_CA_ZERO, a1}; break;
        case CKRST_TOP_ADDSMOOTH: ai = TevInputs{a2, GX_CA_ZERO, a1, a1}; break;
        case CKRST_TOP_BLENDDIFFUSEALPHA: ai = TevInputs{a2, a1, diffuseAlphaA, GX_CA_ZERO}; break;
        case CKRST_TOP_BLENDTEXTUREALPHA: ai = TevInputs{a2, a1, sampled ? (u8)GX_CA_TEXA : (u8)GX_CA_ZERO, GX_CA_ZERO}; break;
        case CKRST_TOP_BLENDCURRENTALPHA: ai = TevInputs{a2, a1, currentAlphaA, GX_CA_ZERO}; break;
        case CKRST_TOP_MULTIPLYADD: ai = TevInputs{GX_CA_ZERO, a1, a2, a0}; break;
        case CKRST_TOP_LERP: ai = TevInputs{a2, a1, a0, GX_CA_ZERO}; break;
        case CKRST_TOP_MODULATE:
        default:
            ai = TevInputs{GX_CA_ZERO, a1, a2, GX_CA_ZERO};
            break;
        }

        if (colorKonst >= 0)
            GX_SetTevKColorSel(stage, (u8)colorKonst);
        if (alphaKonst >= 0)
            GX_SetTevKAlphaSel(stage, (u8)alphaKonst);

        const u8 destination = ((resultArg & TA_BASE_MASK) == CKRST_TA_TEMP) ? GX_TEVREG0 : GX_TEVPREV;
        GX_SetTevColorIn(stage, ci.a, ci.b, ci.c, ci.d);
        GX_SetTevColorOp(stage, colorTevOp, colorBias, colorScale, GX_TRUE, destination);
        GX_SetTevAlphaIn(stage, ai.a, ai.b, ai.c, ai.d);
        GX_SetTevAlphaOp(stage, alphaTevOp, alphaBias, alphaScale, GX_TRUE, destination);
    }

    if (!anyStage)
    {
        // No texture: output the (lit) diffuse colour.
        const u8 stage = (u8)tev.Stage++;
        GX_SetTevOrder(stage, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, tev.ColorDiffuse(false));
        GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, tev.AlphaDiffuse());
        GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    }

    (void)pretransformed;
    GX_SetNumTexGens(texGens);
    GX_SetNumTevStages(tev.Stage);

    if (m_InvalidateTextures)
    {
        GX_InvalidateTexAll();
        m_InvalidateTextures = FALSE;
    }
    return texGens;
}

// ---------------------------------------------------------------------------
// Pixel pipeline: depth, alpha test, blending, culling, fog.

void CKGXRasterizerContext::ApplyPixelState(CKBOOL pretransformed)
{
    const CKDWORD *rs = m_RenderStates;

    GX_SetZMode(rs[VXRENDERSTATE_ZENABLE] ? GX_TRUE : GX_FALSE, CompareFunction(rs[VXRENDERSTATE_ZFUNC]),
                rs[VXRENDERSTATE_ZWRITEENABLE] ? GX_TRUE : GX_FALSE);

    const CKBOOL alphaTest = rs[VXRENDERSTATE_ALPHATESTENABLE] != 0;
    if (alphaTest)
        GX_SetAlphaCompare(CompareFunction(rs[VXRENDERSTATE_ALPHAFUNC]), (u8)(rs[VXRENDERSTATE_ALPHAREF] & 0xFF),
                           GX_AOP_AND, GX_ALWAYS, 0);
    else
        GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    // Depth must be tested after the TEV when alpha testing, or rejected
    // pixels would still write depth.
    GX_SetZCompLoc(alphaTest ? GX_FALSE : GX_TRUE);

    if (rs[VXRENDERSTATE_ALPHABLENDENABLE])
        GX_SetBlendMode(GX_BM_BLEND, SourceBlend(rs[VXRENDERSTATE_SRCBLEND]),
                        DestinationBlend(rs[VXRENDERSTATE_DESTBLEND], rs[VXRENDERSTATE_SRCBLEND]), GX_LO_CLEAR);
    else
        GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);

    const CKDWORD colorWrite = rs[VXRENDERSTATE_COLORWRITEENABLE];
    GX_SetColorUpdate((colorWrite & (CKRST_COLORWRITE_RED | CKRST_COLORWRITE_GREEN | CKRST_COLORWRITE_BLUE)) ? GX_TRUE : GX_FALSE);
    GX_SetAlphaUpdate((colorWrite & CKRST_COLORWRITE_ALPHA) ? GX_TRUE : GX_FALSE);

    // Both APIs see clockwise triangles as front facing.
    CKDWORD cull = rs[VXRENDERSTATE_CULLMODE];
    if (rs[VXRENDERSTATE_INVERSEWINDING])
        cull = (cull == VXCULL_CW) ? VXCULL_CCW : (cull == VXCULL_CCW ? VXCULL_CW : cull);
    if (cull == VXCULL_CCW)
        GX_SetCullMode(GX_CULL_BACK);
    else if (cull == VXCULL_CW)
        GX_SetCullMode(GX_CULL_FRONT);
    else
        GX_SetCullMode(GX_CULL_NONE);

    GX_SetDither(rs[VXRENDERSTATE_DITHERENABLE] ? GX_ENABLE : GX_DISABLE);

    const float pointSize = AsFloat(rs[VXRENDERSTATE_POINTSIZE]);
    GX_SetPointSize((u8)(pointSize > 0.0f && pointSize < 42.0f ? pointSize * 6.0f : 6.0f), GX_TO_ZERO);
    GX_SetLineWidth(6, GX_TO_ZERO);

    // Fog, in eye-space distance.
    const CKDWORD fogMode = rs[VXRENDERSTATE_FOGPIXELMODE] ? rs[VXRENDERSTATE_FOGPIXELMODE] : rs[VXRENDERSTATE_FOGVERTEXMODE];
    const VxMatrix &p = m_Matrices[CKRSTMatrixSlot(VXMATRIX_PROJECTION)];
    if (!pretransformed && rs[VXRENDERSTATE_FOGENABLE] && fogMode != VXFOG_NONE && p[3][3] == 0.0f && p[2][2] != 0.0f &&
        p[2][2] != 1.0f)
    {
        const float zNear = -p[3][2] / p[2][2];
        const float zFar = p[2][2] * zNear / (p[2][2] - 1.0f);
        float start = AsFloat(rs[VXRENDERSTATE_FOGSTART]);
        float end = AsFloat(rs[VXRENDERSTATE_FOGEND]);
        u8 type = GX_FOG_PERSP_LIN;
        if (fogMode == VXFOG_EXP || fogMode == VXFOG_EXP2)
        {
            // GX exponential fog spans start..end; reach ~95% where Direct3D would.
            const float density = AsFloat(rs[VXRENDERSTATE_FOGDENSITY]);
            start = 0.0f;
            end = density > 0.0f ? (fogMode == VXFOG_EXP ? 3.0f : 1.73f) / density : zFar;
            type = fogMode == VXFOG_EXP ? GX_FOG_PERSP_EXP : GX_FOG_PERSP_EXP2;
        }
        GX_SetFog(type, start, end, zNear, zFar, ToGXColor(rs[VXRENDERSTATE_FOGCOLOR]));
    }
    else
    {
        GXColor none = {0, 0, 0, 0};
        GX_SetFog(GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, none);
    }

    if (rs[VXRENDERSTATE_STENCILENABLE])
        Diag(CKRST_DIAG_APPROX_STENCIL_WRITE_MASK);
}

void CKGXRasterizerContext::ApplyViewport(CKBOOL pretransformed)
{
    const int targetWidth = m_Target ? m_TargetWidth : m_Width;
    const int targetHeight = m_Target ? m_TargetHeight : m_Height;

    int x = (int)m_Viewport.ViewX;
    int y = (int)m_Viewport.ViewY;
    int w = (int)m_Viewport.ViewWidth;
    int h = (int)m_Viewport.ViewHeight;
    if (w <= 0 || h <= 0)
    {
        x = y = 0;
        w = targetWidth;
        h = targetHeight;
    }
    if (x + w > targetWidth)
        w = targetWidth - x;
    if (y + h > targetHeight)
        h = targetHeight - y;

    if (pretransformed)
        GX_SetViewport(0.0f, 0.0f, (f32)targetWidth, (f32)targetHeight, 0.0f, 1.0f);
    else
        GX_SetViewport((f32)x, (f32)y, (f32)w, (f32)h, m_Viewport.ViewZMin, m_Viewport.ViewZMax);
    GX_SetScissor(x, y, w > 0 ? w : 0, h > 0 ? h : 0);
}
