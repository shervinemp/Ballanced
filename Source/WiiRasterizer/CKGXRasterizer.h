#ifndef CKGXRASTERIZER_H
#define CKGXRASTERIZER_H

#include "CKRasterizer.h"

#include <gccore.h>

// Native GX rasterizer for the Nintendo Wii.
//
// Virtools drives a Direct3D 7-style fixed-function device. GX is a
// fixed-function GPU too, so the context maps the state directly:
//   transforms   -> XF position / normal / texture matrices
//   lights       -> GX hardware lights (eye space)
//   stage states -> TEV stages
//   render state -> Z, blend, alpha compare, cull and fog registers
// Vertices from engine memory are sent in immediate mode, so the engine may
// reuse that memory as soon as a draw returns. Vertex buffers are GX arrays
// instead: the GPU reads them itself and the CPU only sends indices, so a
// buffer still waiting to be drawn is copied rather than changed in place.
// Indices from an index buffer are recorded once as a display list, which the
// GPU fetches on its own every later frame. Larger indexed draws from engine
// memory (meshes that change every frame) are copied into a staging area once
// per vertex and drawn as GX arrays too, rather than sent whole for each index.

void CKGXRasterizerGetInfo(CKRasterizerInfo *info);

// CKRenderContextStateGuard on the Wii (WiiPatches/RenderEngine/state-guard.patch):
// FALSE when the context is not a GX one and the guard saves state itself.
CKBOOL CKGXPushGuardedState(CKRasterizerContext *context);
CKBOOL CKGXPopGuardedState(CKRasterizerContext *context);

class CKGXRasterizer : public CKRasterizer
{
public:
    CKBOOL Start(WIN_HANDLE appWindow) override;
};

class CKGXRasterizerDriver : public CKRasterizerDriver
{
public:
    explicit CKGXRasterizerDriver(CKRasterizer *owner);
    ~CKGXRasterizerDriver() override;

    CKRasterizerContext *CreateContext() override;
};

// A texture in GX tiled layout, mip levels stored back to back.
struct CKGXTexture
{
    CKTextureDesc Desc;     // As requested by the engine
    u16 Width;              // Native size (power of two, at most 1024)
    u16 Height;
    u8 Format;              // GX_TF_RGBA8 or GX_TF_RGB565
    u8 Levels;              // Mip levels stored
    CKBOOL GenerateMips;
    CKBOOL HasAlpha;
    CKBOOL Loaded;
    CKBOOL UsedThisFrame;
    void *Data;             // 32-byte aligned
    u32 DataSize;
};

// An index buffer range recorded for drawing from GX arrays: GX_Begin
// commands and each index repeated once per vertex attribute.
struct CKGXDisplayList
{
    void *Data;             // 32-byte aligned
    u32 Size;
    CKDWORD StartIndex;
    CKDWORD IndexCount;
    CKDWORD VertexCount;    // Indices from here on are drawn as 0
    u8 Primitive;           // VXPRIMITIVETYPE
    u8 Attributes;
};

struct CKGXBuffer
{
    CKBYTE *Data;           // 32-byte aligned; vertex buffers are read by the GPU
    CKDWORD Size;
    CKBOOL Locked;
    CKDWORD LockOffset;     // Bytes written through the current lock
    CKDWORD LockSize;
    CKBOOL UsedThisFrame;   // A queued draw reads Data / Colors
    u32 *Colors;            // Diffuse colours as GX RGBA, one per vertex
    CKVertexBufferDesc VertexDesc;
    CKIndexBufferDesc IndexDesc;
    // Index buffers
    CKGXDisplayList *Lists;
    int ListCount;
    int ListCapacity;
    CKDWORD LockedFrame;    // Frame number of the last lock + 1, 0 if never locked
};

struct CKGXResource
{
    CKRST_OBJECTTYPE Type;
    CKGXTexture Texture;
    CKGXBuffer Buffer;
};

struct CKGXReadback
{
    CKReadbackCallback Callback;
    void *User;
    CKRECT Rect;
    CKBOOL HasRect;
    VXBUFFER_TYPE Buffer;
};

