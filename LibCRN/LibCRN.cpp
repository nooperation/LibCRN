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

bool convert_file(unsigned const char *in_buff, const std::size_t in_buff_size, unsigned char **out_buff, std::size_t &out_buff_size, texture_file_types::format out_file_type);

enum class MemoryType {
  kMemoryType_Single,
  kMemoryType_Array
};

std::mutex memoryMapMutex;
std::unordered_map<const void *, MemoryType> memoryMap;
static thread_local std::string _errorMessage = "";

void FreeMemory(_In_ const unsigned char *data)
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
  _In_ const unsigned char *inCrnBytes,
  _In_ std::size_t inCrnBytesSize,
  _In_ ConversionOptions options,
  _Out_opt_ unsigned char **outBuff,
  _Out_opt_ std::size_t* outBuffSize)
{
  console::disable_output();

  auto status = convert_file(inCrnBytes, inCrnBytesSize, outBuff, *outBuffSize, static_cast<texture_file_types::format>(options.conversionType));

  return status;
}


bool convert_file(unsigned const char *in_buff, const std::size_t in_buff_size, unsigned char **out_buff, std::size_t &out_buff_size, texture_file_types::format out_file_type)
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

  return success;
}
