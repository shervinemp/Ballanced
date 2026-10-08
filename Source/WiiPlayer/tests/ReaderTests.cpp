// Media readers and writers: little-endian image and sound files must decode
// to host-order ARGB pixel words and host-order samples, images must save back
// to little-endian files, and VxMath's pixel conversions must agree with that
// layout. Movies decode to the same host-order pixels.

#include "TestFramework.h"

#include "CKAll.h"

#include <stdio.h>
#include <string.h>

namespace
{
    // Builds little-endian files byte by byte.
    struct Writer
    {
        CKBYTE Data[512];
        int Size;

        Writer() : Size(0) {}
        void U8(int v) { Data[Size++] = (CKBYTE)v; }
        void U16(int v)
        {
            U8(v);
            U8(v >> 8);
        }
        void U32(CKDWORD v)
        {
            U16((int)(v & 0xFFFF));
            U16((int)(v >> 16));
        }
        void Bytes(const char *text, int count)
        {
            for (int i = 0; i < count; ++i)
                U8(text[i]);
        }
    };

    CKDWORD LE16(const CKBYTE *bytes) { return (CKDWORD)(bytes[0] | (bytes[1] << 8)); }
    CKDWORD LE32(const CKBYTE *bytes) { return LE16(bytes) | (LE16(bytes + 2) << 16); }

    CKDWORD Pixel(const CKBitmapProperties *props, int x, int y)
    {
        const VxImageDescEx &f = props->m_Format;
        const CKBYTE *image = f.Image ? f.Image : (const CKBYTE *)props->m_Data;
        CKDWORD value;
        memcpy(&value, image + y * f.BytesPerLine + x * 4, sizeof(value));
        return value;
    }

    // Decoded image; the reader owns the properties until Release.
    struct Image
    {
        CKBitmapReader *Reader;
        CKBitmapProperties *Props;

        Image() : Reader(NULL), Props(NULL) {}
        ~Image()
        {
            if (Reader && Props && Props->m_Data)
            {
                Reader->ReleaseMemory(Props->m_Data);
                Props->m_Data = NULL;
            }
            if (Reader)
                Reader->Release();
        }
    };

    bool ReadImage(const char *extension, Writer &file, Image &image)
    {
        CKFileExtension ext((CKSTRING)extension);
        image.Reader = CKGetPluginManager()->GetBitmapReader(ext);
        WT_CHECK(image.Reader != NULL, "%s reader", extension);
        if (!image.Reader)
            return false;
        const int result = image.Reader->ReadMemory(file.Data, file.Size, &image.Props);
        WT_CHECK(result == 0 && image.Props, "%s decode %d", extension, result);
        return result == 0 && image.Props;
    }

    void TestBmp()
    {
        // 2x2, 24 bits, bottom-up rows padded to 8 bytes.
        Writer f;
        f.Bytes("BM", 2);
        f.U32(14 + 40 + 16);
        f.U16(0);
        f.U16(0);
        f.U32(14 + 40);
        f.U32(40);
        f.U32(2);
        f.U32(2);
        f.U16(1);
        f.U16(24);
        f.U32(0);
        f.U32(16);
        f.U32(2835);
        f.U32(2835);
        f.U32(0);
        f.U32(0);
        // Bottom row: blue, green. Top row: red, white. Pixels are B, G, R.
        f.U8(0xFF); f.U8(0x00); f.U8(0x00); f.U8(0x00); f.U8(0xFF); f.U8(0x00); f.U16(0);
        f.U8(0x00); f.U8(0x00); f.U8(0xFF); f.U8(0xFF); f.U8(0xFF); f.U8(0xFF); f.U16(0);

        Image image;
        if (!ReadImage("bmp", f, image))
            return;
        const CKBitmapProperties *props = image.Props;
        const VxImageDescEx &fmt = props->m_Format;
        WT_CHECK(fmt.Width == 2 && fmt.Height == 2 && fmt.BitsPerPixel == 32, "bmp format %dx%dx%d",
                 fmt.Width, fmt.Height, fmt.BitsPerPixel);
        WT_CHECK(fmt.RedMask == 0x00FF0000 && fmt.AlphaMask == 0xFF000000, "bmp masks");
        WT_CHECK(Pixel(props, 0, 0) == 0xFFFF0000, "bmp top-left %08X", Pixel(props, 0, 0));
        WT_CHECK(Pixel(props, 1, 0) == 0xFFFFFFFF, "bmp top-right %08X", Pixel(props, 1, 0));
        WT_CHECK(Pixel(props, 0, 1) == 0xFF0000FF, "bmp bottom-left %08X", Pixel(props, 0, 1));
        WT_CHECK(Pixel(props, 1, 1) == 0xFF00FF00, "bmp bottom-right %08X", Pixel(props, 1, 1));
    }

