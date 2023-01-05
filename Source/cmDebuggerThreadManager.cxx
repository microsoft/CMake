/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebuggerThreadManager.h"

namespace cmDebugger {

std::atomic<int64_t> cmDebuggerThreadManager::NextThreadId = 1;

cmDebuggerThreadManager::cmDebuggerThreadManager()
{
}

std::shared_ptr<cmDebuggerThread> cmDebuggerThreadManager::StartThread(
  const std::string& name)
{
  std::shared_ptr<cmDebuggerThread> thread = std::make_shared<cmDebuggerThread>(
    cmDebuggerThreadManager::NextThreadId.fetch_add(1), name);
  Threads.emplace_back(thread);
  return thread;
}

void cmDebuggerThreadManager::EndThread(std::shared_ptr<cmDebuggerThread> thread)
{
  Threads.remove(thread);
}

cm::optional<dap::StackTraceResponse>
cmDebuggerThreadManager::GetThreadStackTraceResponse(int64_t id)
{
  auto it = find_if(Threads.begin(), Threads.end(),
                    [&](const auto& t) { return t->GetId() == id; });

  if (it == Threads.end()) {
    return {};
  }

  return GetStackTraceResponse(*it);
}

} // namespace cmDebugger
