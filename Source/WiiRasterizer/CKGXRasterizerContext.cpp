#include "CKGXRasterizer.h"

#include "WiiSystem.h"

#include <ogc/lwp_watchdog.h>

#include <malloc.h>
#include <string.h>

namespace
{
    const u32 kFifoSize = 1024 * 1024;

    CKBOOL ValidPrimitive(VXPRIMITIVETYPE type, int count)
    {
        switch (type)
        {
        case VX_POINTLIST: return count >= 1;
        case VX_LINELIST:
        case VX_LINESTRIP: return count >= 2;
        case VX_TRIANGLELIST:
        case VX_TRIANGLESTRIP:
        case VX_TRIANGLEFAN: return count >= 3;
        default: return FALSE;
        }
    }
}

CKGXRasterizerContext::CKGXRasterizerContext(CKRasterizerDriver *driver)
    : CKRasterizerContext(driver),
      m_Mode(NULL),
      m_FrameBuffer(0),
      m_Fifo(NULL),
      m_Phase(PHASE_IDLE),
      m_FrameOpen(FALSE),
      m_InvalidateTextures(TRUE),
      m_Target(0),
      m_TargetWidth(0),
      m_TargetHeight(0),
      m_LightsDirty(TRUE),
      m_FrameDrawCalls(0),
      m_FramePrimitives(0),
      m_FrameClears(0),
      m_FrameTextureUploads(0),
      m_FrameBufferUploads(0)
{
    m_FrameBuffers[0] = m_FrameBuffers[1] = NULL;
    memset(m_StageStates, 0, sizeof(m_StageStates));
    memset(m_StageSetMasks, 0, sizeof(m_StageSetMasks));
    memset(m_Textures, 0, sizeof(m_Textures));
    memset(m_Lights, 0, sizeof(m_Lights));
    memset(m_LightEnabled, 0, sizeof(m_LightEnabled));
    memset(m_ClipPlanes, 0, sizeof(m_ClipPlanes));
    for (int i = 0; i < CKRST_MATRIX_SLOT_COUNT; ++i)
        Vx3DMatrixIdentity(m_Matrices[i]);
    ResetMaterial();
    InitDefaultRenderStatesValue();
}

CKGXRasterizerContext::~CKGXRasterizerContext()
{
    BeginShutdown();
}

void CKGXRasterizerContext::Diag(CKRST_DIAGNOSTIC diagnostic)
{
    if ((CKDWORD)diagnostic < CKRST_DIAG_COUNT)
        ++m_Stats.Diagnostics[diagnostic];
}

