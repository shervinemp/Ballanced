#ifndef WIITESTS_TESTFRAMEWORK_H
#define WIITESTS_TESTFRAMEWORK_H

// A small checker for the on-console tests: results go to the OSReport UART
// (Dolphin's log window) and to a log file on the storage device.

class CKContext;

namespace wiitest
{
    bool OpenLog(const char *path);
    void CloseLog();

    void Log(const char *format, ...);
    void BeginSuite(const char *name);
    void EndSuite();

    // Records one check; failures are logged with their location.
    bool Check(bool condition, const char *file, int line, const char *format, ...);
    // Compares bytes and logs the first difference.
    bool CheckBytes(const void *actual, const void *expected, int size, const char *file, int line, const char *what);

    int CheckCount();
    int FailureCount();
    int SuiteFailureCount();
}

#define WT_CHECK(condition, ...) wiitest::Check((condition), __FILE__, __LINE__, __VA_ARGS__)
#define WT_CHECK_BYTES(actual, expected, size, what) wiitest::CheckBytes((actual), (expected), (size), __FILE__, __LINE__, (what))

// Suites
void RunChunkTests();
void RunFileTests(CKContext *context);
void RunReaderTests();
void RunRasterizerTests();
void RunSceneTests(CKContext *context);
void RunSoundTests(CKContext *context);
void RunPhysicsTests(CKContext *context);
void RunFontTests(CKContext *context);
void BuildDemoComposition(CKContext *context);

#endif // WIITESTS_TESTFRAMEWORK_H
