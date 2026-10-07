// Building blocks run on the console: blocks that edit textures must address
// the channels of host-order ARGB texels, and raw buffers that blocks save in
// files arrive little-endian.

#include "TestFramework.h"

#include <string.h>

#include "CKAll.h"

namespace
{
    const CKGUID kCombineTextureGuid(0x84BE2329, 0xAED66CE1);
    const CKGUID kTextureSineGuid(0x9c1208, 0x3a8d779e);

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

    // TT_TextureSine keeps the mesh's original UVs in a raw buffer, which the
    // engine loads byte for byte from a PC file.
    void TestTextureSineLoad(CKContext *context)
    {
        CKBehavior *beh = (CKBehavior *)context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "WiiTextureSine");
        if (WT_CHECK(beh->InitFromGuid(kTextureSineGuid) == CK_OK, "TT_TextureSine prototype"))
        {
            const float uvs[4] = {0.25f, 0.75f, -1.5f, 3.0f};
            CKBYTE file[sizeof(uvs)];
            for (int i = 0; i < 4; ++i)
            {
                CKDWORD bits;
                memcpy(&bits, &uvs[i], sizeof(bits));
                for (int b = 0; b < 4; ++b)
                    file[i * 4 + b] = (CKBYTE)(bits >> (8 * b));
            }
            beh->SetLocalParameterValue(0, file, sizeof(file));
            beh->CallCallbackFunction(CKM_BEHAVIORLOAD);

            const float *loaded = (const float *)beh->GetLocalParameterReadDataPtr(0);
            if (WT_CHECK(loaded != NULL, "saved UVs"))
                WT_CHECK(memcmp(loaded, uvs, sizeof(uvs)) == 0, "saved UVs %g %g %g %g, expected 0.25 0.75 -1.5 3",
                         loaded[0], loaded[1], loaded[2], loaded[3]);
        }
        context->DestroyObject(beh);
    }
}

void RunBehaviorTests(CKContext *context)
{
    wiitest::BeginSuite("Building blocks");
    if (WT_CHECK(context != NULL, "context"))
    {
        TestCombineTexture(context);
        TestTextureSineLoad(context);
    }
    wiitest::EndSuite();
}
