#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <filesystem>

#include "LibCRN.h"

#include "crn_core.h"
#include "crn_defs.h"
#include "crn_console.h"
#include "crn_texture_conversion.h"

// crn_decomp.h is a bullshit file written in a bullshit manner.
namespace crnd {
  const crn_header* crnd_get_header(const void* pData, uint32 data_size);
}

using namespace crnlib;

bool convert_file(
  const uint8_t *in_buff,
  const std::size_t in_buff_size,
  uint8_t **out_buff,
  std::size_t &out_buff_size,
  texture_file_types::format out_file_type
);

bool convert_file_segmented(const uint8_t *in_buff,
  const std::size_t in_buff_size,
  uint8_t **out_buff,
  std::size_t &out_buff_size,
  texture_file_types::format out_file_type,
  uint8_t **segments,
  std::size_t num_segments
);

enum class MemoryType {
  kMemoryType_Single,
  kMemoryType_Array
};

std::mutex memoryMapMutex;
std::unordered_map<const uint8_t*, MemoryType> memoryMap;
static thread_local std::string _errorMessage;

void FreeMemory(_In_ const uint8_t *data)
{
  std::lock_guard<std::mutex> lock(memoryMapMutex);

  auto memoryIter = memoryMap.find(data);
  if (memoryIter != memoryMap.end())
  {
    if (memoryIter->second == MemoryType::kMemoryType_Array)
    {
      delete[] memoryIter->first;
    }
    else
    {
      delete memoryIter->first;
    }
    memoryMap.erase(memoryIter);
  }
}

void SetError(const std::string& errorMessage)
{
  _errorMessage = errorMessage;
}

const char *GetError()
{
  return _errorMessage.c_str();
}

bool ConvertCrnInMemory(
  _In_ const uint8_t *inCrnBytes,
  _In_ std::size_t inCrnBytesSize,
  _In_ ConversionOptions options,
  _In_opt_ uint8_t **levels,
  _In_ std::size_t numLevels,
  _Out_opt_ uint8_t **outBuff,
  _Out_opt_ std::size_t* outBuffSize
)
{
  console::disable_output();

  auto format = static_cast<texture_file_types::format>(options.conversionType);

  if (numLevels > 0)
  {
    return convert_file_segmented(
      inCrnBytes,
      inCrnBytesSize,
      outBuff,
      *outBuffSize,
      format,
      levels,
      numLevels
    );
  }
  else
  {
    return convert_file(
      inCrnBytes,
      inCrnBytesSize,
      outBuff,
      *outBuffSize,
      format
    );
  }
}

bool convert_file(const uint8_t *in_buff, const std::size_t in_buff_size, uint8_t **out_buff, std::size_t &out_buff_size,  texture_file_types::format out_file_type)
{
  mipmapped_texture src_tex;

  if (!src_tex.read_crn_from_memory(in_buff, in_buff_size, "Butts.crn"))
  {
    return false;
  }

  texture_conversion::convert_params params;
  params.m_texture_type = src_tex.determine_texture_type();
  params.m_pInput_texture = &src_tex;
  params.m_dst_file_type = out_file_type;
  params.m_lzma_stats = false;
  params.m_write_mipmaps_to_multiple_files = false;
  params.m_always_use_source_pixel_format = false;
  params.m_y_flip = false;
  params.m_unflip = false;
  params.m_no_stats = true;
  params.m_dst_format = PIXEL_FMT_INVALID;
  params.m_mipmap_params.m_mode = cCRNMipModeUseSourceMips;
  params.m_quick = false;
  if (params.m_texture_type == cTextureTypeNormalMap)
  {
    params.m_comp_params.set_flag(cCRNCompFlagPerceptual, false);
  }

  params.m_dst_filename = dynamic_string();
  params.m_out_buff = nullptr;
  params.m_out_buff_size = 0;

  texture_conversion::convert_stats stats;
  auto success = texture_conversion::process(params, stats);

  *out_buff = params.m_out_buff;
  out_buff_size = params.m_out_buff_size;

  {
    std::lock_guard<std::mutex> lock(memoryMapMutex);
    memoryMap[*out_buff] = MemoryType::kMemoryType_Array;
  }

  return success;
}

