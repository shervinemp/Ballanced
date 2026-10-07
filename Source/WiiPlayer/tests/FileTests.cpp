// Virtools file round trip: objects saved on the console must come back with
// the same values, and the file must have the little-endian Windows layout.

#include "TestFramework.h"

#include "CKAll.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace
{
    const char *kPath = "sd:/wiitest.nmo";
    const CKGUID kFormatReader(0x11223344, 0x55667788);

    CKDWORD LE32At(const CKBYTE *bytes)
    {
        return (CKDWORD)bytes[0] | ((CKDWORD)bytes[1] << 8) | ((CKDWORD)bytes[2] << 16) | ((CKDWORD)bytes[3] << 24);
    }

    CKWORD LE16At(const CKBYTE *bytes)
    {
        return (CKWORD)(bytes[0] | (bytes[1] << 8));
    }

    bool Near8(float a, float b)
    {
        const float d = a - b;
        return d < 1.5f / 255.0f && d > -1.5f / 255.0f;
    }

    bool Near(float a, float b)
    {
        const float d = a - b;
        return d < 1e-4f && d > -1e-4f;
    }

    void CheckFileHeader()
    {
        FILE *fp = fopen(kPath, "rb");
        WT_CHECK(fp != NULL, "saved file opens");
        if (!fp)
            return;
        CKBYTE header[64];
        const size_t read = fread(header, 1, sizeof(header), fp);
        fclose(fp);
        WT_CHECK(read == sizeof(header), "file header size");
        WT_CHECK(memcmp(header, "Nemo Fi", 8) == 0, "signature");
        WT_CHECK(LE32At(header + 16) == 8, "file version %08X (little-endian 8)", LE32At(header + 16));
        WT_CHECK(LE32At(header + 20) == 0, "second file version");
        WT_CHECK(LE32At(header + 44) >= 6, "object count %u", LE32At(header + 44));
    }

    // A texture's save format is a structure stored whole in its chunk, in the
    // file byte order like everything else.
    void CheckSaveFormatBytes(CKTexture *texture)
    {
        CKStateChunk *chunk = CKSaveObjectState(texture);
        if (!WT_CHECK(chunk != NULL, "texture state saved"))
            return;
        chunk->StartRead();
        if (WT_CHECK(chunk->SeekIdentifier(CK_STATESAVE_TEXSAVEFORMAT), "texture save format saved"))
        {
            void *buffer = NULL;
            const int size = chunk->ReadBuffer(&buffer);
            const CKBYTE *bytes = (const CKBYTE *)buffer;
            if (WT_CHECK(bytes && size == (int)sizeof(CKBitmapProperties), "save format size %d", size))
            {
                WT_CHECK(LE32At(bytes) == sizeof(CKBitmapProperties), "save format m_Size %08X", LE32At(bytes));
                const CKBYTE *guid = bytes + offsetof(CKBitmapProperties, m_ReaderGuid);
                WT_CHECK(LE32At(guid) == kFormatReader.d1 && LE32At(guid + 4) == kFormatReader.d2,
                         "save format reader %08X %08X", LE32At(guid), LE32At(guid + 4));
                WT_CHECK(memcmp(bytes + offsetof(CKBitmapProperties, m_Ext), "tga", 4) == 0, "save format extension");
                const CKDWORD width = LE32At(bytes + offsetof(CKBitmapProperties, m_Format.Width));
                WT_CHECK(width == 0x01020304, "save format width %08X", width);
                const CKWORD entry = LE16At(bytes + offsetof(CKBitmapProperties, m_Format.BytesPerColorEntry));
                WT_CHECK(entry == 0x0506, "save format colour entry size %04X", entry);
            }
            CKDeletePointer(buffer);
        }
        DeleteCKStateChunk(chunk);
    }
}

