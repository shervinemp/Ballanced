/**
 * @file VxMemoryMappedFileOgc.cpp
 * @brief VxMemoryMappedFile for the Nintendo Wii.
 *
 * There is no memory mapping on SD/USB storage, so the file is read into a
 * heap buffer. Reads go through stdio in large blocks, which is what libfat
 * handles best.
 */

#include "VxMemoryMappedFile.h"

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

    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0)
        size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        m_errCode = VxMMF_FileMapping;
        return;
    }

    m_cbFile = (size_t)size;
    m_pMemoryMappedFileBase = malloc(m_cbFile);
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
