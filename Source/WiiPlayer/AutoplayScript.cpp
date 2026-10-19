#include "AutoplayScript.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Logger.h"
#include "Utils.h"
#include "WiiAutoplay.h"
#include "WiiGamePlayer.h"
#include "WiiPhysicsShapes.h"
#include "WiiProfiler.h"
#include "WiiSystem.h"

namespace
{
    char *NextToken(char *&cursor)
    {
        while (*cursor && isspace((unsigned char)*cursor))
            ++cursor;
        if (!*cursor)
            return NULL;
        char *token = cursor;
        while (*cursor && !isspace((unsigned char)*cursor))
            ++cursor;
        if (*cursor)
            *cursor++ = '\0';
        return token;
    }

    // Scan code of a key name ("Up_Arrow") or number ("0xC8"); -1 if unknown.
    int KeyCode(CKInputManager *input, const char *name)
    {
        if (isdigit((unsigned char)name[0]))
        {
            const long code = strtol(name, NULL, 0);
            return code > 0 && code < 256 ? (int)code : -1;
        }
        char spaced[32];
        strncpy(spaced, name, sizeof(spaced) - 1);
        spaced[sizeof(spaced) - 1] = '\0';
        for (char *c = spaced; *c; ++c)
        {
            if (*c == '_')
                *c = ' ';
        }
        const CKDWORD key = input ? input->GetKeyFromName(spaced) : 256;
        return key < 256 ? (int)key : -1;
    }

    // 24-bit uncompressed TGA from a 32-bit ARGB picture.
    bool SaveTga(const char *path, const VxImageDescEx &image)
    {
        FILE *file = fopen(path, "wb");
        if (!file)
            return false;
        unsigned char header[18] = {0};
        header[2] = 2; // Uncompressed true colour
        header[12] = (unsigned char)image.Width;
        header[13] = (unsigned char)(image.Width >> 8);
        header[14] = (unsigned char)image.Height;
        header[15] = (unsigned char)(image.Height >> 8);
        header[16] = 24;
        header[17] = 0x20; // Top row first
        bool ok = fwrite(header, sizeof(header), 1, file) == 1;
        XArray<unsigned char> row;
        row.Resize(image.Width * 3);
        for (int y = 0; ok && y < image.Height; ++y)
        {
            const CKDWORD *pixels = (const CKDWORD *)(image.Image + y * image.BytesPerLine);
            for (int x = 0; x < image.Width; ++x)
            {
                row[x * 3 + 0] = (unsigned char)pixels[x];
                row[x * 3 + 1] = (unsigned char)(pixels[x] >> 8);
                row[x * 3 + 2] = (unsigned char)(pixels[x] >> 16);
            }
            ok = fwrite(row.Begin(), row.Size(), 1, file) == 1;
        }
        return fclose(file) == 0 && ok;
    }

    double Milliseconds(unsigned long long after, unsigned long long before)
    {
        return (double)(after - before) / 1000.0;
    }

    // Collision surfaces made since a report started, and where they came from.
    void LogPhysicsShapes(const wiiphysicsshapes::Stats &before)
    {
        wiiphysicsshapes::Stats now;
        wiiphysicsshapes::GetStats(now);
        CLogger::Get().Info("Physics shapes: %u built (%.1f ms), %u from memory (%.1f ms), %u from the cache files "
                            "(%.1f ms), %u built rather than read, %u rejected, %u written, %u verified (%u differ); "
                            "%u KB in use, %u KB kept",
                            now.Built - before.Built, Milliseconds(now.BuildMicroseconds, before.BuildMicroseconds),
                            now.FromMemory - before.FromMemory,
                            Milliseconds(now.MemoryMicroseconds, before.MemoryMicroseconds),
                            now.FromStorage - before.FromStorage,
                            Milliseconds(now.StorageMicroseconds, before.StorageMicroseconds),
                            now.Skipped - before.Skipped, now.Rejected - before.Rejected,
                            now.Written - before.Written,
                            now.Verified - before.Verified, now.Mismatched - before.Mismatched,
                            now.BytesInUse >> 10, now.BytesUnused >> 10);
    }
}

AutoplayScript::AutoplayScript() : m_Next(0), m_Remaining(-1)
{
    memset(&m_Shapes, 0, sizeof(m_Shapes));
}