void RunFileTests(CKContext *context)
{
    wiitest::BeginSuite("Virtools files");
    if (!context)
    {
        WT_CHECK(false, "no CK context");
        wiitest::EndSuite();
        return;
    }
    context->SetGlobalImagesSaveOptions(CKTEXTURE_RAWDATA);

    CKDataArray *array = (CKDataArray *)context->CreateObject(CKCID_DATAARRAY, (CKSTRING) "WiiTestArray");
    array->InsertColumn(-1, CKARRAYTYPE_INT, (CKSTRING) "Int");
    array->InsertColumn(-1, CKARRAYTYPE_FLOAT, (CKSTRING) "Float");
    array->InsertColumn(-1, CKARRAYTYPE_STRING, (CKSTRING) "String");
    array->InsertRow();
    int intValue = 123456789;
    float floatValue = -2.5f;
    array->SetElementValue(0, 0, &intValue, sizeof(intValue));
    array->SetElementValue(0, 1, &floatValue, sizeof(floatValue));
    array->SetElementValue(0, 2, (void *)"hello", 6);

    CKParameterLocal *vectorParam = context->CreateCKParameterLocal((CKSTRING) "WiiTestVector", CKPGUID_VECTOR);
    VxVector vectorValue(1.5f, -2.0f, 3.25f);
    vectorParam->SetValue(&vectorValue);
    CKParameterLocal *intParam = context->CreateCKParameterLocal((CKSTRING) "WiiTestInt", CKPGUID_INT);
    int paramInt = 0x01020304;
    intParam->SetValue(&paramInt);
    CKParameterLocal *stringParam = context->CreateCKParameterLocal((CKSTRING) "WiiTestString", CKPGUID_STRING);
    stringParam->SetStringValue((CKSTRING) "Ballance");

    CKMaterial *material = (CKMaterial *)context->CreateObject(CKCID_MATERIAL, (CKSTRING) "WiiTestMaterial");
    material->SetDiffuse(VxColor(0.25f, 0.5f, 0.75f, 1.0f));

    CKTexture *texture = (CKTexture *)context->CreateObject(CKCID_TEXTURE, (CKSTRING) "WiiTestTexture");
    const CKDWORD texels[4] = {0xFF102030, 0x80405060, 0xFF708090, 0x00A0B0C0};
    if (texture->Create(2, 2, 32))
    {
        CKBYTE *surface = texture->LockSurfacePtr();
        if (surface)
            memcpy(surface, texels, sizeof(texels));
        texture->ReleaseSurfacePtr();
    }
    material->SetTexture0(texture);
    CKBitmapProperties saveFormat;
    saveFormat.m_ReaderGuid = kFormatReader;
    saveFormat.m_Ext = CKFileExtension("tga");
    saveFormat.m_Format.Width = 0x01020304;
    saveFormat.m_Format.BytesPerColorEntry = 0x0506;
    texture->SetSaveFormat(&saveFormat);
    CheckSaveFormatBytes(texture);

    CKMesh *mesh = (CKMesh *)context->CreateObject(CKCID_MESH, (CKSTRING) "WiiTestMesh");
    mesh->SetVertexCount(3);
    VxVector positions[3] = {VxVector(1, 2, 3), VxVector(4, 5, 6), VxVector(7, 8, 9)};
    for (int i = 0; i < 3; ++i)
    {
        mesh->SetVertexPosition(i, &positions[i]);
        VxVector normal(0, 0, -1);
        mesh->SetVertexNormal(i, &normal);
        mesh->SetVertexTextureCoordinates(i, 0.25f * i, 1.0f - 0.25f * i);
        mesh->SetVertexColor(i, 0xFF112233 + i);
    }
    mesh->SetFaceCount(1);
    mesh->SetFaceVertexIndex(0, 0, 1, 2);
    mesh->SetFaceMaterial(0, material);

    CK3dEntity *entity = (CK3dEntity *)context->CreateObject(CKCID_3DENTITY, (CKSTRING) "WiiTestEntity");
    VxVector entityPosition(10.0f, -20.0f, 30.5f);
    entity->SetPosition(&entityPosition);
    entity->SetCurrentMesh(mesh);

    CKObject *saved[] = {array, vectorParam, intParam, stringParam, material, texture, mesh, entity};
    const int savedCount = sizeof(saved) / sizeof(saved[0]);

    wiitest::Log("saving %s", kPath);
    CKFile *file = context->CreateCKFile();
    CKERROR err = file->StartSave((CKSTRING)kPath);
    WT_CHECK(err == CK_OK, "StartSave %d", err);
    file->SaveObjects(saved, savedCount);
    err = file->EndSave();
    WT_CHECK(err == CK_OK, "EndSave %d", err);
    context->DeleteCKFile(file);
    CheckFileHeader();

    for (int i = 0; i < savedCount; ++i)
        context->DestroyObject(saved[i]);
    wiitest::Log("loading %s", kPath);

    CKObjectArray *loaded = CreateCKObjectArray();
    file = context->CreateCKFile();
    err = file->Load((CKSTRING)kPath, loaded);
    WT_CHECK(err == CK_OK, "Load %d", err);
    context->DeleteCKFile(file);
    // Parameters are not listed, the other objects are.
    WT_CHECK(loaded->GetCount() >= savedCount - 3, "loaded %d objects", loaded->GetCount());
    wiitest::Log("checking loaded objects");
    DeleteCKObjectArray(loaded);

    array = (CKDataArray *)context->GetObjectByNameAndClass((CKSTRING) "WiiTestArray", CKCID_DATAARRAY);
    WT_CHECK(array != NULL, "data array loaded");
    if (array)
    {
        int i2 = 0;
        float f2 = 0.0f;
        char *s2 = NULL;
        array->GetElementValue(0, 0, &i2);
        array->GetElementValue(0, 1, &f2);
        array->GetElementValue(0, 2, &s2);
        WT_CHECK(array->GetColumnCount() == 3 && array->GetRowCount() == 1, "array shape");
        WT_CHECK(i2 == intValue, "array int %d", i2);
        WT_CHECK(f2 == floatValue, "array float %f", f2);
        WT_CHECK(s2 && strcmp(s2, "hello") == 0, "array string '%s'", s2 ? s2 : "(null)");
    }

    CKParameterLocal *p = (CKParameterLocal *)context->GetObjectByNameAndClass((CKSTRING) "WiiTestVector", CKCID_PARAMETERLOCAL);
    WT_CHECK(p != NULL, "vector parameter loaded");
    if (p)
    {
        VxVector v;
        p->GetValue(&v);
        WT_CHECK(v.x == 1.5f && v.y == -2.0f && v.z == 3.25f, "vector parameter %f %f %f", v.x, v.y, v.z);
    }
    p = (CKParameterLocal *)context->GetObjectByNameAndClass((CKSTRING) "WiiTestInt", CKCID_PARAMETERLOCAL);
    WT_CHECK(p != NULL, "int parameter loaded");
    if (p)
    {
        int v = 0;
        p->GetValue(&v);
        WT_CHECK(v == paramInt, "int parameter %08X", v);
    }
    p = (CKParameterLocal *)context->GetObjectByNameAndClass((CKSTRING) "WiiTestString", CKCID_PARAMETERLOCAL);
    WT_CHECK(p != NULL, "string parameter loaded");
    if (p)
    {
        const char *v = (const char *)p->GetReadDataPtr();
        WT_CHECK(v && strcmp(v, "Ballance") == 0, "string parameter '%s'", v ? v : "(null)");
    }

    material = (CKMaterial *)context->GetObjectByNameAndClass((CKSTRING) "WiiTestMaterial", CKCID_MATERIAL);
    WT_CHECK(material != NULL, "material loaded");
    if (material)
    {
        const VxColor &c = material->GetDiffuse();
        // Material colours are stored as 8-bit channels.
        WT_CHECK(Near8(c.r, 0.25f) && Near8(c.g, 0.5f) && Near8(c.b, 0.75f), "material diffuse %f %f %f", c.r, c.g, c.b);
    }

    texture = (CKTexture *)context->GetObjectByNameAndClass((CKSTRING) "WiiTestTexture", CKCID_TEXTURE);
    WT_CHECK(texture != NULL, "texture loaded");
    if (texture)
    {
        WT_CHECK(texture->GetWidth() == 2 && texture->GetHeight() == 2, "texture size %dx%d",
                 texture->GetWidth(), texture->GetHeight());
        CKBYTE *surface = texture->LockSurfacePtr();
        WT_CHECK(surface != NULL, "texture surface");
        if (surface)
        {
            // Raw texture data keeps alpha exactly; colour planes are JPEG-coded.
            CKDWORD texels2[4];
            memcpy(texels2, surface, sizeof(texels2));
            for (int i = 0; i < 4; ++i)
            {
                bool close = (texels2[i] >> 24) == (texels[i] >> 24);
                for (int shift = 0; shift < 24; shift += 8)
                {
                    const int a = (int)((texels2[i] >> shift) & 0xFF);
                    const int b = (int)((texels[i] >> shift) & 0xFF);
                    close = close && a - b <= 16 && b - a <= 16;
                }
                WT_CHECK(close, "texel %d %08X, expected %08X", i, texels2[i], texels[i]);
            }
        }
        texture->ReleaseSurfacePtr();
        WT_CHECK(!material || material->GetTexture() == texture, "material texture reference");
        const CKBitmapProperties *format = texture->GetSaveFormat();
        WT_CHECK(format && format->m_ReaderGuid == kFormatReader && format->m_Format.Width == 0x01020304 &&
                     format->m_Format.BytesPerColorEntry == 0x0506,
                 "texture save format loaded");
    }

    mesh = (CKMesh *)context->GetObjectByNameAndClass((CKSTRING) "WiiTestMesh", CKCID_MESH);
    WT_CHECK(mesh != NULL, "mesh loaded");
    if (mesh)
    {
        WT_CHECK(mesh->GetVertexCount() == 3 && mesh->GetFaceCount() == 1, "mesh shape");
        for (int i = 0; i < 3 && i < mesh->GetVertexCount(); ++i)
        {
            VxVector v;
            mesh->GetVertexPosition(i, &v);
            WT_CHECK(v == positions[i], "vertex %d %f %f %f", i, v.x, v.y, v.z);
            float u = 0, w = 0;
            mesh->GetVertexTextureCoordinates(i, &u, &w);
            WT_CHECK(Near(u, 0.25f * i) && Near(w, 1.0f - 0.25f * i), "uv %d %f %f", i, u, w);
            WT_CHECK(mesh->GetVertexColor(i) == 0xFF112233 + (CKDWORD)i, "vertex color %d %08X", i, mesh->GetVertexColor(i));
        }
        WT_CHECK(mesh->GetFaceMaterial(0) == material, "face material reference");
    }

    entity = (CK3dEntity *)context->GetObjectByNameAndClass((CKSTRING) "WiiTestEntity", CKCID_3DENTITY);
    WT_CHECK(entity != NULL, "entity loaded");
    if (entity)
    {
        VxVector v;
        entity->GetPosition(&v);
        WT_CHECK(v == entityPosition, "entity position %f %f %f", v.x, v.y, v.z);
        WT_CHECK(entity->GetCurrentMesh() == mesh, "entity mesh reference");
    }

    remove(kPath);
    wiitest::EndSuite();
}
