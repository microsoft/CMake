/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebuggerThread.h"

namespace cmDebugger {

cmDebuggerThread::cmDebuggerThread(int64_t id, std::string name)
  : Id(id)
  , Name(name)
{
}

void cmDebuggerThread::PushStackFrame(const cmDebuggerStackFrame& frame)
{
  Frames.push_back(frame);
}

void cmDebuggerThread::PopStackFrame()
{
  Frames.pop_back();
}

const cmDebuggerStackFrame* cmDebuggerThread::GetTopStackFrame() const
{
  if (!Frames.empty()) {
    return &Frames.back();
  }

  return nullptr;
}

dap::StackTraceResponse GetStackTraceResponse(
  std::shared_ptr<cmDebuggerThread> thread)
{
  dap::StackTraceResponse response;
  for (int i = thread->Frames.size() - 1; i >= 0; --i) {
    if (thread->Frames[i].Line == 0) {
      // Ignore file loading frames.
      continue;
    }

    dap::Source source;
    source.name = thread->Frames[i].FileName;
    source.path = source.name;

    dap::StackFrame stackFrame;
    stackFrame.line = thread->Frames[i].Line;
    stackFrame.column = 1;
    stackFrame.name = thread->Frames[i].FileName +
      " Line " +
      std::to_string(stackFrame.line);
    stackFrame.id = thread->Frames[i].GetId();
    stackFrame.source = source;

    response.stackFrames.push_back(stackFrame);
  }

  response.totalFrames = response.stackFrames.size();
  return response;
}

} // namespace cmDebugger
