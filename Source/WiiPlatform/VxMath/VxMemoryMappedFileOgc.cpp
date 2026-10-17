/**
 * @file VxMemoryMappedFileOgc.cpp
 * @brief VxMemoryMappedFile for the Nintendo Wii.
 *
 * There is no memory mapping on SD/USB storage, so the file is read into a
 * heap buffer: in one unbuffered read, into memory aligned to 32 bytes, so
 * libfat can have the device driver transfer whole runs of sectors straight
 * into it. libogc's SD card driver reads into an unaligned buffer through a
 * bounce buffer, 8 sectors (4 KB) a request.
 */

#include "VxMemoryMappedFile.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

VxMemoryMappedFile::VxMemoryMappedFile(const char *pszFileName) {
    m_hFile = NULL;
    m_hFileMapping = NULL;
    m_pMemoryMappedFileBase = NULL;
    m_cbFile = 0;
    m_errCode = VxMMF_NoError;

    if (!pszFileName) {
        m_errCode = VxMMF_FileOpen;
        return;
    }

    FILE *file = fopen(pszFileName, "rb");
    if (!file) {
        m_errCode = VxMMF_FileOpen;
        return;
    }
    setvbuf(file, NULL, _IONBF, 0);

    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0)
        size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        m_errCode = VxMMF_FileMapping;
        return;
    }

    m_cbFile = (size_t)size;
    m_pMemoryMappedFileBase = memalign(32, m_cbFile);
    if (!m_pMemoryMappedFileBase) {
        fclose(file);
        m_cbFile = 0;
        m_errCode = VxMMF_MapView;
        return;
    }

    const size_t bytesRead = fread(m_pMemoryMappedFileBase, 1, m_cbFile, file);
    fclose(file);
    if (bytesRead != m_cbFile) {
        free(m_pMemoryMappedFileBase);
        m_pMemoryMappedFileBase = NULL;
        m_cbFile = 0;
        m_errCode = VxMMF_MapView;
        return;
    }

    // Handles are unused; non-null values mark the mapping as open.
    m_hFile = (GENERIC_HANDLE)(intptr_t)1;
    m_hFileMapping = (GENERIC_HANDLE)(intptr_t)1;
}

VxMemoryMappedFile::~VxMemoryMappedFile() {
    free(m_pMemoryMappedFileBase);
    m_pMemoryMappedFileBase = NULL;
    m_hFile = NULL;
    m_hFileMapping = NULL;
    m_cbFile = 0;
}

void *VxMemoryMappedFile::GetBase() {
    return m_pMemoryMappedFileBase;
}

size_t VxMemoryMappedFile::GetFileSize() {
    return m_cbFile;
}

XBOOL VxMemoryMappedFile::IsValid() {
    return m_pMemoryMappedFileBase != NULL;
}

VxMMF_Error VxMemoryMappedFile::GetErrorType() {
    return m_errCode;
}
