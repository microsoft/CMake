/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <string>
#include <tuple>
#include <unordered_map>
#include <mutex>
#include <unordered_set>

#include <cm/optional>
#include <cm3p/cppdap/io.h>
#include <cm3p/cppdap/protocol.h>
#include <cm3p/cppdap/session.h>

#include "cmListFileCache.h"
#include "cmDebuggerSourceBreakpoint.h"

namespace cmDebugger {

/** The breakpoint manager. */
class cmDebuggerBreakpointManager
{
  dap::Session* DapSession;
  std::mutex Mutex;
  std::unordered_map<std::string, std::vector<cmDebuggerSourceBreakpoint>>
    Breakpoints;
  std::unordered_map<
    std::string,
    std::vector<std::tuple<int64_t /*line*/, int64_t /*endLine*/>>>
    ListFileFunctionLines;
  std::unordered_set<std::string> ListFilePendingValidations;
  int64_t NextBreakpointId;

  dap::SetBreakpointsResponse HandleSetBreakpointsRequest(
    const dap::SetBreakpointsRequest& request);
  int64_t FindFunctionStartLine(std::string const& sourcePath, int64_t line);

public:
  cmDebuggerBreakpointManager(dap::Session* dapSession);
  void SourceFileLoaded(std::string const& sourcePath,
                        cmListFile const& listFile);
  std::vector<int64_t> GetBreakpoints(std::string const& sourcePath,
                                      int64_t line);
  void ClearAll();
  int64_t GetLoadedFileNumber();
};

} // namespace cmDebugger