void CKGXRasterizerContext::ResetMaterial()
{
    m_Material = CKMaterialData();
    m_Material.Diffuse = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
    m_Material.Ambient = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Video

void CKGXRasterizerContext::InitVideo()
{
    m_Mode = wiisystem::GetVideoMode();
    if (!m_Mode)
        m_Mode = VIDEO_GetPreferredMode(NULL);

    for (int i = 0; i < 2; ++i)
    {
        m_FrameBuffers[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(m_Mode));
        VIDEO_ClearFrameBuffer(m_Mode, m_FrameBuffers[i], COLOR_BLACK);
    }
    VIDEO_Configure(m_Mode);
    VIDEO_SetNextFramebuffer(m_FrameBuffers[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (m_Mode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    m_Fifo = memalign(32, kFifoSize);
    memset(m_Fifo, 0, kFifoSize);
    GX_Init(m_Fifo, kFifoSize);

    GXColor black = {0, 0, 0, 255};
    GX_SetCopyClear(black, GX_MAX_Z24);

    // The game renders 640x480; PAL 50 Hz modes stretch it while copying out.
    const u16 width = (u16)m_Width;
    const u16 height = (u16)m_Height;
    const f32 yScale = GX_GetYScaleFactor(height, m_Mode->xfbHeight);
    const u32 xfbHeight = GX_SetDispCopyYScale(yScale);
    GX_SetDispCopySrc(0, 0, width, height);
    GX_SetDispCopyDst(m_Mode->fbWidth, xfbHeight);
    GX_SetCopyFilter(m_Mode->aa, m_Mode->sample_pattern, GX_TRUE, m_Mode->vfilter);
    GX_SetFieldMode(m_Mode->field_rendering, (m_Mode->viHeight == 2 * m_Mode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(m_Mode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDither(GX_ENABLE);
    GX_CopyDisp(m_FrameBuffers[0], GX_TRUE);

    GX_InvVtxCache();
    GX_InvalidateTexAll();
}

void CKGXRasterizerContext::CopyToScreen()
{
    GX_SetScissor(0, 0, m_Width, m_Height);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    m_FrameBuffer ^= 1;
    // Clear() draws its own quad, so the copy keeps the EFB: readbacks after
    // BackToFront see the frame just presented.
    GX_CopyDisp(m_FrameBuffers[m_FrameBuffer], GX_FALSE);
    const u64 submitted = gettime();
    GX_DrawDone();
    const u64 drawn = gettime();
    VIDEO_SetNextFramebuffer(m_FrameBuffers[m_FrameBuffer]);
    VIDEO_Flush();
    // The TV refresh paces the game, like a console title.
    VIDEO_WaitVSync();
    wiisystem::AddFrameWaits(drawn - submitted, gettime() - drawn);
}

// ---------------------------------------------------------------------------
// Lifecycle

CKBOOL CKGXRasterizerContext::Create(WIN_HANDLE window, int posX, int posY, int width, int height,
                                     int bpp, CKBOOL fullscreen, int refreshRate, int zBpp, int stencilBpp)
{
    if (m_Created)
        return FALSE;
    // The embedded frame buffer holds at most 640x528; the game always gets 640x480.
    width = wiisystem::GetRenderWidth();
    height = wiisystem::GetRenderHeight();
    SetContextDesc(window, posX, posY, width, height, 32, fullscreen, refreshRate, 24, 0);
    (void)bpp;
    (void)zBpp;
    (void)stencilBpp;

    InitVideo();

    m_Viewport = CKViewportData();
    m_Viewport.ViewWidth = m_Width;
    m_Viewport.ViewHeight = m_Height;
    m_Created = TRUE;
    m_ShuttingDown = FALSE;
    memset(&m_Stats, 0, sizeof(m_Stats));
    m_Stats.Width = m_Width;
    m_Stats.Height = m_Height;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::Resize(int posX, int posY, int width, int height, CKDWORD flags)
{
    // The TV picture never changes size.
    (void)width;
    (void)height;
    (void)flags;
    m_PosX = posX;
    m_PosY = posY;
    return CanWork();
}

CKBOOL CKGXRasterizerContext::SetOptions(const CKRasterizerOptions *options)
{
    if (!options)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    SetContextOptions(*options);
    return TRUE;
}

CKBOOL CKGXRasterizerContext::GetCaps(CKRasterizerCapsDesc *caps) const
{
    if (!caps || !m_Created)
        return FALSE;
    CKRasterizerCapsDesc result;
    result.Features = CKRST_CAPS_SYNC_READBACK;
    result.MaxTextureSize = 1024;
    result.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
    result.MaxAnisotropy = 4;
    result.MaxUserClipPlanes = 0;
    result.MaxVertexBlendMatrices = 0;
    result.MaxMSAASamples = 1;
    result.MaxPointSize = 42.0f;
    result.MaxLights = CKRST_MAX_LIGHTS;
    *caps = result;
    return TRUE;
}

CKERROR CKGXRasterizerContext::GetDeviceStatus() const
{
    return CanWork() ? CK_OK : CKERR_INVALIDRENDERCONTEXT;
}

CKBOOL CKGXRasterizerContext::BeginShutdown()
{
    if (!m_Created || m_ShuttingDown)
        return TRUE;
    m_ShuttingDown = TRUE;

    XClassArray<CKGXReadback> pending;
    pending.Swap(m_Readbacks);
    for (CKGXReadback *it = pending.Begin(); it != pending.End(); ++it)
        it->Callback(it->User, it->HasRect ? &it->Rect : NULL, it->Buffer, NULL, FALSE);

    GX_DrawDone();
    for (int i = 0; i < m_Resources.Size(); ++i)
        ReleaseResource(m_Resources[i]);
    m_Resources.Clear();
    for (int i = 0; i < m_PendingFrees.Size(); ++i)
        ReleaseResource(m_PendingFrees[i]);
    m_PendingFrees.Clear();
    memset(m_Textures, 0, sizeof(m_Textures));
    m_Target = 0;
    m_Phase = PHASE_IDLE;
    m_FrameOpen = FALSE;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::IsIdle() const
{
    return m_Phase == PHASE_IDLE && !m_FrameOpen;
}

// ---------------------------------------------------------------------------
// Frame

CKBOOL CKGXRasterizerContext::Clear(CKDWORD flags, CKDWORD color, float z, CKDWORD stencil,
                                    int rectCount, CKRECT *rects)
{
    (void)stencil; // The EFB has no stencil buffer.
    if (!CanWork() || rectCount < 0 || (rectCount > 0 && !rects))
        return FALSE;
    const CKBOOL clearColor = (flags & CKRST_CTXCLEAR_COLOR) != 0;
    const CKBOOL clearDepth = (flags & CKRST_CTXCLEAR_DEPTH) != 0;
    if (!clearColor && !clearDepth)
        return TRUE;

    // GX only clears while copying the EFB out, so draw the clear instead.
    const int count = rectCount > 0 ? rectCount : 1;
    for (int i = 0; i < count; ++i)
    {
        float x0, y0, x1, y1;
        if (rectCount > 0)
        {
            x0 = (float)rects[i].left;
            y0 = (float)rects[i].top;
            x1 = (float)rects[i].right;
            y1 = (float)rects[i].bottom;
        }
        else
        {
            x0 = (float)m_Viewport.ViewX;
            y0 = (float)m_Viewport.ViewY;
            x1 = (float)(m_Viewport.ViewX + m_Viewport.ViewWidth);
            y1 = (float)(m_Viewport.ViewY + m_Viewport.ViewHeight);
        }
        if (x1 <= x0 || y1 <= y0)
            continue;
        DrawScreenQuad(x0, y0, x1, y1, z, color, clearColor, clearDepth);
        ++m_FrameClears;
    }
    m_FrameOpen = TRUE;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::BeginScene()
{
    if (!CanWork() || m_Phase != PHASE_IDLE)
    {
        if (CanWork())
            Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    m_Phase = PHASE_SCENE;
    m_FrameOpen = TRUE;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::EndScene()
{
    if (!CanWork() || m_Phase != PHASE_SCENE)
    {
        if (CanWork())
            Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    m_Phase = PHASE_IDLE;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::BeginOverlayPhase()
{
    if (!CanWork())
        return FALSE;
    if (m_Target)
    {
        Diag(CKRST_DIAG_OVERLAY_ON_TARGET);
        return FALSE;
    }
    if (m_Phase != PHASE_IDLE)
    {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    m_Phase = PHASE_OVERLAY;
    m_FrameOpen = TRUE;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::BackToFront(CKBOOL vsync)
{
    (void)vsync;
    if (!CanWork())
        return FALSE;
    if (m_Phase == PHASE_SCENE)
    {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    if (m_Target)
        SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS);

    DrawOverlay();
    DrawPointer();

    // Readbacks see the finished picture before the copy clears the EFB.
    if (m_Readbacks.Size() > 0)
    {
        GX_DrawDone();
        XClassArray<CKGXReadback> ready;
        ready.Swap(m_Readbacks);
        for (CKGXReadback *it = ready.Begin(); it != ready.End(); ++it)
        {
            VxImageDescEx image;
            image.Image = NULL;
            const int size = CopyToMemoryBuffer(it->HasRect ? &it->Rect : NULL, it->Buffer, image);
            if (size <= 0)
            {
                it->Callback(it->User, it->HasRect ? &it->Rect : NULL, it->Buffer, NULL, FALSE);
                continue;
            }
            XArray<CKBYTE> pixels;
            pixels.Resize(size);
            image.Image = pixels.Begin();
            CopyToMemoryBuffer(it->HasRect ? &it->Rect : NULL, it->Buffer, image);
            it->Callback(it->User, it->HasRect ? &it->Rect : NULL, it->Buffer, &image, TRUE);
        }
    }

    CopyToScreen();

    // The GPU is idle now: resources deleted during the frame can go.
    for (int i = 0; i < m_PendingFrees.Size(); ++i)
        ReleaseResource(m_PendingFrees[i]);
    m_PendingFrees.Clear();
    for (int i = 0; i < m_Resources.Size(); ++i)
    {
        if (m_Resources[i] && m_Resources[i]->Type == CKRST_OBJ_TEXTURE)
            m_Resources[i]->Texture.UsedThisFrame = FALSE;
    }

    m_Phase = PHASE_IDLE;
    m_FrameOpen = FALSE;
    ++m_Stats.FrameNumber;
    m_Stats.DrawCalls = m_FrameDrawCalls;
    m_Stats.Primitives = m_FramePrimitives;
    m_Stats.Passes = 1;
    m_Stats.Clears = m_FrameClears;
    m_Stats.TextureUploads = m_FrameTextureUploads;
    m_Stats.BufferUploads = m_FrameBufferUploads;
    m_FrameDrawCalls = m_FramePrimitives = m_FrameClears = 0;
    m_FrameTextureUploads = m_FrameBufferUploads = 0;
    return TRUE;
}

// ---------------------------------------------------------------------------
// State storage (translated to GX at draw time, see CKGXState.cpp)

CKBOOL CKGXRasterizerContext::SetRenderState(VXRENDERSTATETYPE state, CKDWORD value)
{
    if (!CKRSTIsValidRenderStateType((CKDWORD)state))
    {
        Diag(CKRST_DIAG_INVALID_RENDER_STATE);
        return FALSE;
    }
    m_RenderStates[(CKDWORD)state] = value;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::GetRenderState(VXRENDERSTATETYPE state, CKDWORD *value)
{
    if (!value)
        return FALSE;
    if (!CKRSTIsValidRenderStateType((CKDWORD)state))
    {
        Diag(CKRST_DIAG_INVALID_RENDER_STATE);
        return FALSE;
    }
    *value = m_RenderStates[(CKDWORD)state];
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value)
{
    if (stage < 0 || stage >= CKRST_MAX_TEXTURE_STAGES)
    {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    if (!CKRSTIsValidTextureStageStateType((CKDWORD)state))
    {
        Diag(CKRST_DIAG_INVALID_STAGE_STATE);
        return FALSE;
    }

    m_StageStates[stage][(CKDWORD)state] = value;
    m_StageSetMasks[stage] |= UINT64_C(1) << (CKDWORD)state;

    if (state == CKRST_TSS_ADDRESS)
    {
        const CKRST_TEXTURESTAGESTATETYPE axes[] = {CKRST_TSS_ADDRESSU, CKRST_TSS_ADDRESSV, CKRST_TSS_ADDRESW};
        for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); ++i)
        {
            m_StageStates[stage][(CKDWORD)axes[i]] = value;
            m_StageSetMasks[stage] |= UINT64_C(1) << (CKDWORD)axes[i];
        }
    }
    else if (state == CKRST_TSS_TEXTUREMAPBLEND)
    {
        // Combine states go back to "derived from TEXTUREMAPBLEND".
        const CKRST_TEXTURESTAGESTATETYPE combine[] = {
            CKRST_TSS_OP, CKRST_TSS_ARG1, CKRST_TSS_ARG2,
            CKRST_TSS_AOP, CKRST_TSS_AARG1, CKRST_TSS_AARG2,
            CKRST_TSS_COLORARG0, CKRST_TSS_ALPHAARG0, CKRST_TSS_RESULTARG0,
        };
        for (size_t i = 0; i < sizeof(combine) / sizeof(combine[0]); ++i)
        {
            m_StageStates[stage][(CKDWORD)combine[i]] = 0;
            m_StageSetMasks[stage] &= ~(UINT64_C(1) << (CKDWORD)combine[i]);
        }
    }
    else if (state == CKRST_TSS_STAGEBLEND && value != 0)
    {
        SetStageBlend(stage, value);
    }
    else if (state == CKRST_TSS_STAGEBLEND && value == 0 && stage > 0)
    {
        ResetTextureStages(stage, CKRST_MAX_TEXTURE_STAGES - stage);
    }
    return TRUE;
}

void CKGXRasterizerContext::SetStageBlend(int stage, CKDWORD stageBlend)
{
    const CKDWORD src = (stageBlend >> 4) & 0xF;
    const CKDWORD dst = stageBlend & 0xF;
    CKDWORD colorOp = CKRST_TOP_MODULATE;
    CKDWORD colorArg1 = CKRST_TA_TEXTURE;
    CKDWORD colorArg2 = CKRST_TA_CURRENT;
    if (src == VXBLEND_ONE && dst == VXBLEND_ONE)
        colorOp = CKRST_TOP_ADD;
    else if (src == VXBLEND_ONE && dst == VXBLEND_ZERO)
        colorOp = CKRST_TOP_SELECTARG1;
    else if (src == VXBLEND_ZERO && dst == VXBLEND_ONE)
        colorOp = CKRST_TOP_SELECTARG2;
    else if (src == VXBLEND_SRCALPHA && dst == VXBLEND_INVSRCALPHA)
        colorOp = CKRST_TOP_BLENDTEXTUREALPHA;
    else if (src == VXBLEND_SRCALPHA && dst == VXBLEND_ONE)
        colorOp = CKRST_TOP_MODULATEALPHA_ADDCOLOR;
    else if (src == VXBLEND_ONE && dst == VXBLEND_INVSRCALPHA)
        colorOp = CKRST_TOP_BLENDTEXTUREALPHAPM;
    else if (src == VXBLEND_DESTCOLOR && dst == VXBLEND_SRCCOLOR)
        colorOp = CKRST_TOP_MODULATE2X;
    else if (src == VXBLEND_INVSRCALPHA && dst == VXBLEND_SRCALPHA)
    {
        colorOp = CKRST_TOP_BLENDTEXTUREALPHA;
        colorArg1 = CKRST_TA_CURRENT;
        colorArg2 = CKRST_TA_TEXTURE;
    }

    const CKRST_TEXTURESTAGESTATETYPE keys[] = {
        CKRST_TSS_OP, CKRST_TSS_ARG1, CKRST_TSS_ARG2, CKRST_TSS_AOP, CKRST_TSS_AARG1, CKRST_TSS_AARG2,
    };
    const CKDWORD values[] = {
        colorOp, colorArg1, colorArg2, CKRST_TOP_SELECTARG2, CKRST_TA_TEXTURE, CKRST_TA_CURRENT,
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
    {
        m_StageStates[stage][(CKDWORD)keys[i]] = values[i];
        m_StageSetMasks[stage] |= UINT64_C(1) << (CKDWORD)keys[i];
    }
}

CKBOOL CKGXRasterizerContext::GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD *value)
{
    if (!value)
        return FALSE;
    if (stage < 0 || stage >= CKRST_MAX_TEXTURE_STAGES)
    {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    if (!CKRSTIsValidTextureStageStateType((CKDWORD)state))
    {
        Diag(CKRST_DIAG_INVALID_STAGE_STATE);
        return FALSE;
    }
    const uint64_t bit = UINT64_C(1) << (CKDWORD)state;
    *value = (m_StageSetMasks[stage] & bit) ? m_StageStates[stage][(CKDWORD)state]
                                            : CKRSTDefaultTextureStageStateValue(stage, state);
    return TRUE;
}

CKBOOL CKGXRasterizerContext::ResetTextureStages(int firstStage, int stageCount)
{
    if (firstStage < 0 || firstStage > CKRST_MAX_TEXTURE_STAGES || stageCount < 0 ||
        stageCount > CKRST_MAX_TEXTURE_STAGES - firstStage)
    {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    for (int stage = firstStage; stage < firstStage + stageCount; ++stage)
    {
        m_Textures[stage] = 0;
        memset(m_StageStates[stage], 0, sizeof(m_StageStates[stage]));
        m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
        // Every state reads back as set (zero) except the unset combine states.
        m_StageSetMasks[stage] = ((UINT64_C(1) << CKRST_TSS_MAXSTATE) - 1) & ~((UINT64_C(1) << CKRST_TSS_OP) - 1);
        const CKRST_TEXTURESTAGESTATETYPE combine[] = {
            CKRST_TSS_OP, CKRST_TSS_ARG1, CKRST_TSS_ARG2,
            CKRST_TSS_AOP, CKRST_TSS_AARG1, CKRST_TSS_AARG2,
            CKRST_TSS_COLORARG0, CKRST_TSS_ALPHAARG0, CKRST_TSS_RESULTARG0,
        };
        for (size_t i = 0; i < sizeof(combine) / sizeof(combine[0]); ++i)
            m_StageSetMasks[stage] &= ~(UINT64_C(1) << (CKDWORD)combine[i]);
        Vx3DMatrixIdentity(m_Matrices[CKRSTMatrixSlot(VXMATRIX_TEXTURE(stage))]);
    }
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetTexture(CKDWORD texture, int stage)
{
    if (stage < 0 || stage >= CKRST_MAX_TEXTURE_STAGES)
    {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    if (texture && !Find(CKRST_OBJ_TEXTURE, texture))
    {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    m_Textures[stage] = texture;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::GetTexture(int stage, CKDWORD *texture)
{
    if (!texture)
        return FALSE;
    if (stage < 0 || stage >= CKRST_MAX_TEXTURE_STAGES)
    {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    *texture = m_Textures[stage];
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix)
{
    const int slot = CKRSTMatrixSlot(type);
    if (slot < 0)
    {
        Diag(CKRST_DIAG_INVALID_MATRIX_TYPE);
        return FALSE;
    }
    m_Matrices[slot] = matrix;
    if (type == VXMATRIX_VIEW)
        m_LightsDirty = TRUE; // Lights live in eye space.
    return TRUE;
}

CKBOOL CKGXRasterizerContext::GetTransformMatrix(VXMATRIX_TYPE type, VxMatrix &matrix)
{
    const int slot = CKRSTMatrixSlot(type);
    if (slot < 0)
    {
        Diag(CKRST_DIAG_INVALID_MATRIX_TYPE);
        return FALSE;
    }
    matrix = m_Matrices[slot];
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetLight(CKDWORD index, const CKLightData *data)
{
    if (index >= CKRST_MAX_LIGHTS)
    {
        Diag(CKRST_DIAG_INVALID_LIGHT_INDEX);
        return FALSE;
    }
    if (!data)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    m_Lights[index] = *data;
    m_LightsDirty = TRUE;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::EnableLight(CKDWORD index, CKBOOL enable)
{
    if (index >= CKRST_MAX_LIGHTS)
    {
        Diag(CKRST_DIAG_INVALID_LIGHT_INDEX);
        return FALSE;
    }
    m_LightEnabled[index] = enable ? TRUE : FALSE;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetMaterial(const CKMaterialData *data)
{
    if (data)
        m_Material = *data;
    else
        ResetMaterial();
    return TRUE;
}

CKBOOL CKGXRasterizerContext::ApplyMaterial(const CKMaterialRenderState &state)
{
    m_Material = state.Material;
    SetRenderState(VXRENDERSTATE_CULLMODE, state.CullMode);
    SetRenderState(VXRENDERSTATE_FILLMODE, state.FillMode);
    SetRenderState(VXRENDERSTATE_SHADEMODE, state.ShadeMode);
    SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, state.AlphaBlend ? TRUE : FALSE);
    if (state.AlphaBlend)
    {
        SetRenderState(VXRENDERSTATE_SRCBLEND, state.SourceBlend);
        SetRenderState(VXRENDERSTATE_DESTBLEND, state.DestBlend);
    }
    SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
    SetRenderState(VXRENDERSTATE_ZWRITEENABLE, state.ZWrite ? TRUE : FALSE);
    SetRenderState(VXRENDERSTATE_ZFUNC, state.ZFunc);
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetViewport(const CKViewportData *data)
{
    if (!data)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    m_Viewport = *data;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetUserClipPlane(CKDWORD index, const VxPlane &plane)
{
    if (index >= CKRST_MAX_USER_CLIP_PLANES)
    {
        Diag(CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
        return FALSE;
    }
    // Stored for GetUserClipPlane; GX has no user clip planes.
    m_ClipPlanes[index] = plane;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::GetUserClipPlane(CKDWORD index, VxPlane &plane)
{
    if (index >= CKRST_MAX_USER_CLIP_PLANES)
    {
        Diag(CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
        return FALSE;
    }
    plane = m_ClipPlanes[index];
    return TRUE;
}

void CKGXRasterizerContext::InitDefaultRenderStatesValue()
{
    for (CKDWORD state = 0; state < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++state)
        m_RenderStates[state] = CKRSTDefaultRenderStateValue((VXRENDERSTATETYPE)state);
    memset(m_StageStates, 0, sizeof(m_StageStates));
    memset(m_StageSetMasks, 0, sizeof(m_StageSetMasks));
    memset(m_Textures, 0, sizeof(m_Textures));
}

// ---------------------------------------------------------------------------
// Draws

CKBOOL CKGXRasterizerContext::DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
                                            VxDrawPrimitiveData *data)
{
    if (!CanWork() || !data || data->VertexCount <= 0)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const int count = indices ? indexCount : data->VertexCount;
    if (!ValidPrimitive(type, count))
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    VertexSource source;
    memset(&source, 0, sizeof(source));
    source.Format = data->Flags;
    source.Position = (const CKBYTE *)data->PositionPtr;
    source.PositionStride = data->PositionStride;
    if (data->Flags & CKRST_DP_LIGHT)
    {
        source.Normal = (const CKBYTE *)data->NormalPtr;
        source.NormalStride = data->NormalStride;
    }
    if (data->Flags & CKRST_DP_DIFFUSE)
    {
        source.Diffuse = (const CKBYTE *)data->ColorPtr;
        source.DiffuseStride = data->ColorStride;
    }
    // CKRST_DP_STAGESn means texture coordinates for stages 0 to n.
    int stages = 0;
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage)
    {
        if (data->Flags & CKRST_DP_STAGE(stage))
            stages = stage + 1;
    }
    for (int stage = 0; stage < stages; ++stage)
    {
        source.Texcoord[stage] = (const CKBYTE *)(stage == 0 ? data->TexCoordPtr : data->TexCoordPtrs[stage - 1]);
        source.TexcoordStride[stage] = stage == 0 ? data->TexCoordStride : data->TexCoordStrides[stage - 1];
        if (source.Texcoord[stage])
            source.TexcoordCount = stage + 1;
    }
    if (!source.Position)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    return Submit(type, source, (CKDWORD)data->VertexCount, indices, indexCount, 0);
}

CKBOOL CKGXRasterizerContext::DrawPrimitiveVB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD startVertex,
                                              CKDWORD vertexCount, CKWORD *indices, int indexCount)
{
    CKGXResource *resource = Find(CKRST_OBJ_VERTEXBUFFER, vb);
    if (!CanWork() || !resource)
    {
        Diag(resource ? CKRST_DIAG_REJECT_INVALID_PARAMETER : CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKVertexBufferDesc &desc = resource->Buffer.VertexDesc;
    if (vertexCount == 0 || startVertex > desc.m_MaxVertexCount || vertexCount > desc.m_MaxVertexCount - startVertex)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (!ValidPrimitive(type, indices ? indexCount : (int)vertexCount))
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    VertexSource source;
    BuildVertexSource(desc.m_VertexFormat, resource->Buffer.Data + (size_t)startVertex * desc.m_VertexSize,
                      desc.m_VertexSize, desc.m_TexcoordDims, source);
    return Submit(type, source, vertexCount, indices, indexCount, 0);
}

CKBOOL CKGXRasterizerContext::DrawPrimitiveVBIB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
                                                CKDWORD minVertexIndex, CKDWORD vertexCount,
                                                CKDWORD startIndex, int indexCount)
{
    CKGXResource *vertices = Find(CKRST_OBJ_VERTEXBUFFER, vb);
    CKGXResource *indexBuffer = Find(CKRST_OBJ_INDEXBUFFER, ib);
    if (!CanWork() || !vertices || !indexBuffer)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKVertexBufferDesc &desc = vertices->Buffer.VertexDesc;
    if (vertexCount == 0 || indexCount <= 0 || minVertexIndex > desc.m_MaxVertexCount ||
        vertexCount > desc.m_MaxVertexCount - minVertexIndex ||
        startIndex > indexBuffer->Buffer.IndexDesc.m_MaxIndexCount ||
        (CKDWORD)indexCount > indexBuffer->Buffer.IndexDesc.m_MaxIndexCount - startIndex ||
        !ValidPrimitive(type, indexCount))
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    // Indices address the whole vertex buffer.
    VertexSource source;
    BuildVertexSource(desc.m_VertexFormat, vertices->Buffer.Data, desc.m_VertexSize, desc.m_TexcoordDims, source);
    const CKWORD *indices = (const CKWORD *)indexBuffer->Buffer.Data + startIndex;
    return Submit(type, source, desc.m_MaxVertexCount, indices, indexCount, 0);
}

// ---------------------------------------------------------------------------
// Resources

CKGXResource *CKGXRasterizerContext::Find(CKRST_OBJECTTYPE type, CKDWORD handle) const
{
    if (handle == 0 || handle > (CKDWORD)m_Resources.Size())
        return NULL;
    CKGXResource *resource = m_Resources[handle - 1];
    return resource && resource->Type == type ? resource : NULL;
}

CKBOOL CKGXRasterizerContext::Insert(CKGXResource *resource, CKDWORD *outHandle)
{
    for (int i = 0; i < m_Resources.Size(); ++i)
    {
        if (!m_Resources[i])
        {
            m_Resources[i] = resource;
            *outHandle = (CKDWORD)i + 1;
            return TRUE;
        }
    }
    m_Resources.PushBack(resource);
    *outHandle = (CKDWORD)m_Resources.Size();
    return TRUE;
}

void CKGXRasterizerContext::ReleaseResource(CKGXResource *resource)
{
    if (!resource)
        return;
    if (resource->Type == CKRST_OBJ_TEXTURE)
        free(resource->Texture.Data);
    else
        free(resource->Buffer.Data);
    delete resource;
}

CKBOOL CKGXRasterizerContext::GetTextureDesc(CKDWORD texture, CKTextureDesc *desc) const
{
    const CKGXResource *resource = Find(CKRST_OBJ_TEXTURE, texture);
    if (!desc || !resource)
        return FALSE;
    *desc = resource->Texture.Desc;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::CreateVertexBuffer(const CKVertexBufferDesc *desc, const void *data, CKDWORD *outHandle)
{
    if (outHandle)
        *outHandle = 0;
    if (!CanWork() || !desc || !outHandle || desc->m_MaxVertexCount == 0)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKDWORD stride = CKRSTGetVertexLayout(desc->m_VertexFormat, desc->m_TexcoordDims, NULL);
    if (!stride || (desc->m_VertexSize && desc->m_VertexSize != stride) || desc->m_MaxVertexCount > 0x7FFFFFFF / stride)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    CKGXResource *resource = new CKGXResource();
    memset(resource, 0, sizeof(CKGXResource));
    resource->Type = CKRST_OBJ_VERTEXBUFFER;
    resource->Buffer.VertexDesc = *desc;
    resource->Buffer.VertexDesc.m_VertexSize = stride;
    resource->Buffer.Size = desc->m_MaxVertexCount * stride;
    resource->Buffer.Data = (CKBYTE *)memalign(32, resource->Buffer.Size);
    if (!resource->Buffer.Data)
    {
        delete resource;
        return FALSE;
    }
    if (data)
    {
        memcpy(resource->Buffer.Data, data, resource->Buffer.Size);
        ++m_FrameBufferUploads;
    }
    else
    {
        memset(resource->Buffer.Data, 0, resource->Buffer.Size);
    }
    return Insert(resource, outHandle);
}

CKBOOL CKGXRasterizerContext::GetVertexBufferDesc(CKDWORD vb, CKVertexBufferDesc *desc) const
{
    const CKGXResource *resource = Find(CKRST_OBJ_VERTEXBUFFER, vb);
    if (!desc || !resource)
        return FALSE;
    *desc = resource->Buffer.VertexDesc;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::CreateIndexBuffer(const CKIndexBufferDesc *desc, const void *data, CKDWORD *outHandle)
{
    if (outHandle)
        *outHandle = 0;
    if (!CanWork() || !desc || !outHandle || desc->m_MaxIndexCount == 0 || desc->m_MaxIndexCount > 0x3FFFFFFF)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    CKGXResource *resource = new CKGXResource();
    memset(resource, 0, sizeof(CKGXResource));
    resource->Type = CKRST_OBJ_INDEXBUFFER;
    resource->Buffer.IndexDesc = *desc;
    resource->Buffer.Size = desc->m_MaxIndexCount * sizeof(CKWORD);
    resource->Buffer.Data = (CKBYTE *)memalign(32, resource->Buffer.Size);
    if (!resource->Buffer.Data)
    {
        delete resource;
        return FALSE;
    }
    if (data)
    {
        memcpy(resource->Buffer.Data, data, resource->Buffer.Size);
        ++m_FrameBufferUploads;
    }
    else
    {
        memset(resource->Buffer.Data, 0, resource->Buffer.Size);
    }
    return Insert(resource, outHandle);
}

CKBOOL CKGXRasterizerContext::GetIndexBufferDesc(CKDWORD ib, CKIndexBufferDesc *desc) const
{
    const CKGXResource *resource = Find(CKRST_OBJ_INDEXBUFFER, ib);
    if (!desc || !resource)
        return FALSE;
    *desc = resource->Buffer.IndexDesc;
    return TRUE;
}

void *CKGXRasterizerContext::LockVertexBuffer(CKDWORD vb, CKDWORD startVertex, CKDWORD vertexCount, CKRST_LOCKFLAGS flags)
{
    (void)flags;
    CKGXResource *resource = Find(CKRST_OBJ_VERTEXBUFFER, vb);
    if (!resource)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    const CKDWORD maximum = resource->Buffer.VertexDesc.m_MaxVertexCount;
    if (vertexCount == 0)
        vertexCount = startVertex < maximum ? maximum - startVertex : 0;
    if (resource->Buffer.Locked || startVertex >= maximum || vertexCount == 0 || vertexCount > maximum - startVertex)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    // Vertices are copied into the GPU FIFO at draw time, so the buffer is free to change.
    resource->Buffer.Locked = TRUE;
    return resource->Buffer.Data + (size_t)startVertex * resource->Buffer.VertexDesc.m_VertexSize;
}

CKBOOL CKGXRasterizerContext::UnlockVertexBuffer(CKDWORD vb)
{
    CKGXResource *resource = Find(CKRST_OBJ_VERTEXBUFFER, vb);
    if (!resource || !resource->Buffer.Locked)
    {
        Diag(resource ? CKRST_DIAG_REJECT_INVALID_PARAMETER : CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    resource->Buffer.Locked = FALSE;
    ++m_FrameBufferUploads;
    return TRUE;
}

void *CKGXRasterizerContext::LockIndexBuffer(CKDWORD ib, CKDWORD startIndex, CKDWORD indexCount, CKRST_LOCKFLAGS flags)
{
    (void)flags;
    CKGXResource *resource = Find(CKRST_OBJ_INDEXBUFFER, ib);
    if (!resource)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    const CKDWORD maximum = resource->Buffer.IndexDesc.m_MaxIndexCount;
    if (indexCount == 0)
        indexCount = startIndex < maximum ? maximum - startIndex : 0;
    if (resource->Buffer.Locked || startIndex >= maximum || indexCount == 0 || indexCount > maximum - startIndex)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    resource->Buffer.Locked = TRUE;
    return resource->Buffer.Data + (size_t)startIndex * sizeof(CKWORD);
}

CKBOOL CKGXRasterizerContext::UnlockIndexBuffer(CKDWORD ib)
{
    CKGXResource *resource = Find(CKRST_OBJ_INDEXBUFFER, ib);
    if (!resource || !resource->Buffer.Locked)
    {
        Diag(resource ? CKRST_DIAG_REJECT_INVALID_PARAMETER : CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    resource->Buffer.Locked = FALSE;
    ++m_FrameBufferUploads;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::DeleteObject(CKRST_HANDLE handle, CKRST_OBJECTTYPE type)
{
    if (type != CKRST_OBJ_TEXTURE && type != CKRST_OBJ_VERTEXBUFFER && type != CKRST_OBJ_INDEXBUFFER)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKGXResource *resource = Find(type, handle);
    if (!resource)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (type == CKRST_OBJ_TEXTURE)
    {
        if (m_Target == handle)
            m_Target = 0;
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage)
        {
            if (m_Textures[stage] == handle)
                m_Textures[stage] = 0;
        }
        // The GPU may still sample it this frame.
        if (resource->Texture.UsedThisFrame)
            m_PendingFrees.PushBack(resource);
        else
            ReleaseResource(resource);
    }
    else
    {
        ReleaseResource(resource);
    }
    m_Resources[handle - 1] = NULL;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::FlushObjects(CKRST_OBJECTMASK typeMask)
{
    for (int i = 0; i < m_Resources.Size(); ++i)
    {
        CKGXResource *resource = m_Resources[i];
        if (resource && (resource->Type & typeMask))
            DeleteObject((CKDWORD)i + 1, resource->Type);
    }
    return TRUE;
}

CKBOOL CKGXRasterizerContext::SetResourceName(CKRST_HANDLE handle, CKRST_OBJECTTYPE type, CKSTRING name)
{
    (void)name;
    return Find(type, handle) != NULL;
}

// ---------------------------------------------------------------------------
// Render targets: draw into the EFB, then copy the result into the texture.

CKBOOL CKGXRasterizerContext::SetTargetTexture(CKDWORD texture, int width, int height, CKRST_CUBEFACE face)
{
    if (!CanWork())
        return FALSE;
    if (m_Phase != PHASE_IDLE)
    {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    if (m_Target)
    {
        CKGXResource *previous = Find(CKRST_OBJ_TEXTURE, m_Target);
        if (previous && previous->Texture.Data)
        {
            CKGXTexture &target = previous->Texture;
            WaitForTextureUse(target);
            GX_SetTexCopySrc(0, 0, target.Width, target.Height);
            GX_SetTexCopyDst(target.Width, target.Height, target.Format, GX_FALSE);
            GX_CopyTex(target.Data, GX_FALSE);
            GX_PixModeSync();
            target.Loaded = TRUE;
            m_InvalidateTextures = TRUE;
        }
        m_Target = 0;
    }

    if (!texture)
    {
        m_TargetWidth = m_TargetHeight = 0;
        return TRUE;
    }

    CKGXResource *resource = Find(CKRST_OBJ_TEXTURE, texture);
    if (!resource)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKGXTexture &target = resource->Texture;
    if (!(target.Desc.Flags & CKRST_TEXTURE_RENDERTARGET) || (CKDWORD)face >= CKRST_CUBEFACE_COUNT ||
        (width > 0 && width != target.Desc.Format.Width) || (height > 0 && height != target.Desc.Format.Height) ||
        target.Width > m_Width || target.Height > m_Height)
    {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }
    m_Target = texture;
    m_TargetWidth = target.Width;
    m_TargetHeight = target.Height;
    return TRUE;
}

CKBOOL CKGXRasterizerContext::CopyToTexture(CKDWORD texture, const VxRect *src, const VxRect *dst, CKRST_CUBEFACE face)
{
    CKGXResource *resource = Find(CKRST_OBJ_TEXTURE, texture);
    if (!CanWork() || !resource || !resource->Texture.Data)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    (void)face;
    (void)dst; // The EFB copy always fills the whole texture.

    CKGXTexture &target = resource->Texture;
    int x = 0, y = 0, w = m_Width, h = m_Height;
    if (src)
    {
        x = (int)src->left;
        y = (int)src->top;
        w = (int)(src->right - src->left);
        h = (int)(src->bottom - src->top);
    }
    // GX copies either 1:1 or with a 2:1 box filter.
    const u8 halve = (w >= 2 * target.Width && h >= 2 * target.Height) ? GX_TRUE : GX_FALSE;
    w = halve ? target.Width * 2 : target.Width;
    h = halve ? target.Height * 2 : target.Height;
    if (x < 0 || y < 0 || x + w > m_Width || y + h > m_Height)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    WaitForTextureUse(target);
    GX_SetTexCopySrc(x, y, w, h);
    GX_SetTexCopyDst(target.Width, target.Height, target.Format, halve);
    GX_CopyTex(target.Data, GX_FALSE);
    GX_PixModeSync();
    target.Loaded = TRUE;
    m_InvalidateTextures = TRUE;
    ++m_FrameTextureUploads;
    return TRUE;
}

int CKGXRasterizerContext::CopyToMemoryBuffer(const CKRECT *rect, VXBUFFER_TYPE buffer, VxImageDescEx &image)
{
    if (!CanWork() || buffer != VXBUFFER_BACKBUFFER)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }
    CKRECT area = {0, 0, m_Width, m_Height};
    if (rect)
        area = *rect;
    if (area.left < 0 || area.top < 0 || area.right > m_Width || area.bottom > m_Height ||
        area.right <= area.left || area.bottom <= area.top)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }

    CKBYTE *destination = image.Image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = area.right - area.left;
    image.Height = area.bottom - area.top;
    image.BytesPerLine = image.Width * 4;
    image.Image = destination;
    const int size = image.BytesPerLine * image.Height;
    if (!destination)
        return size;

    GX_DrawDone();
    for (int y = 0; y < image.Height; ++y)
    {
        CKDWORD *row = (CKDWORD *)(destination + y * image.BytesPerLine);
        for (int x = 0; x < image.Width; ++x)
        {
            GXColor color;
            GX_PeekARGB((u16)(area.left + x), (u16)(area.top + y), &color);
            row[x] = 0xFF000000u | ((CKDWORD)color.r << 16) | ((CKDWORD)color.g << 8) | color.b;
        }
    }
    return size;
}

int CKGXRasterizerContext::CopyFromMemoryBuffer(const CKRECT *rect, VXBUFFER_TYPE buffer, const VxImageDescEx &image)
{
    if (!CanWork() || buffer != VXBUFFER_BACKBUFFER || !image.Image || image.Width <= 0 || image.Height <= 0)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }
    CKRECT area = rect ? *rect : CKRECT{0, 0, m_Width, m_Height};
    if (area.right - area.left != image.Width || area.bottom - area.top != image.Height)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }

    // Upload through a temporary texture and draw it over the area.
    CKTextureDesc desc;
    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;
    desc.Format = image;
    desc.MipMapCount = 1;
    CKDWORD handle = 0;
    if (!CreateTexture(&desc, &handle) || !LoadTexture(handle, image, 0, CKRST_CUBEFACE_XPOS, NULL))
    {
        if (handle)
            DeleteObject(handle, CKRST_OBJ_TEXTURE);
        return 0;
    }

    CKDWORD savedTexture = m_Textures[0];
    CKDWORD savedStates[CKRST_TSS_MAXSTATE];
    const uint64_t savedMask = m_StageSetMasks[0];
    memcpy(savedStates, m_StageStates[0], sizeof(savedStates));
    m_Textures[0] = handle;
    SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_COPY);
    SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);

    struct
    {
        float x, y, z, rhw;
        float u, v;
    } quad[4] = {
        {(float)area.left, (float)area.top, 0.0f, 1.0f, 0.0f, 0.0f},
        {(float)area.right, (float)area.top, 0.0f, 1.0f, 1.0f, 0.0f},
        {(float)area.right, (float)area.bottom, 0.0f, 1.0f, 1.0f, 1.0f},
        {(float)area.left, (float)area.bottom, 0.0f, 1.0f, 0.0f, 1.0f},
    };
    VertexSource source;
    memset(&source, 0, sizeof(source));
    source.Format = CKRST_DP_STAGES0;
    source.Position = (const CKBYTE *)&quad[0].x;
    source.PositionStride = sizeof(quad[0]);
    source.Texcoord[0] = (const CKBYTE *)&quad[0].u;
    source.TexcoordStride[0] = sizeof(quad[0]);
    source.TexcoordCount = 1;
    Submit(VX_TRIANGLEFAN, source, 4, NULL, 4, 0);

    m_Textures[0] = savedTexture;
    memcpy(m_StageStates[0], savedStates, sizeof(savedStates));
    m_StageSetMasks[0] = savedMask;
    DeleteObject(handle, CKRST_OBJ_TEXTURE);
    return image.Width * image.Height * 4;
}

CKBOOL CKGXRasterizerContext::RequestReadback(const CKRECT *rect, VXBUFFER_TYPE buffer,
                                              CKReadbackCallback callback, void *user)
{
    if (!CanWork() || buffer != VXBUFFER_BACKBUFFER || !callback)
    {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKGXReadback readback;
    readback.Callback = callback;
    readback.User = user;
    readback.Buffer = buffer;
    readback.HasRect = rect != NULL;
    if (rect)
        readback.Rect = *rect;
    m_Readbacks.PushBack(readback);
    return TRUE;
}
