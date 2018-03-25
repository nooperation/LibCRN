#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>

#include "LibCrn.h"

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
  _Out_opt_ std::size_t* outBuffSize,
  _Out_opt_ ImageProperties *outImageProperties)
{

  *outBuffSize = 0;
  outImageProperties->height = 0;
  outImageProperties->width = 0;

  return false;
}
