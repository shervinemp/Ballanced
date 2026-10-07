// Building blocks run on the console: blocks that edit textures must address
// the channels of host-order ARGB texels.

#include "TestFramework.h"

#include "CKAll.h"

namespace
{
    const CKGUID kCombineTextureGuid(0x84BE2329, 0xAED66CE1);

    CKTexture *CreateTexture(CKContext *context, const char *name, CKDWORD texel)
    {
        CKTexture *texture = (CKTexture *)context->CreateObject(CKCID_TEXTURE, (CKSTRING)name);
        if (!texture->Create(2, 1, 32))
            return texture;
        CKDWORD *texels = (CKDWORD *)texture->LockSurfacePtr();
        if (texels)
            texels[0] = texels[1] = texel;
        texture->ReleaseSurfacePtr();
        return texture;
    }

    void SetInput(CKContext *context, CKParameterIn *input, const void *value, int size)
    {
        CKParameterLocal *source = context->CreateCKParameterLocal((CKSTRING) "WiiTestInput", input->GetGUID());
        source->SetValue(value, size);
        input->SetDirectSource(source);
    }

    void SetObjectInput(CKContext *context, CKParameterIn *input, CKObject *object)
    {
        CK_ID id = object ? object->GetID() : 0;
        SetInput(context, input, &id, sizeof(id));
    }

    // Combine Texture blends a colour texture into its target channel by
    // channel, then takes the alpha from an alpha texture's blue channel.
    void TestCombineTexture(CKContext *context)
    {
        CKTexture *target = CreateTexture(context, "WiiCombineTarget", 0xFF102030);
        CKTexture *colours = CreateTexture(context, "WiiCombineColours", 0x80405060);
        CKTexture *alpha = CreateTexture(context, "WiiCombineAlpha", 0x112233C0);
        CKBehavior *beh = (CKBehavior *)context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "WiiCombine");
        if (WT_CHECK(beh->InitFromGuid(kCombineTextureGuid) == CK_OK, "Combine Texture prototype"))
        {
            beh->UseTarget(TRUE);
            SetObjectInput(context, beh->GetTargetParameter(), target);
            SetObjectInput(context, beh->GetInputParameter(0), colours);
            // Per channel: red from the colours, green kept, blue halfway.
            VxColor filter(1.0f, 0.0f, 0.5f, 0.0f);
            SetInput(context, beh->GetInputParameter(1), &filter, sizeof(filter));
            SetObjectInput(context, beh->GetInputParameter(2), alpha);
            beh->ActivateInput(0);
            beh->Execute(0.0f);

            CKDWORD *texels = (CKDWORD *)target->LockSurfacePtr();
            if (WT_CHECK(texels != NULL, "combined texture"))
                WT_CHECK(texels[0] == 0xC0402048 && texels[1] == 0xC0402048, "combined texel %08X, expected C0402048",
                         texels[0]);
            target->ReleaseSurfacePtr();
        }
        context->DestroyObject(beh);
        context->DestroyObject(target);
        context->DestroyObject(colours);
        context->DestroyObject(alpha);
    }
}

void RunBehaviorTests(CKContext *context)
{
    wiitest::BeginSuite("Building blocks");
    if (WT_CHECK(context != NULL, "context"))
        TestCombineTexture(context);
    wiitest::EndSuite();
}
