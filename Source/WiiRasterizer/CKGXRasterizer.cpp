#include "CKGXRasterizer.h"

#include "CKRasterizerDriverCaps.h"

#include <new>

// ---------------------------------------------------------------------------
// Driver: the one GX device

CKGXRasterizerDriver::CKGXRasterizerDriver(CKRasterizer *owner)
    : CKRasterizerDriver(owner, 0, (CKSTRING)"Nintendo Wii GX", TRUE)
{
    CKRSTInitializeDriverCaps(m_DisplayModes, m_TextureFormats, m_NativeCaps);
    m_CapsFinal = TRUE;
}

CKGXRasterizerDriver::~CKGXRasterizerDriver()
{
    DestroyContexts();
}

CKRasterizerContext *CKGXRasterizerDriver::CreateContext()
{
    // GX drives a single picture.
    if (m_Contexts.Size() != 0)
        return NULL;
    CKGXRasterizerContext *context = new (std::nothrow) CKGXRasterizerContext(this);
    if (context)
        AddContext(context);
    return context;
}

// ---------------------------------------------------------------------------
// Rasterizer

CKBOOL CKGXRasterizer::Start(WIN_HANDLE appWindow)
{
    if (GetDriverCount() != 0)
        return TRUE;
    CKRasterizer::Start(appWindow);
    CKGXRasterizerDriver *driver = new (std::nothrow) CKGXRasterizerDriver(this);
    if (!driver)
        return FALSE;
    AddDriver(driver);
    return TRUE;
}

static CKRasterizer *CKGXRasterizerStart(WIN_HANDLE appWindow)
{
    CKGXRasterizer *rasterizer = new (std::nothrow) CKGXRasterizer();
    if (rasterizer && !rasterizer->Start(appWindow))
    {
        delete rasterizer;
        rasterizer = NULL;
    }
    return rasterizer;
}

static void CKGXRasterizerClose(CKRasterizer *rasterizer)
{
    delete rasterizer;
}

// Registered statically by CK2_3D (CKRE_STATIC_GX_RASTERIZER).
void CKGXRasterizerGetInfo(CKRasterizerInfo *info)
{
    if (!info)
        return;
    info->DllName = "CKGXRasterizer";
    info->Desc = "Nintendo Wii GX Rasterizer";
    info->DllInstance = NULL;
    info->StartFct = CKGXRasterizerStart;
    info->CloseFct = CKGXRasterizerClose;
    info->InterfaceRevision = CKRST_INTERFACE_REVISION;
}