class CKGXRasterizerContext : public CKRasterizerContext
{
public:
    explicit CKGXRasterizerContext(CKRasterizerDriver *driver);
    ~CKGXRasterizerContext() override;

    // Lifecycle
    CKBOOL Create(WIN_HANDLE window, int posX, int posY, int width, int height,
                  int bpp, CKBOOL fullscreen, int refreshRate, int zBpp, int stencilBpp) override;
    CKBOOL Resize(int posX, int posY, int width, int height, CKDWORD flags) override;
    CKBOOL SetOptions(const CKRasterizerOptions *options) override;
    CKBOOL GetCaps(CKRasterizerCapsDesc *caps) const override;
    CKERROR GetDeviceStatus() const override;
    CKBOOL BeginShutdown() override;
    CKBOOL IsIdle() const override;

    // Frame
    CKBOOL Clear(CKDWORD flags, CKDWORD color, float z, CKDWORD stencil, int rectCount, CKRECT *rects) override;
    CKBOOL BeginScene() override;
    CKBOOL EndScene() override;
    CKBOOL BeginOverlayPhase() override;
    CKBOOL BackToFront(CKBOOL vsync) override;

    // State
    CKBOOL SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) override;
    CKBOOL GetRenderState(VXRENDERSTATETYPE state, CKDWORD *value) override;
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override;
    CKBOOL GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD *value) override;
    CKBOOL ResetTextureStages(int firstStage, int stageCount) override;
    CKBOOL SetTexture(CKDWORD texture, int stage) override;
    CKBOOL GetTexture(int stage, CKDWORD *texture) override;
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override;
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE type, VxMatrix &matrix) override;
    CKBOOL SetLight(CKDWORD index, const CKLightData *data) override;
    CKBOOL EnableLight(CKDWORD index, CKBOOL enable) override;
    CKBOOL SetMaterial(const CKMaterialData *data) override;
    CKBOOL ApplyMaterial(const CKMaterialRenderState &state) override;
    CKBOOL SetViewport(const CKViewportData *data) override;
    CKBOOL SetUserClipPlane(CKDWORD index, const VxPlane &plane) override;
    CKBOOL GetUserClipPlane(CKDWORD index, VxPlane &plane) override;
    void InitDefaultRenderStatesValue() override;

    // Draws
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int indexCount, VxDrawPrimitiveData *data) override;
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD startVertex, CKDWORD vertexCount,
                           CKWORD *indices, int indexCount) override;
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib, CKDWORD minVertexIndex,
                             CKDWORD vertexCount, CKDWORD startIndex, int indexCount) override;

    // Resources
    CKBOOL CreateTexture(const CKTextureDesc *desc, CKDWORD *outHandle) override;
    CKBOOL LoadTexture(CKDWORD texture, const VxImageDescEx &image, int mipLevel, CKRST_CUBEFACE face,
                       const CKRECT *region) override;
    CKBOOL GetTextureDesc(CKDWORD texture, CKTextureDesc *desc) const override;
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *desc, const void *data, CKDWORD *outHandle) override;
    CKBOOL GetVertexBufferDesc(CKDWORD vb, CKVertexBufferDesc *desc) const override;
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *desc, const void *data, CKDWORD *outHandle) override;
    CKBOOL GetIndexBufferDesc(CKDWORD ib, CKIndexBufferDesc *desc) const override;
    void *LockVertexBuffer(CKDWORD vb, CKDWORD startVertex, CKDWORD vertexCount, CKRST_LOCKFLAGS flags) override;
    CKBOOL UnlockVertexBuffer(CKDWORD vb) override;
    void *LockIndexBuffer(CKDWORD ib, CKDWORD startIndex, CKDWORD indexCount, CKRST_LOCKFLAGS flags) override;
    CKBOOL UnlockIndexBuffer(CKDWORD ib) override;
    CKBOOL DeleteObject(CKRST_HANDLE handle, CKRST_OBJECTTYPE type) override;
    CKBOOL FlushObjects(CKRST_OBJECTMASK typeMask) override;
    CKBOOL SetResourceName(CKRST_HANDLE handle, CKRST_OBJECTTYPE type, CKSTRING name) override;

    // Targets and readback
    CKBOOL SetTargetTexture(CKDWORD texture, int width, int height, CKRST_CUBEFACE face) override;
    CKBOOL CopyToTexture(CKDWORD texture, const VxRect *src, const VxRect *dst, CKRST_CUBEFACE face) override;
    int CopyToMemoryBuffer(const CKRECT *rect, VXBUFFER_TYPE buffer, VxImageDescEx &image) override;
    int CopyFromMemoryBuffer(const CKRECT *rect, VXBUFFER_TYPE buffer, const VxImageDescEx &image) override;
    CKBOOL RequestReadback(const CKRECT *rect, VXBUFFER_TYPE buffer, CKReadbackCallback callback, void *user) override;

    // The state CKRenderContextStateGuard keeps (render and texture stage
    // states, textures, world / view / projection and texture matrices),
    // saved and restored in one copy instead of a call per state. Guards are
    // scoped, so the copies form a stack.
    void PushGuardedState();
    void PopGuardedState();

