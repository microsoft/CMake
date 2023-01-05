/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <list>
#include <memory>
#include <string>

#include <cm/optional>

#include "cmDebuggerThread.h"

namespace cmDebugger {

class cmDebuggerThreadManager
{
  static std::atomic<int64_t> NextThreadId;
  std::list<std::shared_ptr<cmDebuggerThread>> Threads;

public:
  cmDebuggerThreadManager();
  std::shared_ptr<cmDebuggerThread> StartThread(const std::string& name);
  void EndThread(std::shared_ptr<cmDebuggerThread> thread);
  cm::optional<dap::StackTraceResponse> GetThreadStackTraceResponse(
    int64_t id);
};

} // namespace cmDebugger
