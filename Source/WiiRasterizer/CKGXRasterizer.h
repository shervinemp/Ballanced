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
};

#endif // CKGXRASTERIZER_H
