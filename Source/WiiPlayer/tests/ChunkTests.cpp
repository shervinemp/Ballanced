// State chunk byte layout: chunks must hold exactly the little-endian bytes a
// Windows build writes, and read them back to the same host values.

#include "TestFramework.h"

#include "CKAll.h"
#include "CKJpegDecoder.h"

#include <stdlib.h>

#include <string.h>

namespace
{
    // Little-endian words built independently of the engine.
    void PutLE32(CKBYTE *bytes, CKDWORD value)
    {
        bytes[0] = (CKBYTE)value;
        bytes[1] = (CKBYTE)(value >> 8);
        bytes[2] = (CKBYTE)(value >> 16);
        bytes[3] = (CKBYTE)(value >> 24);
    }

    CKDWORD FloatBits(float value)
    {
        CKDWORD bits;
        memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    const CKDWORD kFirstId = 0x12345678;
    const CKDWORD kSecondId = 0x9ABCDEF0;

    void TestScalarLayout()
    {
        CKStateChunk *chunk = CreateCKStateChunk(0, NULL);
        chunk->StartWrite();
        chunk->WriteIdentifier(kFirstId);
        chunk->WriteInt(0x11223344);
        chunk->WriteFloat(1.0f);
        chunk->WriteGuid(CKGUID(0x01020304, 0xA0B0C0D0));
        chunk->WriteString((CKSTRING) "abc");
        CKDWORD words[2] = {0x11223344, 0x55667788};
        chunk->WriteBuffer_LEndian(sizeof(words), words);
        chunk->WriteBuffer(4, (void *)"wxyz");
        CKWORD halves[2] = {0x1122, 0x3344};
        chunk->WriteBuffer_LEndian16(sizeof(halves), halves);
        VxVector vector(1.0f, 2.0f, 3.0f);
        chunk->WriteVector(vector);
        chunk->WriteByte(0x7F);
        chunk->WriteWord(0xBEEF);
        chunk->WriteIdentifier(kSecondId);
        chunk->WriteDword(0xCAFEBABE);
        chunk->CloseChunk();

        static const CKBYTE expected[] = {
            0x78, 0x56, 0x34, 0x12, 0x14, 0x00, 0x00, 0x00, // identifier, next at word 20
            0x44, 0x33, 0x22, 0x11,                         // int
            0x00, 0x00, 0x80, 0x3F,                         // 1.0f
            0x04, 0x03, 0x02, 0x01, 0xD0, 0xC0, 0xB0, 0xA0, // guid
            0x04, 0x00, 0x00, 0x00, 'a', 'b', 'c', 0x00,    // string
            0x08, 0x00, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55, // dword buffer
            0x04, 0x00, 0x00, 0x00, 'w', 'x', 'y', 'z',     // raw buffer
            0x04, 0x00, 0x00, 0x00, 0x22, 0x11, 0x44, 0x33, // word buffer
            0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40, // vector
            0x7F, 0x00, 0x00, 0x00,                         // byte
            0xEF, 0xBE, 0x00, 0x00,                         // word
            0xF0, 0xDE, 0xBC, 0x9A, 0x00, 0x00, 0x00, 0x00, // identifier
            0xBE, 0xBA, 0xFE, 0xCA,                         // dword
        };
        WT_CHECK(chunk->GetDataSize() == (int)sizeof(expected), "chunk size %d, expected %d",
                 chunk->GetDataSize(), (int)sizeof(expected));
        chunk->StartRead();
        WT_CHECK_BYTES(chunk->LockReadBuffer(), expected, sizeof(expected), "scalar chunk bytes");

        WT_CHECK(chunk->SeekIdentifier(kFirstId), "seek first identifier");
        WT_CHECK(chunk->ReadInt() == 0x11223344, "ReadInt");
        WT_CHECK(chunk->ReadFloat() == 1.0f, "ReadFloat");
        WT_CHECK(chunk->ReadGuid() == CKGUID(0x01020304, 0xA0B0C0D0), "ReadGuid");
        char *text = NULL;
        const int textSize = chunk->ReadString(&text);
        WT_CHECK(textSize == 4 && text && strcmp(text, "abc") == 0, "ReadString");
        delete[] text;
        CKDWORD words2[2] = {0, 0};
        chunk->ReadAndFillBuffer_LEndian(words2);
        WT_CHECK(words2[0] == words[0] && words2[1] == words[1], "dword buffer %08X %08X", words2[0], words2[1]);
        char raw[4] = {0};
        chunk->ReadAndFillBuffer(raw);
        WT_CHECK(memcmp(raw, "wxyz", 4) == 0, "raw buffer");
        CKWORD halves2[2] = {0, 0};
        chunk->ReadAndFillBuffer_LEndian16(halves2);
        WT_CHECK(halves2[0] == 0x1122 && halves2[1] == 0x3344, "word buffer %04X %04X", halves2[0], halves2[1]);
        VxVector vector2;
        chunk->ReadVector(vector2);
        WT_CHECK(vector2.x == 1.0f && vector2.y == 2.0f && vector2.z == 3.0f, "ReadVector");
        WT_CHECK(chunk->ReadByte() == 0x7F, "ReadByte");
        WT_CHECK(chunk->ReadWord() == 0xBEEF, "ReadWord");
        WT_CHECK(chunk->SeekIdentifier(kSecondId), "seek second identifier");
        WT_CHECK(chunk->ReadDword() == 0xCAFEBABE, "ReadDword");
        WT_CHECK(chunk->SeekIdentifierAndReturnSize(kFirstId) == 20 * 4 - 8, "identifier block size");

        // Sized reads and allocated buffers.
        chunk->SeekIdentifier(kFirstId);
        chunk->Skip(4); // int, float, guid: the string's size word is next
        void *allocated = NULL;
        WT_CHECK(chunk->ReadBuffer(&allocated) == 4 && allocated && memcmp(allocated, "abc", 4) == 0, "ReadBuffer");
        delete[] (CKBYTE *)allocated;
        chunk->Skip(1); // size word of the dword buffer
        CKDWORD words3[2] = {0, 0};
        chunk->ReadAndFillBuffer_LEndian(sizeof(words3), words3);
        WT_CHECK(words3[0] == words[0] && words3[1] == words[1], "sized dword buffer");
        DeleteCKStateChunk(chunk);
    }

    void TestArraysAndMatrices()
    {
        CKStateChunk *chunk = CreateCKStateChunk(0, NULL);
        chunk->StartWrite();
        CKDWORD ints[2] = {0x01020304, 0x05060708};
        chunk->WriteArray_LEndian(2, sizeof(CKDWORD), ints);
        CKWORD shorts[3] = {0x0102, 0x0304, 0x0506};
        chunk->WriteArray_LEndian16(3, sizeof(CKWORD), shorts);
        VxMatrix matrix;
        Vx3DMatrixIdentity(matrix);
        matrix[3][0] = 5.0f;
        chunk->WriteMatrix(matrix);
        chunk->CloseChunk();

        static const CKBYTE expected[] = {
            0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01, 0x08, 0x07, 0x06, 0x05,
            0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x01, 0x04, 0x03, 0x06, 0x05,
        };
        chunk->StartRead();
        const CKBYTE *bytes = (const CKBYTE *)chunk->LockReadBuffer();
        WT_CHECK_BYTES(bytes, expected, sizeof(expected), "array bytes");
        // Matrix row 3, column 0 is word 12 of the matrix, after 8 array words.
        static const CKBYTE five[] = {0x00, 0x00, 0xA0, 0x40};
        WT_CHECK_BYTES(bytes + (8 + 12) * 4, five, 4, "matrix translation bytes");

        void *array = NULL;
        WT_CHECK(chunk->ReadArray_LEndian(&array) == 2 && array &&
                     ((CKDWORD *)array)[0] == ints[0] && ((CKDWORD *)array)[1] == ints[1],
                 "ReadArray_LEndian");
        delete[] (CKBYTE *)array;
        array = NULL;
        WT_CHECK(chunk->ReadArray_LEndian16(&array) == 3 && array && ((CKWORD *)array)[0] == shorts[0] &&
                     ((CKWORD *)array)[1] == shorts[1] && ((CKWORD *)array)[2] == shorts[2],
                 "ReadArray_LEndian16");
        delete[] (CKBYTE *)array;
        VxMatrix matrix2;
        chunk->ReadMatrix(matrix2);
        WT_CHECK(matrix2[3][0] == 5.0f && matrix2[0][0] == 1.0f && matrix2[1][1] == 1.0f, "ReadMatrix");
        DeleteCKStateChunk(chunk);
    }

    // Object ids live in the chunk; their positions are host-order lists, and a
    // sub-chunk carries its lists inside the parent's data.
    void TestObjectIdsAndSubChunks()
    {
        CKStateChunk *sub = CreateCKStateChunk(0, NULL);
        sub->StartWrite();
        sub->WriteIdentifier(0x10);
        sub->WriteInt(7);
        sub->WriteObjectID(1234);
        sub->CloseChunk();

        CKStateChunk *parent = CreateCKStateChunk(0, NULL);
        parent->StartWrite();
        parent->WriteIdentifier(0x20);
        parent->WriteObjectID(1234);
        parent->WriteSubChunk(sub);
        parent->CloseChunk();
        DeleteCKStateChunk(sub);

        WT_CHECK(parent->RemapObject(1234, 5678) == 2, "remap reaches the chunk and its sub-chunk");

        // Through the file buffer form and back.
        const int size = parent->ConvertToBuffer(NULL);
        CKBYTE *buffer = new CKBYTE[size];
        parent->ConvertToBuffer(buffer);
        const CKDWORD header = (CKDWORD)buffer[0] | ((CKDWORD)buffer[1] << 8) | ((CKDWORD)buffer[2] << 16) |
                               ((CKDWORD)buffer[3] << 24);
        WT_CHECK((header >> 16 & 0xFF) == CHUNK_VERSION4, "file header chunk version %08X", header);
        WT_CHECK((header >> 24 & CHNK_OPTION_IDS) && (header >> 24 & CHNK_OPTION_CHN), "file header options %08X", header);
        DeleteCKStateChunk(parent);

        CKStateChunk *loaded = CreateCKStateChunk(0, NULL);
        WT_CHECK(loaded->ConvertFromBuffer(buffer), "ConvertFromBuffer");
        delete[] buffer;
        WT_CHECK(loaded->RemapObject(5678, 42) == 2, "remap after a file round trip");
        loaded->StartRead();
        WT_CHECK(loaded->SeekIdentifier(0x20), "seek parent identifier");
        WT_CHECK(loaded->ReadObjectID() == 42, "parent object id");
        CKStateChunk *sub2 = loaded->ReadSubChunk();
        WT_CHECK(sub2 != NULL, "ReadSubChunk");
        if (sub2)
        {
            sub2->StartRead();
            WT_CHECK(sub2->SeekIdentifier(0x10), "seek sub identifier");
            WT_CHECK(sub2->ReadInt() == 7, "sub int");
            WT_CHECK(sub2->ReadObjectID() == 42, "sub object id");
            DeleteCKStateChunk(sub2);
        }
        DeleteCKStateChunk(loaded);
    }

    // A chunk as a Windows build stores it in a file.
    void TestWindowsChunkBuffer()
    {
        CKBYTE buffer[10 * 4];
        const CKDWORD classId = 7;
        PutLE32(buffer + 0, ((CKDWORD)(CHUNK_VERSION4 | (CHNK_OPTION_IDS << 8)) << 16) | (classId << 8) | 2);
        PutLE32(buffer + 4, 6);                   // data words
        PutLE32(buffer + 8, 0x10);                // identifier
        PutLE32(buffer + 12, 0);                  // next identifier
        PutLE32(buffer + 16, 42);                 // int
        PutLE32(buffer + 20, FloatBits(2.5f));    // float
        PutLE32(buffer + 24, 99);                 // object id
        PutLE32(buffer + 28, 0x00636261);         // "abc"
        PutLE32(buffer + 32, 1);                  // id list size
        PutLE32(buffer + 36, 4);                  // id at word 4

        CKStateChunk *chunk = CreateCKStateChunk(0, NULL);
        WT_CHECK(chunk->ConvertFromBuffer(buffer), "ConvertFromBuffer of a Windows chunk");
        WT_CHECK(chunk->GetChunkClassID() == (CK_CLASSID)classId, "class id %d", chunk->GetChunkClassID());
        WT_CHECK(chunk->GetDataVersion() == 2, "data version %d", chunk->GetDataVersion());
        chunk->StartRead();
        WT_CHECK(chunk->SeekIdentifier(0x10), "seek identifier");
        WT_CHECK(chunk->ReadInt() == 42, "int");
        WT_CHECK(chunk->ReadFloat() == 2.5f, "float");
        WT_CHECK(chunk->ReadObjectID() == 99, "object id");
        char text[4];
        chunk->ReadAndFillBuffer(4, text);
        WT_CHECK(memcmp(text, "abc", 4) == 0, "raw bytes");
        WT_CHECK(chunk->RemapObject(99, 100) == 1, "remap through the loaded id list");
        chunk->SeekIdentifier(0x10);
        chunk->Skip(2);
        WT_CHECK(chunk->ReadObjectID() == 100, "remapped id");

        // Writing it back gives the same bytes, with the remapped id.
        CKBYTE out[sizeof(buffer)];
        WT_CHECK(chunk->ConvertToBuffer(NULL) == (int)sizeof(buffer), "buffer size");
        chunk->ConvertToBuffer(out);
        PutLE32(buffer + 24, 100);
        WT_CHECK_BYTES(out, buffer, sizeof(buffer), "Windows chunk written back");
        DeleteCKStateChunk(chunk);
    }
}

namespace
{
    void TestJpegPlanes()
    {
        const CKBYTE plane[4] = {0x10, 0x40, 0x70, 0xA0};
        CKBYTE *encoded = NULL;
        int encodedSize = 0;
        WT_CHECK(CKJpegDecoder::EncodeGrayscalePlane(plane, 2, 2, 85, &encoded, encodedSize), "JPEG plane encode");
        CKBYTE *decoded = NULL;
        WT_CHECK(encoded && CKJpegDecoder::DecodeGrayscalePlane(encoded, encodedSize, 2, 2, &decoded), "JPEG plane decode");
        if (decoded)
        {
            bool close = true;
            for (int i = 0; i < 4; ++i)
                close = close && abs((int)decoded[i] - (int)plane[i]) <= 8;
            WT_CHECK(close, "JPEG plane keeps its rows: %02X %02X %02X %02X", decoded[0], decoded[1], decoded[2],
                     decoded[3]);
        }
        delete[] encoded;
        delete[] decoded;
    }

