// TrueType fonts: the Wii has no system fonts, so the font manager looks in a
// Fonts folder next to the game. A font placed there is found by name and
// renders into a white texture whose alpha is the glyph coverage.

#include "TestFramework.h"

#include "CKAll.h"
#include "CKFontManager.h"
#include "CKStbFontBackend.h"
#include "VxWiiPlatform.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Roboto Regular (Apache License 2.0), embedded by the build.
extern "C" const unsigned char kTestFont[];
extern "C" const unsigned int kTestFontSize;

namespace
{
    bool WriteFont(const XString &path)
    {
        FILE *fp = fopen(path.CStr(), "wb");
        if (!fp)
            return false;
        const bool written = fwrite(kTestFont, 1, kTestFontSize, fp) == kTestFontSize;
        return fclose(fp) == 0 && written;
    }

    int FindFontIndex(CKContext *context, const char *family)
    {
        CKParameterManager *pm = context->GetParameterManager();
        CKEnumStruct *names = pm->GetEnumDescByType(pm->ParameterGuidToType(CKPGUID_FONTNAME));
        if (!names)
            return -1;
        for (int i = 0; i < names->NbData; ++i)
            if (strcmp(names->Desc[i], family) == 0)
                return i;
        return -1;
    }

    void CheckAtlas(CKTexture *texture)
    {
        CKBYTE *surface = texture->LockSurfacePtr();
        if (!WT_CHECK(surface != NULL, "font texture surface"))
            return;
        const int count = texture->GetWidth() * texture->GetHeight();
        int covered = 0, partial = 0, coloured = 0;
        for (int i = 0; i < count; ++i)
        {
            CKDWORD texel;
            memcpy(&texel, surface + i * 4, sizeof(texel));
            const CKDWORD alpha = texel >> 24;
            if (alpha == 0)
                continue;
            ++covered;
            if (alpha < 0xFF)
                ++partial;
            if ((texel & 0x00FFFFFF) != 0x00FFFFFF)
                ++coloured;
        }
        texture->ReleaseSurfacePtr();
        WT_CHECK(covered > 100, "glyph texels %d", covered);
        WT_CHECK(partial > 0, "anti-aliased texels %d", partial);
        WT_CHECK(coloured == 0, "covered texels are white (%d are not)", coloured);
    }
}

void RunFontTests(CKContext *context)
{
    wiitest::BeginSuite("Fonts");
    CKFontManager *fonts = context ? (CKFontManager *)context->GetManagerByGuid(FONT_MANAGER_GUID) : NULL;
    if (!WT_CHECK(fonts != NULL, "font manager"))
    {
        wiitest::EndSuite();
        return;
    }

    XString folder(VxWiiGetApplicationPath());
    folder << "Fonts";
    XClassArray<XString> directories;
    CKStbDefaultFontDirectories(directories);
    WT_CHECK(directories.Size() == 1 && directories[0] == folder, "font folder %s",
             directories.Size() ? directories[0].CStr() : "(none)");

    mkdir(folder.CStr(), 0777);
    XString path(folder);
    path << "/WiiTestRoboto.ttf";
    if (!WT_CHECK(WriteFont(path), "%s written", path.CStr()))
    {
        wiitest::EndSuite();
        return;
    }

    XClassArray<CKStbSystemFontFace> faces;
    CKStbEnumerateFontDirectories(directories, faces);
    CKStbSystemFontFace face;
    WT_CHECK(CKStbFindBestFontFace(faces, "Roboto", 400, FALSE, face) && face.family == "Roboto",
             "Roboto found in the font folder");
    // Windows fonts the game names fall back to it (the install ships Roboto).
    WT_CHECK(CKStbFindBestFontFace(faces, "Arial", 700, FALSE, face) && face.family == "Roboto",
             "Arial falls back to Roboto");
    WT_CHECK(CKStbFindBestFontFace(faces, "Comic Sans MS", 400, TRUE, face) && face.family == "Roboto",
             "an unknown font falls back to Roboto");

    // Font textures join the current level, which the game always has.
    CKLevel *level = (CKLevel *)context->CreateObject(CKCID_LEVEL, (CKSTRING) "WiiFontLevel");
    context->SetCurrentLevel(level);

    // Fonts are listed by family; the folder may hold others already.
    fonts->RegenerateFontEnumeration();
    int index = FindFontIndex(context, "Roboto");
    if (index < 0)
        index = FindFontIndex(context, "Arial");
    if (WT_CHECK(index >= 0, "font listed in the Font Name enumeration"))
    {
        CKTexture *texture = fonts->CreateTextureFromFont(index, 2, FALSE, 400, FALSE, FALSE, FALSE, FALSE);
        if (WT_CHECK(texture != NULL, "font texture created"))
        {
            // 16 x 8 characters: the first 128 codes.
            WT_CHECK(texture->GetWidth() == 256 && texture->GetHeight() == 128, "font texture %dx%d",
                     texture->GetWidth(), texture->GetHeight());
            CheckAtlas(texture);
            context->DestroyObject(texture);
        }
    }

    // A font index from a PC's longer font list still gets a font.
    CKTexture *fallback = fonts->CreateTextureFromFont(57, 1, FALSE, 400, FALSE, FALSE, FALSE, FALSE);
    if (WT_CHECK(fallback != NULL, "a font index past the Wii's list falls back"))
        context->DestroyObject(fallback);

    context->SetCurrentLevel(NULL);
    context->DestroyObject(level);
    remove(path.CStr());
    rmdir(folder.CStr());
    wiitest::EndSuite();
}
