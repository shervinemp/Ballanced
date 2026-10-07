// Database.tdb holds Ballance's settings, key bindings and high scores. The
// file is written by the PC game, so its ints and floats are little-endian
// and every byte is scrambled; saving must write the same layout back.

#include "TestFramework.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "CKAll.h"
#include "DatabaseManager.h"

namespace
{
    const char *kArrayName = "WiiDbTest";
    const char *kPath = "sd:/wiidbtest.tdb";

    unsigned char RotateRight(unsigned char value, int shift)
    {
        return (unsigned char)((value >> shift) | (value << (8 - shift)));
    }

    unsigned char RotateLeft(unsigned char value, int shift)
    {
        return (unsigned char)((value << shift) | (value >> (8 - shift)));
    }

    // The PC game's file layout, built byte by byte.
    class FileImage
    {
    public:
        FileImage() : m_Size(0) {}

        void String(const char *text)
        {
            const int length = (int)strlen(text) + 1;
            memcpy(m_Data + m_Size, text, length);
            m_Size += length;
        }

        void Int(int value)
        {
            const unsigned int bits = (unsigned int)value;
            for (int i = 0; i < 4; ++i)
                m_Data[m_Size++] = (unsigned char)(bits >> (8 * i));
        }

        void Float(float value)
        {
            unsigned int bits;
            memcpy(&bits, &value, sizeof(bits));
            Int((int)bits);
        }

        void PatchInt(int offset, int value)
        {
            const int size = m_Size;
            m_Size = offset;
            Int(value);
            m_Size = size;
        }

        int Size() const { return m_Size; }
        const unsigned char *Data() const { return m_Data; }

    private:
        unsigned char m_Data[512];
        int m_Size;
    };

    // One array: name, size, header, columns, then the values column by column.
    void BuildArray(FileImage &image, int bestScore)
    {
        image.String(kArrayName);
        const int sizeOffset = image.Size();
        image.Int(0);
        const int start = image.Size();

        image.Int(3);  // columns
        image.Int(2);  // rows
        image.Int(-1); // no key column
        image.String("Name");
        image.Int(CKARRAYTYPE_STRING);
        image.String("Score");
        image.Int(CKARRAYTYPE_INT);
        image.String("Time");
        image.Int(CKARRAYTYPE_FLOAT);

        image.String("Mr. Ball");
        image.String("Wii");
        image.Int(bestScore);
        image.Int(-7);
        image.Float(12.5f);
        image.Float(0.25f);

        image.PatchInt(sizeOffset, image.Size() - start);
    }

    bool WriteScrambled(const char *path, const FileImage &image)
    {
        unsigned char data[512];
        for (int i = 0; i < image.Size(); ++i)
            data[i] = RotateRight((unsigned char)(-image.Data()[i] ^ 0xAF), 3);
        FILE *file = fopen(path, "wb");
        if (!file)
            return false;
        const bool written = fwrite(data, 1, image.Size(), file) == (size_t)image.Size();
        fclose(file);
        return written;
    }

    int ReadUnscrambled(const char *path, unsigned char *data, int capacity)
    {
        FILE *file = fopen(path, "rb");
        if (!file)
            return -1;
        const int size = (int)fread(data, 1, capacity, file);
        fclose(file);
        for (int i = 0; i < size; ++i)
            data[i] = (unsigned char)-(RotateLeft(data[i], 3) ^ 0xAF);
        return size;
    }