    void TestBmp16()
    {
        // 2x1, 16 bits (X1R5G5B5): red, blue.
        Writer f;
        f.Bytes("BM", 2);
        f.U32(14 + 40 + 4);
        f.U16(0);
        f.U16(0);
        f.U32(14 + 40);
        f.U32(40);
        f.U32(2);
        f.U32(1);
        f.U16(1);
        f.U16(16);
        f.U32(0);
        f.U32(4);
        f.U32(0);
        f.U32(0);
        f.U32(0);
        f.U32(0);
        f.U16(0x7C00);
        f.U16(0x001F);

        Image image;
        if (!ReadImage("bmp", f, image))
            return;
        const CKBitmapProperties *props = image.Props;
        WT_CHECK(Pixel(props, 0, 0) == 0xFFFF0000, "16-bit bmp red %08X", Pixel(props, 0, 0));
        WT_CHECK(Pixel(props, 1, 0) == 0xFF0000FF, "16-bit bmp blue %08X", Pixel(props, 1, 0));
    }

    void TestTga()
    {
        // 2x1, 32 bits, top-left origin: half-transparent blue, red.
        Writer f;
        f.U8(0);
        f.U8(0);
        f.U8(2);
        f.U16(0);
        f.U16(0);
        f.U8(0);
        f.U16(0);
        f.U16(0);
        f.U16(2);
        f.U16(1);
        f.U8(32);
        f.U8(0x28);
        f.U8(0xFF); f.U8(0x00); f.U8(0x00); f.U8(0x80);
        f.U8(0x00); f.U8(0x00); f.U8(0xFF); f.U8(0xFF);

        Image image;
        if (!ReadImage("tga", f, image))
            return;
        const CKBitmapProperties *props = image.Props;
        WT_CHECK(props->m_Format.Width == 2 && props->m_Format.Height == 1, "tga size %dx%d",
                 props->m_Format.Width, props->m_Format.Height);
        WT_CHECK(Pixel(props, 0, 0) == 0x800000FF, "tga first pixel %08X", Pixel(props, 0, 0));
        WT_CHECK(Pixel(props, 1, 0) == 0xFFFF0000, "tga second pixel %08X", Pixel(props, 1, 0));
    }

    // An image of host-order ARGB words.
    void Describe32(VxImageDescEx &f, int width, int height, CKDWORD *pixels)
    {
        f.Width = width;
        f.Height = height;
        f.BitsPerPixel = 32;
        f.BytesPerLine = width * 4;
        f.AlphaMask = 0xFF000000;
        f.RedMask = 0x00FF0000;
        f.GreenMask = 0x0000FF00;
        f.BlueMask = 0x000000FF;
        f.Image = (XBYTE *)pixels;
    }

    // Saves opaque red and half-transparent blue as a 24-bit file of headerSize
    // header bytes, checks the header fields and B, G, R pixel bytes, and
    // reads the file back.
    void TestSave(const char *extension, int headerSize, int widthOffset, int heightOffset, int fieldSize)
    {
        CKDWORD pixels[2] = {0xFFFF0000, 0x800000FF};
        CKBitmapProperties props;
        Describe32(props.m_Format, 2, 1, pixels);
        CKFileExtension ext((CKSTRING)extension);
        Image saver;
        saver.Reader = CKGetPluginManager()->GetBitmapReader(ext);
        if (!WT_CHECK(saver.Reader != NULL, "%s writer", extension))
            return;
        void *memory = NULL;
        const int size = saver.Reader->SaveMemory(&memory, &props);
        const CKBYTE *bytes = (const CKBYTE *)memory;
        if (WT_CHECK(bytes && size >= headerSize + 6, "%s saved %d bytes", extension, size))
        {
            const CKDWORD width = fieldSize == 2 ? LE16(bytes + widthOffset) : LE32(bytes + widthOffset);
            const CKDWORD height = fieldSize == 2 ? LE16(bytes + heightOffset) : LE32(bytes + heightOffset);
            WT_CHECK(width == 2 && height == 1, "%s header size %ux%u", extension, width, height);
            static const CKBYTE expected[6] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
            const CKBYTE *p = bytes + headerSize;
            WT_CHECK(memcmp(p, expected, sizeof(expected)) == 0, "%s pixels %02X %02X %02X %02X %02X %02X", extension,
                     p[0], p[1], p[2], p[3], p[4], p[5]);

            Image image;
            image.Reader = CKGetPluginManager()->GetBitmapReader(ext);
            if (image.Reader && image.Reader->ReadMemory(memory, size, &image.Props) == 0 && image.Props)
                WT_CHECK(Pixel(image.Props, 0, 0) == 0xFFFF0000 && Pixel(image.Props, 1, 0) == 0xFF0000FF,
                         "%s reads back %08X %08X", extension, Pixel(image.Props, 0, 0), Pixel(image.Props, 1, 0));
            else
                WT_CHECK(false, "%s reads back", extension);
        }
        if (memory)
            saver.Reader->ReleaseMemory(memory);
    }

