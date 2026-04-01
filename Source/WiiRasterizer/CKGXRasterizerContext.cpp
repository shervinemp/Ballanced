#include "CKGXRasterizer.h"

#ifdef WII
#include <gccore.h>
#include <ogc/gu.h>
#endif

CKGXRasterizerContext::CKGXRasterizerContext() {
#ifdef WII
    m_ZEnable = GX_TRUE;
    m_ZWriteEnable = GX_TRUE;
    m_ZFunc = GX_LEQUAL;
    m_AlphaBlendEnable = GX_FALSE;
    m_SrcBlend = GX_BL_SRCALPHA;
    m_DestBlend = GX_BL_INVSRCALPHA;
    m_CullMode = GX_CULL_NONE;
#endif
}
CKGXRasterizerContext::~CKGXRasterizerContext() {}

#ifdef WII
// --- Helper Functions to convert Virtools matrices to Wii hardware matrices ---
static void ConvertProjectionMatrix(const VxMatrix& vxMat, Mtx44& gxMat) {
    // Virtools is row-major, GX Mtx44 is a 4x4 float array.
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            gxMat[r][c] = vxMat[r][c];
        }
    }
}

static void ConvertModelViewMatrix(const VxMatrix& vxMat, Mtx& gxMat) {
    // Virtools is 4x4 row-major. GX Mtx is 3x4 (drops the bottom row [0, 0, 0, 1]).
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            gxMat[r][c] = vxMat[r][c];
        }
    }
}

// --- Implementation in the Rasterizer Context ---
void CKGXRasterizerContext::SetProjectionMatrix(const VxMatrix& projMat) {
    Mtx44 gxProj;
    ConvertProjectionMatrix(projMat, gxProj);
    // Load it into the GPU (assuming perspective projection)
    GX_LoadProjectionMtx(gxProj, GX_PERSPECTIVE);
}

void CKGXRasterizerContext::SetWorldViewMatrix(const VxMatrix& worldMat, const VxMatrix& viewMat) {
    VxMatrix worldView = worldMat * viewMat;
    Mtx gxModelView;
    ConvertModelViewMatrix(worldView, gxModelView);
    // Load it into Position Matrix register 0
    GX_LoadPosMtxImm(gxModelView, GX_PNMTX0);
}
#endif

void CKGXRasterizerContext::Present() {
#ifdef WII
    extern void* frameBuffer[2];
    extern int currentFb;

    // 1. Tell the GPU to finish all drawing commands in the FIFO
    GX_DrawDone();

    // 2. Toggle the active framebuffer (Double Buffering)
    currentFb ^= 1;

    // 3. Copy the GPU internal render to our display buffer
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(frameBuffer[currentFb], GX_TRUE);

    // 4. Send the new buffer to the screen
    GX_Flush();
    VIDEO_SetNextFramebuffer(frameBuffer[currentFb]);
    VIDEO_Flush();

    // 5. CRITICAL: Lock the engine loop to the console refresh rate (60Hz)
    VIDEO_WaitVSync();
#endif
}

