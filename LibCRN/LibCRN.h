#pragma once

#include <memory>

#define DllExport   __declspec( dllexport ) 

struct ConversionOptions
{
  int32_t conversionType;
};

extern "C"
{
  DllExport bool ConvertCrnInMemory(
    _In_ const unsigned char *inCrnBytes,
    _In_ std::size_t inCrnBytesSize,
    _In_ ConversionOptions options,
    _Out_opt_ unsigned char **outBuff,
    _Out_opt_ std::size_t *outBuffSize
  );

  DllExport const char *GetError();
  DllExport void FreeMemory(_In_ const unsigned char *data);
}
