// Media readers: little-endian image and sound files must decode to host-order
// ARGB pixel words and host-order samples.

#include "TestFramework.h"

#include "CKAll.h"

#include <string.h>

namespace
{
    // Builds little-endian files byte by byte.
    struct Writer
    {
        CKBYTE Data[256];
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
}

void RunReaderTests()
{
    wiitest::BeginSuite("Media readers");
    TestBmp();
    TestBmp16();
    TestTga();
    TestWav();
    wiitest::EndSuite();
}
