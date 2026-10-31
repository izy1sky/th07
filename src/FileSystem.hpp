#pragma once

#include <string>

#include "ZunBool.hpp"
#include "inttypes.hpp"

extern u32 g_LastFileSize;

namespace FileSystem
{
ZunBool CheckFileExists(const char *file);
u8 *OpenFile(const char *filepath, ZunBool isExternalResource);
i32 WriteDataToFile(const char *filename, const void *out, u32 bytesToWrite);
std::string GetBasePath(const char *filename);
std::string GetPrefPath(const char *filename);
} // namespace FileSystem
