/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <atomic>
#include <string>

namespace cmDebugger {

class cmDebuggerStackFrame
{
  static std::atomic<int64_t> NextId;
  int64_t Id;

public:
  std::string FileName;
  std::string FunctionName;
  int64_t Line;

  cmDebuggerStackFrame(std::string fileName);
  cmDebuggerStackFrame(std::string fileName, std::string functionName,
                       int64_t line);
  int64_t GetId() const noexcept { return this->Id; }
};

} // namespace cmDebugger
