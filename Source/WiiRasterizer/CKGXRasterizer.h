#ifndef CKGXRASTERIZER_H
#define CKGXRASTERIZER_H

#include "CKRasterizer.h"

class CKGXRasterizer : public CKRasterizer {
public:
    CKGXRasterizer();
    virtual ~CKGXRasterizer();

    virtual CKBOOL Init();
    virtual void Close();

    virtual CKRasterizerDriver *CreateDriver();
    virtual CKBOOL DestroyDriver(CKRasterizerDriver *Driver);

    virtual CKBOOL CheckCapabilities(int renderId, VxDriverDesc *desc);

    void InitGPU();
};

class CKGXRasterizerContext : public CKRasterizerContext {
public:
    CKGXRasterizerContext();
    virtual ~CKGXRasterizerContext();

    virtual CKBOOL SetTexture(CKDWORD Texture, CKDWORD Stage = 0);
    virtual CKBOOL SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value);
    virtual CKBOOL DrawPrimitive(VXPRIMITIVETYPE pType, CKWORD *indices, int indexcount, VxDrawPrimitiveData *data);

protected:
#ifdef WII
    u8 m_ZEnable;
    u8 m_ZWriteEnable;
    u8 m_ZFunc;

    u8 m_AlphaBlendEnable;
    u8 m_SrcBlend;
    u8 m_DestBlend;

    u8 m_CullMode;
    u8 m_AlphaFunc;
    u8 m_AlphaRef;
#endif

public:
    // Matrix methods
    void SetProjectionMatrix(const VxMatrix& projMat);
    void SetWorldViewMatrix(const VxMatrix& worldMat, const VxMatrix& viewMat);
    void Present();
};

#endif // CKGXRASTERIZER_H