    // 24-bit pixels are blue, green, red bytes whatever the host byte order.
    void TestBlit24()
    {
        CKDWORD argb[2] = {0xFF112233, 0x80445566};
        VxImageDescEx src;
        Describe32(src, 2, 1, argb);
        CKBYTE rgb[8] = {0};
        VxImageDescEx mid;
        mid.Width = 2;
        mid.Height = 1;
        mid.BitsPerPixel = 24;
        mid.BytesPerLine = 6;
        mid.RedMask = 0x00FF0000;
        mid.GreenMask = 0x0000FF00;
        mid.BlueMask = 0x000000FF;
        mid.Image = rgb;
        VxDoBlit(src, mid);
        static const CKBYTE expected[6] = {0x33, 0x22, 0x11, 0x66, 0x55, 0x44};
        WT_CHECK(memcmp(rgb, expected, sizeof(expected)) == 0, "32 to 24-bit blit %02X %02X %02X", rgb[0], rgb[1],
                 rgb[2]);
        CKDWORD back[2] = {0, 0};
        VxImageDescEx dst;
        Describe32(dst, 2, 1, back);
        VxDoBlit(mid, dst);
        WT_CHECK(back[0] == 0xFF112233 && back[1] == 0xFF445566, "24 to 32-bit blit %08X %08X", back[0], back[1]);
    }

    // Quantizing to a palette must see red as red: left half red, right half blue.
    void TestQuantize()
    {
        static CKDWORD argb[16 * 16];
        for (int i = 0; i < 16 * 16; ++i)
            argb[i] = (i % 16) < 8 ? 0xFFFF0000 : 0xFF0000FF;
        VxImageDescEx src;
        Describe32(src, 16, 16, argb);
        static CKBYTE indices[16 * 16];
        static CKBYTE palette[256 * 4];
        VxImageDescEx dst;
        dst.Width = 16;
        dst.Height = 16;
        dst.BitsPerPixel = 8;
        dst.BytesPerLine = 16;
        dst.ColorMapEntries = 256;
        dst.BytesPerColorEntry = 4;
        dst.ColorMap = palette;
        dst.Image = indices;
        VxDoBlit(src, dst);
        // Palette entries are blue, green, red(, alpha) bytes.
        const CKBYTE *red = palette + indices[0] * 4;
        const CKBYTE *blue = palette + indices[15] * 4;
        WT_CHECK(red[2] > red[1] + 100 && red[2] > red[0] + 100, "quantized red %02X %02X %02X", red[2], red[1],
                 red[0]);
        WT_CHECK(blue[0] > blue[1] + 100 && blue[0] > blue[2] + 100, "quantized blue %02X %02X %02X", blue[2],
                 blue[1], blue[0]);
    }

    // A flat 32-bit image becomes a bump map without slopes: each pixel is
    // the word 63 (luminance above the threshold, zero deltas).
    void TestBumpMap()
    {
        CKDWORD pixels[16];
        for (int i = 0; i < 16; ++i)
            pixels[i] = 0xFF808080;
        VxImageDescEx image;
        Describe32(image, 4, 4, pixels);
        WT_CHECK(VxConvertToBumpMap(image), "bump map converted");
        WT_CHECK(pixels[0] == 63 && pixels[5] == 63, "bump pixels %08X %08X", pixels[0], pixels[5]);
    }

    void TestWav()
    {
        // Mono, 16 bits, 22050 Hz: samples 0x1234 and -2.
        Writer f;
        f.Bytes("RIFF", 4);
        f.U32(36 + 4);
        f.Bytes("WAVE", 4);
        f.Bytes("fmt ", 4);
        f.U32(16);
        f.U16(1);
        f.U16(1);
        f.U32(22050);
        f.U32(44100);
        f.U16(2);
        f.U16(16);
        f.Bytes("data", 4);
        f.U32(4);
        f.U16(0x1234);
        f.U16(0xFFFE);

        CKFileExtension ext((CKSTRING) "wav");
        CKSoundReader *reader = CKGetPluginManager()->GetSoundReader(ext);
        WT_CHECK(reader != NULL, "wav reader");
        if (!reader)
            return;
        WT_CHECK(reader->ReadMemory(f.Data, f.Size) == CK_OK, "wav open");
        CKWaveFormat format;
        memset(&format, 0, sizeof(format));
        reader->GetWaveFormat(&format);
        WT_CHECK(format.nChannels == 1 && format.wBitsPerSample == 16 && format.nSamplesPerSec == 22050,
                 "wav format %d ch %d bits %d Hz", format.nChannels, format.wBitsPerSample, (int)format.nSamplesPerSec);
        CKBYTE *data = NULL;
        int size = 0;
        reader->Decode();
        reader->GetDataBuffer(&data, &size);
        WT_CHECK(size == 4 && data, "wav data %d bytes", size);
        if (data && size >= 4)
        {
            short samples[2];
            memcpy(samples, data, sizeof(samples));
            WT_CHECK(samples[0] == 0x1234 && samples[1] == -2, "wav samples %04X %d", (unsigned short)samples[0], samples[1]);
        }
        reader->Release();
    }

