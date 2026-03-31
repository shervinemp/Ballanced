#include "CKGXRasterizer.h"

#ifdef WII
#include <gccore.h>
#endif

CKGXRasterizerContext::CKGXRasterizerContext() {}
CKGXRasterizerContext::~CKGXRasterizerContext() {}

CKBOOL CKGXRasterizerContext::SetTexture(CKDWORD Texture, CKDWORD Stage) {
#ifdef WII
    CKTextureDesc* desc = m_Owner->GetTextureDesc(Texture);
    if (desc) {
        // We assume GXTexObj is attached to or derived from desc->Flags
        GXTexObj* texObj = (GXTexObj*)desc->Flags;
        if (texObj) {
            DCFlushRange(texObj, sizeof(GXTexObj));
            GX_LoadTexObj(texObj, GX_TEXMAP0);
        }
    }
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