bool AutoplayScript::Load(const char *path, CKContext *context)
{
    m_Commands.Clear();
    m_Next = 0;
    m_Remaining = -1;
    m_Folder = utils::GetFileDirectory(path, true);

    FILE *file = fopen(path, "r");
    if (!file)
    {
        CLogger::Get().Error("Autoplay: cannot open %s", path);
        return false;
    }
    CKInputManager *input = (CKInputManager *)context->GetManagerByGuid(INPUT_MANAGER_GUID);
    char line[256];
    int lineNumber = 0;
    bool ok = true;
    while (fgets(line, sizeof(line), file))
    {
        ++lineNumber;
        char *comment = strchr(line, '#');
        if (comment)
            *comment = '\0';
        char *cursor = line;
        const char *word = NextToken(cursor);
        if (!word)
            continue;

        Command command;
        command.Frames = 0;
        command.Line = lineNumber;
        memset(command.Keys, 0, sizeof(command.Keys));
        const char *argument = NextToken(cursor);
        bool valid = true;
        if (strcasecmp(word, "wait") == 0 || strcasecmp(word, "hold") == 0)
        {
            command.Kind = strcasecmp(word, "wait") == 0 ? WAIT : HOLD;
            command.Frames = argument ? atoi(argument) : 0;
            valid = command.Frames > 0;
            for (const char *key = NextToken(cursor); valid && key; key = NextToken(cursor))
            {
                const int code = KeyCode(input, key);
                valid = command.Kind == HOLD && code > 0;
                if (valid)
                    command.Keys[code] = 1;
            }
        }
        else if (strcasecmp(word, "waitplay") == 0)
        {
            command.Kind = WAIT_PLAY;
            command.Frames = argument ? atoi(argument) : 60 * 60;
        }
        else if (strcasecmp(word, "report") == 0 && argument)
        {
            command.Kind = strcasecmp(argument, "start") == 0 ? REPORT_START : REPORT_STOP;
            valid = command.Kind == REPORT_START || strcasecmp(argument, "stop") == 0;
        }
        else if (strcasecmp(word, "profile") == 0 && argument && strcasecmp(argument, "start") == 0)
        {
            command.Kind = PROFILE_START;
            const char *interval = NextToken(cursor);
            command.Frames = interval ? atoi(interval) : 1000;
            valid = command.Frames > 0;
        }
        else if (strcasecmp(word, "profile") == 0 && argument && strcasecmp(argument, "stop") == 0)
        {
            command.Kind = PROFILE_STOP;
            const char *name = NextToken(cursor);
            valid = name != NULL;
            command.Text = name ? name : "";
        }
        else if (strcasecmp(word, "shot") == 0 && argument)
        {
            command.Kind = SHOT;
            command.Text = argument;
        }
        else if (strcasecmp(word, "log") == 0)
        {
            command.Kind = LOG;
            XString text = argument ? argument : "";
            if (*cursor)
                text << " " << cursor;
            text.Trim();
            command.Text = text;
        }
        else if (strcasecmp(word, "quit") == 0)
        {
            command.Kind = QUIT;
        }
        else
        {
            valid = false;
        }
        if (!valid)
        {
            CLogger::Get().Error("Autoplay: %s line %d is not a command", path, lineNumber);
            ok = false;
            continue;
        }
        m_Commands.PushBack(command);
    }
    fclose(file);
    if (!ok)
        m_Commands.Clear();
    else
        CLogger::Get().Info("Autoplay: %d commands from %s", m_Commands.Size(), path);
    return ok;
}

void AutoplayScript::Update(WiiGamePlayer &player)
{
    while (IsActive())
    {
        const Command &command = m_Commands[m_Next];
        if (command.Kind == WAIT || command.Kind == HOLD || command.Kind == WAIT_PLAY)
        {
            if (m_Remaining < 0)
                m_Remaining = command.Frames;
            const bool waiting = command.Kind == WAIT_PLAY ? !wiiautoplay::IsGamePlaying() : true;
            if (waiting && m_Remaining > 0)
            {
                --m_Remaining;
                wiiautoplay::SetKeys(command.Kind == HOLD ? command.Keys : NULL);
                return;
            }
            if (command.Kind == WAIT_PLAY && waiting)
                CLogger::Get().Warn("Autoplay: line %d: the game did not start playing", command.Line);
            m_Remaining = -1;
            ++m_Next;
            continue;
        }
        Execute(command, player);
        ++m_Next;
    }
    wiiautoplay::SetKeys(NULL);
}

void AutoplayScript::Execute(const Command &command, WiiGamePlayer &player)
{
    switch (command.Kind)
    {
    case REPORT_START:
        player.StartFrameReport();
        wiiphysicsshapes::GetStats(m_Shapes);
        break;
    case REPORT_STOP:
        player.LogFrameReport();
        LogPhysicsShapes(m_Shapes);
        break;
    case PROFILE_START:
        if (!wiiprofiler::Start((u32)command.Frames, 40000))
            CLogger::Get().Error("Autoplay: the profiler could not start");
        break;
    case PROFILE_STOP:
    {
        const u32 samples = wiiprofiler::GetSampleCount();
        const XString path = utils::JoinPath(m_Folder.CStr(), command.Text.CStr());
        if (wiiprofiler::Write(path.CStr()))
            CLogger::Get().Info("Autoplay: %u samples written to %s", (unsigned)samples, path.CStr());
        else
            CLogger::Get().Error("Autoplay: cannot write %s", path.CStr());
        break;
    }
    case SHOT:
    {
        CKRenderContext *context = player.GetRenderContext();
        VxImageDescEx image;
        image.Image = NULL;
        const int size = context ? context->DumpToMemory(NULL, VXBUFFER_BACKBUFFER, image) : 0;
        const XString path = utils::JoinPath(m_Folder.CStr(), command.Text.CStr());
        bool ok = false;
        if (size > 0)
        {
            XArray<CKBYTE> pixels;
            pixels.Resize(size);
            image.Image = pixels.Begin();
            ok = context->DumpToMemory(NULL, VXBUFFER_BACKBUFFER, image) > 0 && SaveTga(path.CStr(), image);
        }
        if (!ok)
            CLogger::Get().Error("Autoplay: cannot save %s", path.CStr());
        break;
    }
    case LOG:
        CLogger::Get().Info("Autoplay: %s", command.Text.CStr());
        break;
    case QUIT:
        CLogger::Get().Info("Autoplay: quitting");
        wiisystem::RequestQuit(wiisystem::QUIT_TO_LOADER);
        break;
    default:
        break;
    }
}
