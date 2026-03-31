#include "CKGXRasterizer.h"

#ifdef WII
#include <gccore.h>
#include <ogc/gu.h>
#endif

CKGXRasterizerContext::CKGXRasterizerContext() {}
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

    // Bind texture to hardware map 0
    GX_LoadTexObj(texObj, GX_TEXMAP0);

    // Tell the GPU we are using 1 texture generation and 1 TEV stage
    GX_SetNumTexGens(1);
    GX_SetNumTevStages(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    // Set the TEV operation to Modulate (Texture Color * Vertex Color)
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
#endif
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) {
#ifdef WII
    // Handle various render states mapping to GX functions here.
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
