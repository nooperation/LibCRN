#pragma once

#include <memory>

#define DllExport   __declspec( dllexport ) 

struct ConversionOptions
{
};

struct ImageProperties
{
  uint32_t width;
  uint32_t height;
};

extern "C"
{
  DllExport bool ConvertCrnInMemory(
    _In_ const unsigned char *inCrnBytes,
    _In_ std::size_t inCrnBytesSize,
    _In_ ConversionOptions options,
    _Out_opt_ unsigned char **outBuff,
    _Out_opt_ std::size_t *outBuffSize,
    _Out_opt_ ImageProperties *outImageProperties
  );

  DllExport const char *GetError();
  DllExport void FreeMemory(_In_ const unsigned char *data);
}
