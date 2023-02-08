/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include "cmDebuggerStackFrame.h"
#include "cmDebuggerThread.h"
#include "cmDebuggerVariables.h"
#include "cmMakefile.h"
#include "cmStateSnapshot.h"

namespace cmDebugger {

cmDebuggerThread::cmDebuggerThread(int64_t id, std::string const& name)
  : Id(id)
  , Name(name)
{
}

void cmDebuggerThread::PushStackFrame(cmMakefile* mf,
                                      std::string const& sourcePath,
                                      cmListFileFunction const& lff)
{
  std::unique_lock<std::mutex> lock(Mutex);
  Frames.emplace_back(
    std::make_shared<cmDebuggerStackFrame>(mf, sourcePath, lff));
  FrameMap.insert({ Frames.back()->GetId(), Frames.back() });
}

void cmDebuggerThread::PopStackFrame()
{
  std::unique_lock<std::mutex> lock(Mutex);
  FrameMap.erase(Frames.back()->GetId());
  FrameScopes.erase(Frames.back()->GetId());
  for (auto id : FrameVariables[Frames.back()->GetId()]) {
    Variables.erase(id);
  }
  FrameVariables.erase(Frames.back()->GetId());
  Frames.pop_back();
}

std::shared_ptr<cmDebuggerStackFrame> cmDebuggerThread::GetTopStackFrame()
{
  std::unique_lock<std::mutex> lock(Mutex);
  if (!Frames.empty()) {
    return Frames.back();
  }

  return {};
}

std::shared_ptr<cmDebuggerStackFrame> cmDebuggerThread::GetStackFrame(int64_t frameId)
{
  std::unique_lock<std::mutex> lock(Mutex);
  auto it = FrameMap.find(frameId);

  if (it == FrameMap.end()) {
    return {};
  }

  return it->second;
}

dap::ScopesResponse cmDebuggerThread::GetScopesResponse(
  int64_t frameId, bool supportsVariableType)
{
  std::unique_lock<std::mutex> lock(Mutex);
  auto it = FrameScopes.find(frameId);

  if (it != FrameScopes.end()) {
    dap::ScopesResponse response;
    response.scopes = it->second;
    return response;
  }

  auto it2 = FrameMap.find(frameId);
  if (it2 == FrameMap.end()) {
    return dap::ScopesResponse();
  }

  std::shared_ptr<cmDebuggerStackFrame> frame = it2->second;
  auto cacheVariables = std::make_shared<cmDebuggerVariablesCache>(
    supportsVariableType,
    [=]() { return frame->GetMakefile()->GetStateSnapshot().ClosureKeys(); },
    [=](std::string const& key) {
      return frame->GetMakefile()->GetStateSnapshot().GetDefinition(key);
    });
  Variables.insert({ cacheVariables->GetId(), cacheVariables });
  FrameVariables[frameId].push_back(cacheVariables->GetId());

  auto localVariables = std::make_shared<cmDebuggerVariablesLocal>(
    supportsVariableType, [=]() { return frame->GetLine(); },
    cacheVariables->GetId());

  Variables.insert({ localVariables->GetId(), localVariables });
  FrameVariables[frameId].push_back(localVariables->GetId());

  dap::Scope scope;
  scope.name = "Locals";
  scope.presentationHint = "locals";
  scope.variablesReference = localVariables->GetId();

  dap::Source source;
  source.name = frame->GetFileName();
  source.path = source.name;
  scope.source = source;

  FrameScopes[frameId].push_back(scope);

  dap::ScopesResponse response;
  response.scopes.push_back(scope);
  return response;
}

dap::VariablesResponse cmDebuggerThread::GetVariablesResponse(
  dap::VariablesRequest const& request)
{
  std::unique_lock<std::mutex> lock(Mutex);
  dap::VariablesResponse response;
  auto it = Variables.find(request.variablesReference);

  if (it != Variables.end()) {
    response.variables = it->second->GetVariables(request);
  }

  return response;
}

dap::StackTraceResponse GetStackTraceResponse(
  std::shared_ptr<cmDebuggerThread> const& thread)
{
  dap::StackTraceResponse response;
  std::unique_lock<std::mutex> lock(thread->Mutex);
  for (int i = thread->Frames.size() - 1; i >= 0; --i) {
    dap::Source source;
    source.name = thread->Frames[i]->GetFileName();
    source.path = source.name;

    dap::StackFrame stackFrame;
    stackFrame.line = thread->Frames[i]->GetLine();
    stackFrame.column = 1;
    stackFrame.name = thread->Frames[i]->GetFileName() +
      " Line " +
      std::to_string(stackFrame.line);
    stackFrame.id = thread->Frames[i]->GetId();
    stackFrame.source = source;

    response.stackFrames.push_back(stackFrame);
  }

  response.totalFrames = response.stackFrames.size();
  return response;
}

} // namespace cmDebugger
