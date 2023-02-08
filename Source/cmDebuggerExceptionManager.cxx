/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include <algorithm>

#include "cmDebuggerExceptionManager.h"
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
    std::make_tuple("AUTHOR_WARNING", "Warning (dev)");
  ExceptionMap[MessageType::AUTHOR_ERROR] =
    std::make_tuple("AUTHOR_ERROR", "Error (dev)");
  ExceptionMap[MessageType::FATAL_ERROR] =
    std::make_tuple("FATAL_ERROR", "Fatal error");
  ExceptionMap[MessageType::INTERNAL_ERROR] =
    std::make_tuple("INTERNAL_ERROR", "Internal error");
  ExceptionMap[MessageType::MESSAGE] =
    std::make_tuple("MESSAGE", "Other messages");
  ExceptionMap[MessageType::WARNING] = std::make_tuple("WARNING", "Warning");
  ExceptionMap[MessageType::LOG] = std::make_tuple("LOG", "Debug log");
  ExceptionMap[MessageType::DEPRECATION_ERROR] =
    std::make_tuple("DEPRECATION_ERROR", "Deprecation error");
  ExceptionMap[MessageType::DEPRECATION_WARNING] =
    std::make_tuple("DEPRECATION_WARNING", "Deprecation warning");
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
  for (auto& filter : request.filters)
  {
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
  if (TheException.has_value())
  {
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
  for (auto& [key, value] : ExceptionMap)
  {
    dap::ExceptionBreakpointsFilter filter;
    filter.filter = std::get<0>(value);
    filter.label = std::get<1>(value);
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
  if (RaiseExceptions[std::get<0>(ExceptionMap[t])])
  {
    dap::StoppedEvent stoppedEvent;
    stoppedEvent.allThreadsStopped = true;
    stoppedEvent.reason = "exception";
    stoppedEvent.description = "Pause on exception";
    stoppedEvent.text = text;
    TheException = cmDebuggerException{ std::get<0>(ExceptionMap[t]),
                                        text };
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
