#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>

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

bool convert_file_segmented(
  const uint8_t *in_buff,
  std::size_t in_buff_size,
  texture_file_types::format out_file_type,
  std::size_t num_level_segments,
  const uint8_t *level_segment_bytes,
  const std::size_t level_segment_bytes_size,
  uint8_t **out_buff,
  std::size_t &out_buff_size
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
  _In_ const uint8_t *in_buff,
  _In_ const std::size_t in_buff_size,
  _In_ const ConversionOptions options,
  _In_ const std::size_t num_level_segments,
  _In_opt_ const uint8_t *level_segment_bytes,
  _In_opt_  const std::size_t level_segment_bytes_size,
  _Out_opt_ uint8_t **out_buff,
  _Out_opt_ std::size_t* out_buff_size
)
{
  console::disable_output();

  auto crn_header = crnd::crnd_get_header(in_buff, in_buff_size);
  auto format = static_cast<texture_file_types::format>(options.conversionType);

  if (crn_header->m_flags & crnd::cCRNHeaderFlagSegmented)
  {
    return convert_file_segmented(
      in_buff,
      in_buff_size,
      format,
      num_level_segments,
      level_segment_bytes,
      level_segment_bytes_size,
      out_buff,
      *out_buff_size
    );
  }
  else
  {
    return convert_file(
      in_buff,
      in_buff_size,
      out_buff,
      *out_buff_size,
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

bool convert_file_segmented(
  const uint8_t *in_buff,
  std::size_t in_buff_size,
  texture_file_types::format out_file_type,
  std::size_t num_level_segments,
  const uint8_t *level_segment_bytes,
  const std::size_t level_segment_bytes_size,
  uint8_t **out_buff,
  std::size_t &out_buff_size
)
{
  auto crn_header = crnd::crnd_get_header(in_buff, in_buff_size);
  auto header_size = static_cast<uint>(crn_header->m_data_size);
  auto level_to_export = num_level_segments;


  if (num_level_segments >= static_cast<uint>(crn_header->m_levels))
  {
    printf("All mip levels are segments. This cannot be recovered.\n");
    return false;
  }

  // Determine the real size of the crn, including space for missing levels
  auto total_size = in_buff_size;
  auto last_offset = static_cast<uint>(crn_header->m_level_ofs[0]);
  std::vector<std::size_t> segment_sizes;
  for (std::size_t level = 1; level < (num_level_segments + 1); ++level)
  {
    auto previous_level_size = static_cast<uint>(crn_header->m_level_ofs[level]) - last_offset;
    segment_sizes.push_back(previous_level_size);
    total_size += previous_level_size;
    last_offset = static_cast<uint>(crn_header->m_level_ofs[level]);
  }

  // Add zero-filled space where missing segments should exist
  auto complete_crn_bytes = std::make_unique<uint8_t[]>(total_size);
  auto complete_crn_bytes_ptr = complete_crn_bytes.get();

  memset(complete_crn_bytes_ptr, 0, total_size);
  memcpy(complete_crn_bytes_ptr, in_buff, header_size);
  complete_crn_bytes_ptr += static_cast<uint>(crn_header->m_level_ofs[num_level_segments]);
  memcpy(complete_crn_bytes_ptr, in_buff + header_size, in_buff_size - header_size);

  // Replace zeroed out levels with user supplied level segment bytes
  if (level_segment_bytes != nullptr)
  {
    auto level_offset = static_cast<uint>(crn_header->m_level_ofs[0]);
    memcpy(&complete_crn_bytes[level_offset], level_segment_bytes, level_segment_bytes_size);
    level_to_export = 0;
  }

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
    level_to_export
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
#include <fstream>


int main()
{
  static const int num_segments = 5;
  static const std::string kInputFile = "a.crn";
  static const std::vector<std::string> kLevelSegmentNames = {
    "a1.crn",
    "a2.crn",
    "a3.crn",
    "a4.crn",
    "a5.crn",
  };

  std::size_t level_segment_bytes_length = 0;
  for (const auto &segment_file : kLevelSegmentNames)
  {
    if (!std::experimental::filesystem::exists(segment_file))
    {
      printf("Missing segment file %s\n", segment_file.c_str());
      return 1;
    }

    std::experimental::filesystem::path segment_path(segment_file);
    level_segment_bytes_length += std::experimental::filesystem::file_size(segment_path) - 4;
  }

  std::vector<uint8_t> level_segment_bytes(level_segment_bytes_length);
  auto level_segment_bytes_ptr = &level_segment_bytes[0];

  for (const auto &segment_file: kLevelSegmentNames)
  {
    if (!std::experimental::filesystem::exists(segment_file))
    {
      printf("Missing segment file %s\n", segment_file.c_str());
      return 1;
    }

    std::experimental::filesystem::path segment_path(segment_file);
    const auto segment_size = std::experimental::filesystem::file_size(segment_path) - 4;

    std::ifstream in_file(segment_path.string().c_str(), std::ios::beg | std::ios::binary);
    in_file.seekg(4); // segments have 4 byte length header
    in_file.read(reinterpret_cast<char *>(level_segment_bytes_ptr), segment_size);
    in_file.close();

    level_segment_bytes_ptr += segment_size;
  }

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
  conversion_options.conversionType = crnlib::texture_file_types::cFormatDDS;

  ConvertCrnInMemory(
    crn_bytes_partial,
    file_size,
    conversion_options,
    num_segments,
    &level_segment_bytes[0],
    level_segment_bytes.size(),
    &out_buff,
    &out_buff_size
  );

  std::ofstream out_file_png("out222.dds", std::ios::binary);
  out_file_png.write(reinterpret_cast<char *>(out_buff), out_buff_size);
  out_file_png.close();

  FreeMemory(out_buff);

  return 0;
}

#endif
