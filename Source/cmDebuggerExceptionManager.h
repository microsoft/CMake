/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <string>
#include <unordered_map>
#include <mutex>

#include <cm/optional>
#include <cm3p/cppdap/io.h>
#include <cm3p/cppdap/protocol.h>
#include <cm3p/cppdap/session.h>

#include "cmDebuggerProtocol.h"
#include "cmMessageType.h"

namespace cmDebugger {

namespace {
struct cmDebuggerException
{
  std::string Id;
  std::string Description;
};
}

/** The exception manager. */
class cmDebuggerExceptionManager
{
  dap::Session* DapSession;
  std::mutex Mutex;
  std::unordered_map<std::string, bool> RaiseExceptions;
  std::unordered_map<MessageType,
    std::tuple<std::string, std::string>> ExceptionMap;
  int64_t NextExceptionId;
  std::optional<cmDebuggerException> TheException;

  dap::SetExceptionBreakpointsResponse HandleSetExceptionBreakpointsRequest(
    const dap::SetExceptionBreakpointsRequest& request);

  dap::ExceptionInfoResponse HandleExceptionInfoRequest(
    const dap::ExceptionInfoRequest& request);

public:
  cmDebuggerExceptionManager(dap::Session* dapSession);
  void HandleInitializeRequest(dap::CMakeInitializeResponse& response);
  std::optional<dap::StoppedEvent> RaiseExceptionIfAny(
    MessageType t, std::string const& text);
  void ClearAll();
};

} // namespace cmDebugger