    void TestRawBitmap()
    {
        const CKDWORD texels[4] = {0xFF102030, 0x80405060, 0xFF708090, 0x00A0B0C0};
        VxImageDescEx desc;
        VxPixelFormat2ImageDesc(_32_ARGB8888, desc);
        desc.Width = 2;
        desc.Height = 2;
        desc.BytesPerLine = 8;
        desc.Image = (XBYTE *)texels;
        CKStateChunk *chunk = CreateCKStateChunk(0, NULL);
        chunk->StartWrite();
        chunk->WriteRawBitmap(desc);
        chunk->CloseChunk();
        chunk->StartRead();
        VxImageDescEx read;
        CKBYTE *pixels = chunk->ReadRawBitmap(read);
        WT_CHECK(pixels != NULL && read.Width == 2 && read.Height == 2, "ReadRawBitmap");
        if (pixels)
        {
            // Raw bitmaps are stored bottom-up.
            const int order[4] = {2, 3, 0, 1};
            for (int i = 0; i < 4; ++i)
            {
                CKDWORD value;
                memcpy(&value, pixels + 4 * i, sizeof(value));
                const CKDWORD expected = texels[order[i]];
                bool close = (value >> 24) == (expected >> 24);
                for (int shift = 0; shift < 24; shift += 8)
                    close = close && abs((int)((value >> shift) & 0xFF) - (int)((expected >> shift) & 0xFF)) <= 16;
                WT_CHECK(close, "raw bitmap pixel %d %08X, expected %08X", i, value, expected);
            }
        }
        delete[] pixels;
        DeleteCKStateChunk(chunk);
    }
}

void RunChunkTests()
{
    wiitest::BeginSuite("State chunks");
    WT_CHECK(sizeof(CKChunkWord) == 4, "chunk words are 4 bytes");
    TestScalarLayout();
    TestArraysAndMatrices();
    TestObjectIdsAndSubChunks();
    TestWindowsChunkBuffer();
    TestJpegPlanes();
    TestRawBitmap();
    wiitest::EndSuite();
}
