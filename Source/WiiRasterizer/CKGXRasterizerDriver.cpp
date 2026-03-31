#include "CKGXRasterizer.h"

class CKGXRasterizerDriver : public CKRasterizerDriver {
public:
    CKGXRasterizerDriver() {}
    virtual ~CKGXRasterizerDriver() {}

    virtual CKRasterizerContext* CreateContext(CKRasterizer *rst) {
        CKGXRasterizerContext* context = new CKGXRasterizerContext();
        return context;
    }
};

CKRasterizerDriver *CKGXRasterizer::CreateDriver() {
    // For GX, we only have one hardware driver mapping to the Wii GPU
    CKGXRasterizerDriver* driver = new CKGXRasterizerDriver();
    driver->m_Desc.DriverIndex = 0;
    driver->m_Desc.Caps.MaxTextureWidth = 1024;
    driver->m_Desc.Caps.MaxTextureHeight = 1024;
    return driver;
}
