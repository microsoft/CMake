/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <cm3p/cppdap/io.h>
#include <cm3p/cppdap/protocol.h>
#include <cm3p/cppdap/session.h>

#include "cmMessageType.h"

class cmListFile;
class cmListFileFunction;
class cmStateSnapshot;
class cmMakefile;

namespace cmDebugger {
class cmDebuggerBreakpointManager;
class cmDebuggerExceptionManager;
class cmDebuggerThread;
class cmDebuggerThreadManager;
class Semaphore;
class SyncEvent;

class cmDebuggerAdapter
{
public:
  cmDebuggerAdapter(std::shared_ptr<dap::Reader> const& reader,
                    std::shared_ptr<dap::Writer> const& writer,
                    std::string const& dapLogPath);
  ~cmDebuggerAdapter();

  void ReportExitCode(int exitCode);

  void SourceFileLoaded(std::string const& sourcePath,
                        cmListFile const& listFile);
  void BeginFunction(cmMakefile* mf, std::string const& sourcePath,
                     cmListFileFunction const& lff);
  void EndFunction();

  void CheckException(MessageType t, std::string const& text);

private:
  void ClearStepRequests();
  std::unique_ptr<dap::Session> Session;
  std::shared_ptr<dap::Writer> SessionLog;
  std::thread SessionThread;
  std::atomic<bool> SessionActive;
  std::mutex Mutex;
  std::unique_ptr<SyncEvent> DisconnectEvent;
  std::unique_ptr<SyncEvent> ConfigurationDoneEvent;
  std::unique_ptr<Semaphore> ContinueSem;
  std::atomic<int64_t> NextStepFrom;
  std::atomic<bool> StepInRequest;
  std::atomic<int64_t> StepOutDepth;
  std::atomic<bool> PauseRequest;
  std::unique_ptr<cmDebuggerThreadManager> ThreadManager;
  std::shared_ptr<cmDebuggerThread> DefaultThread;
  std::unique_ptr<cmDebuggerBreakpointManager> BreakpointManager;
  std::unique_ptr<cmDebuggerExceptionManager> ExceptionManager;
  bool SupportsVariableType;
};

} // namespace cmDebugger