private:
    enum Phase
    {
        PHASE_IDLE,
        PHASE_SCENE,
        PHASE_OVERLAY
    };

    struct GuardedState
    {
        CKDWORD RenderStates[VXRENDERSTATE_MAXSTATE];
        CKDWORD StageStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE];
        uint64_t StageSetMasks[CKRST_MAX_TEXTURE_STAGES];
        CKDWORD ResetStages;
        CKDWORD Textures[CKRST_MAX_TEXTURE_STAGES];
        VxMatrix TextureMatrices[CKRST_MAX_TEXTURE_STAGES];
        VxMatrix World;
        VxMatrix View;
        VxMatrix Projection;
    };

    // A vertex stream described by the canonical layout or by separate pointers.
    struct VertexSource
    {
        CKDWORD Format;           // CKRST_DP_* vertex flags
        const CKBYTE *Position;
        CKDWORD PositionStride;
        const CKBYTE *Normal;
        CKDWORD NormalStride;
        const CKBYTE *Diffuse;
        CKDWORD DiffuseStride;
        const CKBYTE *Texcoord[CKRST_MAX_TEXTURE_STAGES];
        CKDWORD TexcoordStride[CKRST_MAX_TEXTURE_STAGES];
        int TexcoordCount;
        CKGXBuffer *Buffer;       // Set when the streams are GX arrays in a vertex buffer
    };

    CKBOOL CanWork() const { return m_Created && !m_ShuttingDown; }
    void Diag(CKRST_DIAGNOSTIC diagnostic);
    CKGXResource *Find(CKRST_OBJECTTYPE type, CKDWORD handle) const;
    CKBOOL Insert(CKGXResource *resource, CKDWORD *outHandle);
    void ReleaseResource(CKGXResource *resource);
    void StoreVertices(CKGXBuffer &buffer, CKDWORD offset, CKDWORD size);
    void GpuIdle();
    void ResetMaterial();
    void SetStageBlend(int stage, CKDWORD value);

    // Video
    void InitVideo();
    void CopyToScreen();
    void DrawOverlay();
    void DrawPointer();

    // State translation (CKGXState.cpp)
    void ApplyTransforms(CKBOOL pretransformed);
    void ApplyLights();
    void ApplyChannels(const VertexSource &source, CKBOOL pretransformed, CKBOOL *litOut);
    int ApplyTextureStages(const VertexSource &source, CKBOOL pretransformed, CKBOOL lit);
    void ApplyPixelState(CKBOOL pretransformed);
    void ApplyViewport(CKBOOL pretransformed);

    // What the GX registers hold from the last draw, so the next one only sends
    // what changed. Code that sets GX state itself (clears, system screens,
    // the pointer, presenting) calls ForgetAppliedState afterwards.
    void ForgetAppliedState();

    // Draw submission (CKGXDraw.cpp)
    CKBOOL Submit(VXPRIMITIVETYPE type, const VertexSource &source, CKDWORD vertexCount,
                  const CKWORD *indices, int indexCount, CKDWORD baseVertex,
                  CKGXBuffer *indexBuffer = NULL, CKDWORD startIndex = 0);
    CKBOOL CallDisplayList(CKGXBuffer &indexBuffer, VXPRIMITIVETYPE type, CKDWORD startIndex, int indexCount,
                           CKDWORD vertexCount, int attributes);
    CKBOOL StageVertices(const VertexSource &source, CKDWORD vertexCount, bool normals, int texcoordSets,
                         VertexSource &staged);
    void ReleaseDisplayLists(CKGXBuffer &indexBuffer, CKBOOL gpuMayRead);
    void BuildVertexSource(CKGXBuffer &buffer, CKDWORD startVertex, VertexSource &source) const;
    void DrawScreenQuad(float x0, float y0, float x1, float y1, float z, CKDWORD color,
                        CKBOOL writeColor, CKBOOL writeDepth);

    // Textures (CKGXTextures.cpp)
    CKBOOL BindTexture(int stage, int texmap);
    void WaitForTextureUse(CKGXTexture &texture);

    // Device
    GXRModeObj *m_Mode;
    void *m_FrameBuffers[2];
    int m_FrameBuffer;
    void *m_Fifo;
    Phase m_Phase;
    CKBOOL m_FrameOpen;
    CKBOOL m_InvalidateTextures;

    // Render target
    CKDWORD m_Target;
    int m_TargetWidth;
    int m_TargetHeight;

    // Resources: handle = index + 1
    XArray<CKGXResource *> m_Resources;
    XArray<CKGXResource *> m_PendingFrees;
    XArray<void *> m_PendingBufferFrees;    // Vertex data replaced while the GPU may read it
    CKBOOL m_VertexCacheDirty;
    u32 m_DisplayListBytes;
    u8 *m_Staging;              // Vertices of this frame's staged draws
    u32 m_StagingUsed;

    // Engine-visible state
    CKDWORD m_RenderStates[VXRENDERSTATE_MAXSTATE];
    CKDWORD m_StageStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE];
    uint64_t m_StageSetMasks[CKRST_MAX_TEXTURE_STAGES];
    CKDWORD m_ResetStages;      // Bit per stage untouched since ResetTextureStages
    CKDWORD m_Textures[CKRST_MAX_TEXTURE_STAGES];
    VxMatrix m_Matrices[CKRST_MATRIX_SLOT_COUNT];
    CKLightData m_Lights[CKRST_MAX_LIGHTS];
    CKBOOL m_LightEnabled[CKRST_MAX_LIGHTS];
    CKBOOL m_LightsDirty;
    CKMaterialData m_Material;
    CKViewportData m_Viewport;
    VxPlane m_ClipPlanes[CKRST_MAX_USER_CLIP_PLANES];

    enum AppliedTransform
    {
        TRANSFORM_UNKNOWN,
        TRANSFORM_SCREEN,   // Pretransformed vertices, in pixels
        TRANSFORM_WORLD
    };

    struct AppliedTexMap
    {
        bool Valid;
        void *Data;
        u16 Width, Height;
        u8 Format, WrapS, WrapT, MinFilter, MagFilter, Anisotropy, MaxLod;
        bool Mipmapped;
        float Bias;
    };

    struct AppliedState
    {
        AppliedTransform Transform;
        int ScreenWidth, ScreenHeight;
        VxMatrix World, View, Projection;

        bool ViewportValid;
        f32 Viewport[6];
        u32 Scissor[4];

        bool ChannelsValid;
        u8 ChannelKey[16];

        bool VertexFormatValid;
        u32 VertexFormat;

        bool PixelValid;
        u8 ZMode[3];
        u8 AlphaCompare[3];
        u8 Blend[4];
        u8 Updates[2];
        u8 Cull;
        u8 Dither;
        u8 PointSize;
        u8 FogType;
        f32 Fog[4];
        GXColor FogColor;

        AppliedTexMap TexMaps[8];
    };

    XClassArray<CKGXReadback> m_Readbacks;
    AppliedState m_Applied;
    XClassArray<GuardedState> m_GuardedStates;
    int m_GuardDepth;

    // Statistics for the current frame
    CKDWORD m_FrameDrawCalls;
    CKDWORD m_FramePrimitives;
    CKDWORD m_FrameClears;
    CKDWORD m_FrameTextureUploads;
    CKDWORD m_FrameBufferUploads;
};

#endif // CKGXRASTERIZER_H
