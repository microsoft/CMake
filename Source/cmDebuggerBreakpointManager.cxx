/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebuggerBreakpointManager.h"

#include <algorithm>

namespace cmDebugger {

cmDebuggerBreakpointManager::cmDebuggerBreakpointManager(
  dap::Session* dapSession)
  : DapSession(dapSession)
  , NextBreakpointId(0)
{
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_SetBreakpoints
  DapSession->registerHandler([&](const dap::SetBreakpointsRequest& request) {
    return HandleSetBreakpointsRequest(request);
  });
}

int64_t cmDebuggerBreakpointManager::FindFunctionStartLine(
  std::string const& sourcePath, int64_t line)
{
  auto location =
    find_if(ListFileFunctionLines[sourcePath].begin(),
            ListFileFunctionLines[sourcePath].end(),
            [=](cmDebuggerFunctionLocation const& location) {
              return location.StartLine <= line && location.EndLine >= line;
            });

  if (location != ListFileFunctionLines[sourcePath].end()) {
    return location->StartLine;
  }

  return 0;
}

int64_t cmDebuggerBreakpointManager::CalibrateBreakpointLine(
  std::string const& sourcePath,
    int64_t line)
{
  auto location =
    find_if(ListFileFunctionLines[sourcePath].begin(),
            ListFileFunctionLines[sourcePath].end(),
            [=](cmDebuggerFunctionLocation const& location) {
              return location.StartLine >= line;
            });

  if (location != ListFileFunctionLines[sourcePath].end()) {
    return location->StartLine;
  }

  if (ListFileFunctionLines[sourcePath].size() > 0 &&
      ListFileFunctionLines[sourcePath].back().EndLine <= line) {
    // return last function start line for any breakpoints after.
    return ListFileFunctionLines[sourcePath].back().StartLine;
  }

  return 0;
}

dap::SetBreakpointsResponse
cmDebuggerBreakpointManager::HandleSetBreakpointsRequest(
  dap::SetBreakpointsRequest const& request)
{
  std::unique_lock<std::mutex> lock(Mutex);

  dap::SetBreakpointsResponse response;

  auto sourcePath =
    cmSystemTools::GetActualCaseForPath(request.source.path.value());
  auto& breakpoints = request.breakpoints.value({});
  if (ListFileFunctionLines.find(sourcePath) != ListFileFunctionLines.end()) {
    // The file has loaded, we can validate breakpoints.
    if (Breakpoints.find(sourcePath) != Breakpoints.end()) {
      Breakpoints[sourcePath].clear();
    }
    response.breakpoints.resize(breakpoints.size());
    for (size_t i = 0; i < breakpoints.size(); i++) {
      int64_t correctedLine =
        CalibrateBreakpointLine(sourcePath, breakpoints[i].line);
      if (correctedLine > 0) {
        Breakpoints[sourcePath].emplace_back(NextBreakpointId++,
                                             correctedLine);
        response.breakpoints[i].id = Breakpoints[sourcePath].back().GetId();
        response.breakpoints[i].line =
          Breakpoints[sourcePath].back().GetLine();
        response.breakpoints[i].verified = true;
      } else {
        response.breakpoints[i].verified = false;
        response.breakpoints[i].line = breakpoints[i].line;
      }
      dap::Source dapSrc;
      dapSrc.path = sourcePath;
      response.breakpoints[i].source = dapSrc;
    }
  } else {
    // The file has not loaded, validate breakpoints later.
    ListFilePendingValidations.emplace(sourcePath);

    response.breakpoints.resize(breakpoints.size());
    for (size_t i = 0; i < breakpoints.size(); i++) {
      Breakpoints[sourcePath].emplace_back(NextBreakpointId++,
                                           breakpoints[i].line);
      response.breakpoints[i].id = Breakpoints[sourcePath].back().GetId();
      response.breakpoints[i].line = Breakpoints[sourcePath].back().GetLine();
      response.breakpoints[i].verified = false;
      dap::Source dapSrc;
      dapSrc.path = sourcePath;
      response.breakpoints[i].source = dapSrc;
    }
  }

  return response;
}

void cmDebuggerBreakpointManager::SourceFileLoaded(
  std::string const& sourcePath,
  std::vector<cmListFileFunction> const& functions)
{
  std::unique_lock<std::mutex> lock(Mutex);
  if (ListFileFunctionLines.find(sourcePath) != ListFileFunctionLines.end()) {
    // this is not expected.
    return;
  }

  for (cmListFileFunction const& func : functions) {
    ListFileFunctionLines[sourcePath].emplace_back(
      cmDebuggerFunctionLocation{ func.Line(), func.LineEnd() });
  }

  if (ListFilePendingValidations.find(sourcePath) ==
      ListFilePendingValidations.end()) {
    return;
  }

  ListFilePendingValidations.erase(sourcePath);

  for (size_t i = 0; i < Breakpoints[sourcePath].size(); i++) {
    dap::BreakpointEvent breakpointEvent;
    breakpointEvent.breakpoint.id = Breakpoints[sourcePath][i].GetId();
    breakpointEvent.breakpoint.line = Breakpoints[sourcePath][i].GetLine();
    auto source = dap::Source();
    source.path = sourcePath;
    breakpointEvent.breakpoint.source = source;
    int64_t correctedLine = CalibrateBreakpointLine(
      sourcePath, Breakpoints[sourcePath][i].GetLine());
    if (correctedLine != Breakpoints[sourcePath][i].GetLine()) {
      Breakpoints[sourcePath][i].ChangeLine(correctedLine);
    }
    breakpointEvent.reason = "changed";
    breakpointEvent.breakpoint.verified = (correctedLine > 0);
    if (breakpointEvent.breakpoint.verified) {
      breakpointEvent.breakpoint.line = correctedLine;
    } else {
      Breakpoints[sourcePath][i].Invalid();
    }

    DapSession->send(breakpointEvent);
  }
}

std::vector<int64_t> cmDebuggerBreakpointManager::GetBreakpoints(
  std::string const& sourcePath, int64_t line)
{
  std::unique_lock<std::mutex> lock(Mutex);
  const auto& all = Breakpoints[sourcePath];
  std::vector<int64_t> breakpoints;
  if (all.size() == 0) {
    return breakpoints;
  }

  std::vector<cmDebuggerSourceBreakpoint>::const_iterator it = all.begin();

  while ((it = std::find_if(it, all.end(), [&](const auto& breakpoint) {
            return (breakpoint.GetIsValid() && breakpoint.GetLine() == line);
          })) != all.end()) {
    breakpoints.emplace_back(it->GetId());
    ++it;
  }

  return breakpoints;
}

void cmDebuggerBreakpointManager::ClearAll()
{
  std::unique_lock<std::mutex> lock(Mutex);
  Breakpoints.clear();
}

} // namespace cmDebugger
