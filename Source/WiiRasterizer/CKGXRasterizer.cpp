#include "CKGXRasterizer.h"

#ifdef WII
#include <gccore.h>
#include <malloc.h>
#include <string.h>
#endif

// The Wii GPU requires a dedicated block of memory (FIFO) to receive draw commands
#define DEFAULT_FIFO_SIZE (256 * 1024)
static void* gp_Fifo = NULL;

// Video state globals
void* frameBuffer[2] = {NULL, NULL};
int currentFb = 0;
GXRModeObj* rmode = NULL;

CKGXRasterizer::CKGXRasterizer() {}
CKGXRasterizer::~CKGXRasterizer() {}

CKBOOL CKGXRasterizer::Init() {
    InitGPU();
    return TRUE;
}
void CKGXRasterizer::Close() {}
CKBOOL CKGXRasterizer::DestroyDriver(CKRasterizerDriver *Driver) {
    if (Driver) delete Driver;
    return TRUE;
}
CKBOOL CKGXRasterizer::CheckCapabilities(int renderId, VxDriverDesc *desc) { return TRUE; }

void CKGXRasterizer::InitGPU() {
#ifdef WII
    // 1. Allocate 32-byte aligned memory for the GPU Command Buffer
    gp_Fifo = memalign(32, DEFAULT_FIFO_SIZE);
    memset(gp_Fifo, 0, DEFAULT_FIFO_SIZE);

    // 2. Initialize GX
    GX_Init(gp_Fifo, DEFAULT_FIFO_SIZE);

    // 3. Clear the background color to black
    GXColor background = {0, 0, 0, 255};
    GX_SetCopyClear(background, 0x00ffffff);

    // 4. Set the vertex layout for Virtools standard meshes
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    // Initialize Video System
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);

    // Allocate two framebuffers in MEM1 (32-byte aligned for DMA)
    frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(frameBuffer[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if(rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
#endif
}
