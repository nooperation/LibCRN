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
    _In_ const uint8_t *in_buff,
    _In_ const std::size_t in_buff_size,
    _In_ const ConversionOptions options,
    _In_ const std::size_t num_level_segments,
    _In_opt_ const uint8_t *level_segment_bytes,
    _In_opt_  const std::size_t level_segment_bytes_size,
    _Out_opt_ uint8_t **out_buff,
    _Out_opt_ std::size_t* out_buff_size
  );

  DllExport const char *GetError();
  DllExport void FreeMemory(_In_ const unsigned char *data);
}