    // Ballance's Atari logo is a Microsoft Video 1 (CRAM) AVI. One 8x4 frame:
    // a red block, then a blue one.
    void TestAvi()
    {
        Writer f;
        f.Bytes("RIFF", 4);
        f.U32(252);
        f.Bytes("AVI ", 4);
        f.Bytes("LIST", 4);
        f.U32(192);
        f.Bytes("hdrl", 4);
        f.Bytes("avih", 4);
        f.U32(56);
        const CKDWORD avih[14] = {40000, 0, 0, 0x10, 1, 0, 1, 0, 8, 4, 0, 0, 0, 0};
        for (int i = 0; i < 14; ++i)
            f.U32(avih[i]);
        f.Bytes("LIST", 4);
        f.U32(116);
        f.Bytes("strl", 4);
        f.Bytes("strh", 4);
        f.U32(56);
        f.Bytes("vids", 4);
        f.Bytes("MSVC", 4);
        f.U32(0);
        f.U16(0);
        f.U16(0);
        const CKDWORD strh[8] = {0, 1, 25, 0, 1, 0, 0, 0};
        for (int i = 0; i < 8; ++i)
            f.U32(strh[i]);
        f.U16(0);
        f.U16(0);
        f.U16(8);
        f.U16(4);
        f.Bytes("strf", 4);
        f.U32(40);
        f.U32(40);
        f.U32(8);
        f.U32(4);
        f.U16(1);
        f.U16(16);
        f.Bytes("CRAM", 4);
        for (int i = 0; i < 5; ++i)
            f.U32(0);
        f.Bytes("LIST", 4);
        f.U32(16);
        f.Bytes("movi", 4);
        f.Bytes("00dc", 4);
        f.U32(4);
        f.U16(0xFC00); // one-colour block, RGB555 red
        f.U16(0x801F); // one-colour block, RGB555 blue
        f.Bytes("idx1", 4);
        f.U32(16);
        f.Bytes("00dc", 4);
        f.U32(0x10);
        f.U32(4);
        f.U32(4);
        if (!WT_CHECK(f.Size == 260, "avi file %d bytes", f.Size))
            return;

        const char *kPath = "sd:/wiimovietest.avi";
        FILE *file = fopen(kPath, "wb");
        if (!WT_CHECK(file != NULL, "write %s", kPath))
            return;
        fwrite(f.Data, 1, f.Size, file);
        fclose(file);

        CKFileExtension ext((CKSTRING) "avi");
        CKMovieReader *reader = CKGetPluginManager()->GetMovieReader(ext, NULL);
        if (WT_CHECK(reader != NULL, "avi reader"))
        {
            CKMovieProperties *props = NULL;
            const CKERROR opened = reader->OpenFile((CKSTRING)kPath);
            if (WT_CHECK(opened == CK_OK, "avi open %d", opened) &&
                WT_CHECK(reader->ReadFrame(0, &props) == CK_OK && props, "avi frame"))
            {
                WT_CHECK(reader->GetMovieFrameCount() == 1, "avi %d frames", reader->GetMovieFrameCount());
                const VxImageDescEx &format = props->m_Format;
                WT_CHECK(format.Width == 8 && format.Height == 4 && format.BitsPerPixel == 32,
                         "avi frame %dx%d %d bits", format.Width, format.Height, format.BitsPerPixel);
                const CKBYTE *image = format.Image ? format.Image : (const CKBYTE *)props->m_Data;
                if (image && format.Width == 8)
                {
                    CKDWORD left, right;
                    memcpy(&left, image, sizeof(left));
                    memcpy(&right, image + 4 * 4, sizeof(right));
                    WT_CHECK(left == 0xFFFF0000 && right == 0xFF0000FF, "avi pixels %08X %08X", left, right);
                }
            }
            reader->Release();
        }
        remove(kPath);
    }
}

void RunReaderTests()
{
    wiitest::BeginSuite("Media readers");
    TestBmp();
    TestBmp16();
    TestTga();
    TestSave("bmp", 54, 18, 22, 4);
    TestSave("tga", 18, 12, 14, 2);
    TestBlit24();
    TestQuantize();
    TestBumpMap();
    TestWav();
    TestAvi();
    wiitest::EndSuite();
}
