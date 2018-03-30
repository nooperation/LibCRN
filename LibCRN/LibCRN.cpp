#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <filesystem>

#include "LibCRN.h"

#include "crn_core.h"
#include "crn_console.h"
#include "crn_texture_conversion.h"

using namespace crnlib;

bool convert_file(const uint8_t *in_buff, const std::size_t in_buff_size, uint8_t **out_buff, std::size_t &out_buff_size, texture_file_types::format out_file_type);

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
  _Out_opt_ uint8_t **outBuff,
  _Out_opt_ std::size_t* outBuffSize)
{
  console::disable_output();

  auto status = convert_file(inCrnBytes, inCrnBytesSize, outBuff, *outBuffSize, static_cast<texture_file_types::format>(options.conversionType));

  return status;
}


bool convert_file(const uint8_t *in_buff, const std::size_t in_buff_size, uint8_t **out_buff, std::size_t &out_buff_size, texture_file_types::format out_file_type)
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

#ifdef CONSOLE_DEBUG
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstdint>

int main()
{
  static const std::string kInputFile = "binary2.crn";

  if (!std::experimental::filesystem::exists(kInputFile))
  {
    printf("Missing file\n");
    return 1;
  }

  std::experimental::filesystem::path crn_path(kInputFile);
  const auto file_size = std::experimental::filesystem::file_size(crn_path);

  auto *crn_bytes = new uint8_t[file_size];
  std::ifstream in_file(crn_path.string().c_str(), std::ios::beg | std::ios::binary);
  in_file.read(reinterpret_cast<char *>(crn_bytes), file_size);
  in_file.close();

  uint8_t *out_buff;
  std::size_t out_buff_size;
  convert_file(crn_bytes, file_size, &out_buff, out_buff_size, texture_file_types::format::cFormatJPEG);
  delete[] crn_bytes;

  std::ofstream out_file("out.jpg", std::ios::binary);
  out_file.write(reinterpret_cast<char *>(out_buff), out_buff_size);
  out_file.close();

  FreeMemory(out_buff);

  return 0;
}
#endif