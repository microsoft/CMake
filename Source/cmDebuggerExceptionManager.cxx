/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebuggerExceptionManager.h"

#include <algorithm>

#include "cmMessageType.h"

namespace cmDebugger {

cmDebuggerExceptionManager::cmDebuggerExceptionManager(
  dap::Session* dapSession)
  : DapSession(dapSession)
  , NextExceptionId(0)
{
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_SetExceptionBreakpoints
  DapSession->registerHandler(
    [&](const dap::SetExceptionBreakpointsRequest& request) {
      return HandleSetExceptionBreakpointsRequest(request);
    });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ExceptionInfo
  DapSession->registerHandler([&](const dap::ExceptionInfoRequest& request) {
    return HandleExceptionInfoRequest(request);
  });

  ExceptionMap[MessageType::AUTHOR_WARNING] =
    cmDebuggerExceptionFilter{ "AUTHOR_WARNING", "Warning (dev)" };
  ExceptionMap[MessageType::AUTHOR_ERROR] =
    cmDebuggerExceptionFilter{ "AUTHOR_ERROR", "Error (dev)" };
  ExceptionMap[MessageType::FATAL_ERROR] =
    cmDebuggerExceptionFilter{ "FATAL_ERROR", "Fatal error" };
  ExceptionMap[MessageType::INTERNAL_ERROR] =
    cmDebuggerExceptionFilter{ "INTERNAL_ERROR", "Internal error" };
  ExceptionMap[MessageType::MESSAGE] =
    cmDebuggerExceptionFilter{ "MESSAGE", "Other messages" };
  ExceptionMap[MessageType::WARNING] =
    cmDebuggerExceptionFilter{ "WARNING", "Warning" };
  ExceptionMap[MessageType::LOG] =
    cmDebuggerExceptionFilter{ "LOG", "Debug log" };
  ExceptionMap[MessageType::DEPRECATION_ERROR] =
    cmDebuggerExceptionFilter{ "DEPRECATION_ERROR", "Deprecation error" };
  ExceptionMap[MessageType::DEPRECATION_WARNING] =
    cmDebuggerExceptionFilter{ "DEPRECATION_WARNING", "Deprecation warning" };
  RaiseExceptions["AUTHOR_ERROR"] = true;
  RaiseExceptions["FATAL_ERROR"] = true;
  RaiseExceptions["INTERNAL_ERROR"] = true;
  RaiseExceptions["DEPRECATION_ERROR"] = true;
}

dap::SetExceptionBreakpointsResponse
cmDebuggerExceptionManager::HandleSetExceptionBreakpointsRequest(
  dap::SetExceptionBreakpointsRequest const& request)
{
  std::unique_lock<std::mutex> lock(Mutex);
  dap::SetExceptionBreakpointsResponse response;
  RaiseExceptions.clear();
  for (auto& filter : request.filters) {
    RaiseExceptions[filter] = true;
  }

  return response;
}

dap::ExceptionInfoResponse
cmDebuggerExceptionManager::HandleExceptionInfoRequest(
  dap::ExceptionInfoRequest const& request)
{
  std::unique_lock<std::mutex> lock(Mutex);

  dap::ExceptionInfoResponse response;
  if (TheException.has_value()) {
    response.exceptionId = TheException->Id;
    response.breakMode = "always";
    response.description = TheException->Description;
    TheException = {};
  }
  return response;
}

void cmDebuggerExceptionManager::HandleInitializeRequest(
  dap::CMakeInitializeResponse& response)
{
  std::unique_lock<std::mutex> lock(Mutex);
  response.supportsExceptionInfoRequest = true;

  dap::array<dap::ExceptionBreakpointsFilter> exceptionBreakpointFilters;
  for (auto& [key, value] : ExceptionMap) {
    dap::ExceptionBreakpointsFilter filter;
    filter.filter = value.Filter;
    filter.label = value.Label;
    filter.def = RaiseExceptions[filter.filter];
    exceptionBreakpointFilters.emplace_back(filter);
  }

  response.exceptionBreakpointFilters = exceptionBreakpointFilters;
}

std::optional<dap::StoppedEvent>
cmDebuggerExceptionManager::RaiseExceptionIfAny(MessageType t,
                                                std::string const& text)
{
  std::unique_lock<std::mutex> lock(Mutex);
  if (RaiseExceptions[ExceptionMap[t].Filter]) {
    dap::StoppedEvent stoppedEvent;
    stoppedEvent.allThreadsStopped = true;
    stoppedEvent.reason = "exception";
    stoppedEvent.description = "Pause on exception";
    stoppedEvent.text = text;
    TheException = cmDebuggerException{ ExceptionMap[t].Filter, text };
    return stoppedEvent;
  }

  return {};
}

void cmDebuggerExceptionManager::ClearAll()
{
  std::unique_lock<std::mutex> lock(Mutex);
  RaiseExceptions.clear();
}

} // namespace cmDebugger