    void CheckRows(CKDataArray *array, int bestScore)
    {
        if (!WT_CHECK(array->GetColumnCount() == 3 && array->GetRowCount() == 2, "array is %dx%d, expected 3x2",
                      array->GetColumnCount(), array->GetRowCount()))
            return;
        WT_CHECK(array->GetColumnType(0) == CKARRAYTYPE_STRING && array->GetColumnType(1) == CKARRAYTYPE_INT &&
                     array->GetColumnType(2) == CKARRAYTYPE_FLOAT,
                 "column types %d %d %d", array->GetColumnType(0), array->GetColumnType(1), array->GetColumnType(2));
        WT_CHECK(strcmp(array->GetColumnName(1), "Score") == 0, "column name '%s'", array->GetColumnName(1));

        CKSTRING name = NULL;
        array->GetElementValue(0, 0, &name);
        WT_CHECK(name && strcmp(name, "Mr. Ball") == 0, "name '%s'", name ? name : "(null)");
        int score = 0, other = 0;
        array->GetElementValue(0, 1, &score);
        array->GetElementValue(1, 1, &other);
        WT_CHECK(score == bestScore && other == -7, "scores %d %d, expected %d -7", score, other, bestScore);
        float time = 0.0f, otherTime = 0.0f;
        array->GetElementValue(0, 2, &time);
        array->GetElementValue(1, 2, &otherTime);
        WT_CHECK(time == 12.5f && otherTime == 0.25f, "times %g %g, expected 12.5 0.25", time, otherTime);
    }

    void TestLoadAndSave(CKContext *context, DatabaseManager *manager)
    {
        FileImage image;
        BuildArray(image, 1234);
        if (!WT_CHECK(WriteScrambled(kPath, image), "write %s", kPath))
            return;

        CKDataArray *array = (CKDataArray *)context->CreateObject(CKCID_DATAARRAY, (CKSTRING)kArrayName);
        manager->SetProperty((CKSTRING)kPath, TRUE);
        const int loaded = manager->Load(context, true, (CKSTRING)kArrayName);
        if (WT_CHECK(loaded == 1, "load returned %d", loaded))
            CheckRows(array, 1234);

        // A new best score goes back to the file in the PC layout.
        const int best = 5678;
        array->SetElementValue(0, 1, (void *)&best, sizeof(best));
        const int saved = manager->Save(context);
        WT_CHECK(saved == 1, "save returned %d", saved);

        FileImage expected;
        BuildArray(expected, best);
        unsigned char data[512];
        const int size = ReadUnscrambled(kPath, data, sizeof(data));
        if (WT_CHECK(size == expected.Size(), "saved %d bytes, expected %d", size, expected.Size()))
            WT_CHECK_BYTES(data, expected.Data(), size, "saved database");

        // And reads back unchanged.
        array->Clear();
        if (WT_CHECK(manager->Load(context, false, (CKSTRING)kArrayName) == 1, "reload"))
            CheckRows(array, best);

        manager->Clear();
        context->DestroyObject(array);
        remove(kPath);
    }

    // Ballance names the file the way the PC player sees it, a folder above the
    // game; the game folder is the current directory on the Wii.
    void TestRelativeName(CKContext *context, DatabaseManager *manager)
    {
        char cwd[256];
        if (!getcwd(cwd, sizeof(cwd)))
        {
            wiitest::Log("  No current directory: relative name check skipped");
            return;
        }
        FileImage image;
        BuildArray(image, 42);
        if (!WT_CHECK(WriteScrambled("wiidbtest.tdb", image), "write wiidbtest.tdb in %s", cwd))
            return;

        CKDataArray *array = (CKDataArray *)context->CreateObject(CKCID_DATAARRAY, (CKSTRING)kArrayName);
        manager->SetProperty((CKSTRING) "..\\wiidbtest.tdb", TRUE);
        const int loaded = manager->Load(context, true, (CKSTRING)kArrayName);
        if (WT_CHECK(loaded == 1, "load of ..\\wiidbtest.tdb from %s returned %d", cwd, loaded))
            CheckRows(array, 42);
        // Saving goes to the file that was found.
        WT_CHECK(manager->Save(context) == 1, "save next to the game");

        manager->Clear();
        context->DestroyObject(array);
        remove("wiidbtest.tdb");
    }
}

void RunDatabaseTests(CKContext *context)
{
    wiitest::BeginSuite("Game database");
    DatabaseManager *manager = context ? DatabaseManager::GetManager(context) : NULL;
    if (WT_CHECK(manager != NULL, "TT Database Manager"))
    {
        TestLoadAndSave(context, manager);
        TestRelativeName(context, manager);
    }
    wiitest::EndSuite();
}