bool convert_file_segmented(const uint8_t *in_buff, const std::size_t in_buff_size, uint8_t **out_buff, std::size_t &out_buff_size, texture_file_types::format out_file_type, uint8_t **segments, std::size_t num_segments)
{
  auto crn_header = crnd::crnd_get_header(in_buff, in_buff_size);
  auto header_size = static_cast<uint>(crn_header->m_data_size);

  if ((crn_header->m_flags & crnd::cCRNHeaderFlagSegmented) == 0)
  {
    printf("CRN Is not a segmented file, but a number of segments was specified.\n");
    return false;
  }

  if (num_segments >= static_cast<uint>(crn_header->m_levels))
  {
    printf("All mip levels are segments. This cannot be recovered.\n");
    return false;
  }

  // Determine the real size of the crn, including space for missing levels
  auto total_size = in_buff_size;
  auto last_offset = static_cast<uint>(crn_header->m_level_ofs[0]);
  for (uint32_t i = 1; i < (num_segments + 1); ++i)
  {
    auto previous_level_size = static_cast<uint>(crn_header->m_level_ofs[i]) - last_offset;
    total_size += previous_level_size;
    last_offset = static_cast<uint>(crn_header->m_level_ofs[i]);
  }

  // Add zero-filled space where missing segments should exist
  auto complete_crn_bytes = std::make_unique<uint8_t[]>(total_size);
  auto complete_crn_bytes_ptr = complete_crn_bytes.get();

  memset(complete_crn_bytes_ptr, 0, total_size);
  memcpy(complete_crn_bytes_ptr, in_buff, header_size);
  complete_crn_bytes_ptr += static_cast<uint>(crn_header->m_level_ofs[num_segments]);
  memcpy(complete_crn_bytes_ptr, in_buff + header_size, in_buff_size - header_size);

  // Read the new crn, which contains zero fill for missing levels
  mipmapped_texture tex;
  if (!tex.read_crn_from_memory(complete_crn_bytes.get(), total_size, "butts.crn"))
  {
    printf("Failed to read completed crn\n");
    return false;
  }

  auto is_successful = tex.write_to_memory(
    out_buff,
    out_buff_size, 
    out_file_type,
    nullptr,
    nullptr,
    nullptr,
    0,
    num_segments
  );

  if (!is_successful) {
    return false;
  }
  
  {
    std::lock_guard<std::mutex> lock(memoryMapMutex);
    memoryMap[*out_buff] = MemoryType::kMemoryType_Array;
  }

  return true;
}

#ifdef CONSOLE_DEBUG
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstdint>

int main()
{
  static const int num_segments = 5;
  static const std::string kInputFile = "a.crn";

  if (!std::experimental::filesystem::exists(kInputFile))
  {
    printf("Missing file\n");
    return 1;
  }

  std::experimental::filesystem::path crn_path(kInputFile);
  const auto file_size = std::experimental::filesystem::file_size(crn_path);

  auto *crn_bytes_partial = new uint8_t[file_size];
  std::ifstream in_file(crn_path.string().c_str(), std::ios::beg | std::ios::binary);
  in_file.read(reinterpret_cast<char *>(crn_bytes_partial), file_size);
  in_file.close();

  uint8_t *out_buff;
  std::size_t out_buff_size;

  ConversionOptions conversion_options;
  conversion_options.conversionType = crnlib::texture_file_types::cFormatPNG;

  ConvertCrnInMemory(
    crn_bytes_partial,
    file_size,
    conversion_options,
    nullptr,
    num_segments,
    &out_buff,
    &out_buff_size
  );

  std::ofstream out_file_png("out22.png", std::ios::binary);
  out_file_png.write(reinterpret_cast<char *>(out_buff), out_buff_size);
  out_file_png.close();

  FreeMemory(out_buff);

  return 0;
}

#endif