CKBOOL CKGXRasterizerContext::SetTexture(CKDWORD Texture, CKDWORD Stage) {
#ifdef WII
    CKTextureDesc* desc = m_Owner->GetTextureDesc(Texture);
    if (!desc || !desc->Flags) {
        // Disable texturing for this stage (draw flat color)
        GX_SetNumTexGens(0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR); // Pass vertex color directly
        return TRUE;
    }

    // Assuming texture data is stored inside a custom CKGXTexture subclass
    // We assume GXTexObj is attached to or derived from desc->Flags
    GXTexObj* texObj = (GXTexObj*)desc->Flags;

    // CRITICAL: Texture data must be flushed from CPU cache to MEM1/MEM2
    // Get the actual texture data pointer and size from the GXTexObj
    void* texData = GX_GetTexObjData(texObj);
    u32 texWidth = GX_GetTexObjWidth(texObj);
    u32 texHeight = GX_GetTexObjHeight(texObj);
    DCFlushRange(texData, texWidth * texHeight * 4); // Assuming 32-bit RGBA8 for the flush size

    // Bind texture to the specific hardware map based on Stage
    u32 texMap = GX_TEXMAP0 + Stage;
    GX_LoadTexObj(texObj, texMap);

    // Tell the GPU we are using N texture generations and N TEV stages
    // This is simplistic and assumes stages are set sequentially
    GX_SetNumTexGens(Stage + 1);
    GX_SetNumTevStages(Stage + 1);

    u32 texCoord = GX_TEXCOORD0 + Stage;
    u32 tevStage = GX_TEVSTAGE0 + Stage;

    // Coordinate generation
    GX_SetTexCoordGen(texCoord, GX_TG_MTX2x4, GX_TG_TEX0 + Stage, GX_IDENTITY);

    // TEV Stage Configuration
    // If it's the first stage, use rasterized color (vertex color).
    // For multi-texturing (Stage > 0), blend with the previous TEV output.
    u32 colorIn = (Stage == 0) ? GX_COLOR0A0 : GX_COLORPREV;
    GX_SetTevOrder(tevStage, texCoord, texMap, colorIn);
    GX_SetTevOp(tevStage, GX_MODULATE);
#endif
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) {
#ifdef WII
    switch (State) {
        case VXRENDERSTATE_ZENABLE:
            m_ZEnable = Value ? GX_TRUE : GX_FALSE;
            GX_SetZMode(m_ZEnable, m_ZFunc, m_ZWriteEnable);
            break;
        case VXRENDERSTATE_ZWRITEENABLE:
            m_ZWriteEnable = Value ? GX_TRUE : GX_FALSE;
            GX_SetZMode(m_ZEnable, m_ZFunc, m_ZWriteEnable);
            break;
        case VXRENDERSTATE_ZFUNC: {
            static const u8 cmpFuncs[] = {
                GX_NEVER, GX_NEVER, GX_LESS, GX_EQUAL,
                GX_LEQUAL, GX_GREATER, GX_NEQUALS, GX_GEQUAL, GX_ALWAYS
            };
            if (Value <= 8) m_ZFunc = cmpFuncs[Value];
            GX_SetZMode(m_ZEnable, m_ZFunc, m_ZWriteEnable);
            break;
        }
        case VXRENDERSTATE_ALPHABLENDENABLE:
            m_AlphaBlendEnable = Value ? GX_TRUE : GX_FALSE;
            GX_SetBlendMode(m_AlphaBlendEnable ? GX_BM_BLEND : GX_BM_NONE, m_SrcBlend, m_DestBlend, GX_LO_CLEAR);
            break;
        case VXRENDERSTATE_SRCBLEND: {
            static const u8 blendFactors[] = {
                GX_BL_ZERO, GX_BL_ZERO, GX_BL_ONE, GX_BL_SRCCLR, GX_BL_INVSRCCLR,
                GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_BL_DSTALPHA, GX_BL_INVDSTALPHA,
                GX_BL_DSTCLR, GX_BL_INVDSTCLR, GX_BL_SRCALPHA, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA
            };
            if (Value <= 13) m_SrcBlend = blendFactors[Value];
            GX_SetBlendMode(m_AlphaBlendEnable ? GX_BM_BLEND : GX_BM_NONE, m_SrcBlend, m_DestBlend, GX_LO_CLEAR);
            break;
        }
        case VXRENDERSTATE_DESTBLEND: {
            static const u8 blendFactors[] = {
                GX_BL_ZERO, GX_BL_ZERO, GX_BL_ONE, GX_BL_SRCCLR, GX_BL_INVSRCCLR,
                GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_BL_DSTALPHA, GX_BL_INVDSTALPHA,
                GX_BL_DSTCLR, GX_BL_INVDSTCLR, GX_BL_SRCALPHA, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA
            };
            if (Value <= 13) m_DestBlend = blendFactors[Value];
            GX_SetBlendMode(m_AlphaBlendEnable ? GX_BM_BLEND : GX_BM_NONE, m_SrcBlend, m_DestBlend, GX_LO_CLEAR);
            break;
        }
        case VXRENDERSTATE_CULLMODE: {
            static const u8 cullModes[] = {
                GX_CULL_NONE, GX_CULL_NONE, GX_CULL_BACK, GX_CULL_FRONT
            };
            if (Value <= 3) m_CullMode = cullModes[Value];
            GX_SetCullMode(m_CullMode);
            break;
        }
        default:
            break;
    }
#endif
    return TRUE;
}

CKBOOL CKGXRasterizerContext::DrawPrimitive(VXPRIMITIVETYPE pType, CKWORD *indices, int indexcount, VxDrawPrimitiveData *data) {
#ifdef WII
    if (!data || !data->PositionPtr || indexcount <= 0) return FALSE;

    // We assume triangles for now based on the requested mapping
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, indexcount);

    CKBYTE* posPtr = (CKBYTE*)data->PositionPtr;
    CKBYTE* normPtr = (CKBYTE*)data->NormalPtr;
    CKBYTE* uvPtr = (CKBYTE*)data->TexCoordPtr;

    for (int i = 0; i < indexcount; i++) {
        int idx = indices ? indices[i] : i;

        VxVector* pos = (VxVector*)(posPtr + idx * data->PositionStride);
        GX_Position3f32(pos->x, pos->y, pos->z);

        if (normPtr) {
            VxVector* norm = (VxVector*)(normPtr + idx * data->NormalStride);
            GX_Normal3f32(norm->x, norm->y, norm->z);
        } else {
            GX_Normal3f32(0.0f, 1.0f, 0.0f);
        }

        if (uvPtr) {
            float* uv = (float*)(uvPtr + idx * data->TexCoordStride);
            GX_TexCoord2f32(uv[0], uv[1]);
        } else {
            GX_TexCoord2f32(0.0f, 0.0f);
        }
    }
    GX_End();
#endif
    return TRUE;
}
