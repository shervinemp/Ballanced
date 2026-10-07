#include "TestFramework.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace wiitest
{
    namespace
    {
        FILE *g_Log = NULL;
        const char *g_Suite = "";
        int g_Checks = 0;
        int g_Failures = 0;
        int g_SuiteChecks = 0;
        int g_SuiteFailures = 0;

        void Write(const char *text)
        {
            fputs(text, stdout);
            fflush(stdout);
            if (g_Log)
            {
                fputs(text, g_Log);
                fflush(g_Log);
            }
        }

        const char *BaseName(const char *path)
        {
            const char *slash = strrchr(path, '/');
            const char *backslash = strrchr(path, '\\');
            if (backslash && (!slash || backslash > slash))
                slash = backslash;
            return slash ? slash + 1 : path;
        }
    }

    bool OpenLog(const char *path)
    {
        g_Log = fopen(path, "w");
        return g_Log != NULL;
    }

    void CloseLog()
    {
        if (g_Log)
            fclose(g_Log);
        g_Log = NULL;
    }

    void Log(const char *format, ...)
    {
        char text[512];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        Write(text);
        Write("\n");
    }

    void BeginSuite(const char *name)
    {
        g_Suite = name;
        g_SuiteChecks = 0;
        g_SuiteFailures = 0;
        Log("== %s", name);
    }

    void EndSuite()
    {
        Log("== %s: %d checks, %d failed", g_Suite, g_SuiteChecks, g_SuiteFailures);
    }

    bool Check(bool condition, const char *file, int line, const char *format, ...)
    {
        ++g_Checks;
        ++g_SuiteChecks;
        if (condition)
            return true;
        ++g_Failures;
        ++g_SuiteFailures;

        char text[512];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        Log("FAIL %s:%d: %s", BaseName(file), line, text);
        return false;
    }

    bool CheckBytes(const void *actual, const void *expected, int size, const char *file, int line, const char *what)
    {
        if (!actual)
            return Check(false, file, line, "%s: no data", what);
        const unsigned char *a = (const unsigned char *)actual;
        const unsigned char *e = (const unsigned char *)expected;
        for (int i = 0; i < size; ++i)
        {
            if (a[i] != e[i])
                return Check(false, file, line, "%s: byte %d is %02X, expected %02X", what, i, a[i], e[i]);
        }
        return Check(true, file, line, "%s", what);
    }

    int CheckCount()
    {
        return g_Checks;
    }

    int FailureCount()
    {
        return g_Failures;
    }

    int SuiteFailureCount()
    {
        return g_SuiteFailures;
    }
}
