/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <memory>
#include <string>
#include <vector>

#include <cm3p/cppdap/protocol.h>

#include "cmDebuggerStackFrame.h"

namespace cmDebugger {

class cmDebuggerThread
{
  int64_t Id;
  std::string Name;
  std::vector<cmDebuggerStackFrame> Frames;

public:
  cmDebuggerThread(int64_t id, std::string name);
  int64_t GetId() const { return this->Id; }
  const std::string& GetName() const { return this->Name; }
  void PushStackFrame(const cmDebuggerStackFrame& frame);
  void PopStackFrame();
  const cmDebuggerStackFrame* GetTopStackFrame() const;
  size_t GetStackFrameSize() const { return this->Frames.size(); }
  friend dap::StackTraceResponse GetStackTraceResponse(
    std::shared_ptr<cmDebuggerThread> thread);
};

} // namespace cmDebugger
