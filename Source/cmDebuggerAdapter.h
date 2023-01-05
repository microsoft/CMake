/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <condition_variable>
#include <mutex>
#include <string>

#include <cm3p/cppdap/io.h>
#include <cm3p/cppdap/protocol.h>
#include <cm3p/cppdap/session.h>

#include "cmDebuggerBreakpointManager.h"
#include "cmDebuggerExceptionManager.h"
#include "cmDebuggerThreadManager.h"

class cmStateSnapshot;
namespace cmDebugger {

class cmDebuggerAdapter
{
  // Event provides a basic wait and signal synchronization primitive.
  class SyncEvent
  {
  public:
    // Wait() blocks until the event is fired.
    void Wait()
    {
      std::unique_lock<std::mutex> lock(Mutex);
      Cv.wait(lock, [&] { return Fired; });
    }

    // Fire() sets signals the event, and unblocks any calls to Wait().
    void Fire()
    {
      std::unique_lock<std::mutex> lock(Mutex);
      Fired = true;
      Cv.notify_all();
    }

  private:
    std::mutex Mutex;
    std::condition_variable Cv;
    bool Fired = false;
  };

  class Semaphore
  {
  public:
    Semaphore(int count_ = 0)
      : Count(count_)
    {
    }

    inline void Notify()
    {
      std::unique_lock<std::mutex> lock(Mutex);
      Count++;
      // notify the waiting thread
      Cv.notify_one();
    }

    inline void Wait()
    {
      std::unique_lock<std::mutex> lock(Mutex);
      while (Count == 0) {
        // wait on the mutex until notify is called
        Cv.wait(lock);
      }
      Count--;
    }

  private:
    std::mutex Mutex;
    std::condition_variable Cv;
    int Count;
  };

public:
  cmDebuggerAdapter(std::shared_ptr<dap::Reader> reader,
                    std::shared_ptr<dap::Writer> writer,
                    std::function<cmStateSnapshot()> getCurrentSnapshot,
                    std::string dapLogPath);
  ~cmDebuggerAdapter();

  void Terminate(int exitCode);

  void SourceFileLoaded(std::string const& sourcePath,
                        cmListFile const& listFile);
  void BeginFunction(std::string const& sourcePath,
                     std::string const& functionName, int64_t line);
  void EndFunction();

  void CheckException(MessageType t, std::string const& text);

private:
  void ClearStepRequests();
  std::function<cmStateSnapshot()> GetCurrentSnapshot;
  std::unique_ptr<dap::Session> Session;
  std::shared_ptr<dap::Writer> SessionLog;
  std::thread SessionThread;
  std::atomic<bool> SessionActive;
  std::mutex Mutex;
  SyncEvent DisconnectEvent;
  SyncEvent ConfigurationDoneEvent;
  Semaphore ContinueSem;
  std::atomic<int64_t> NextStepFrom;
  std::atomic<bool> StepInRequest;
  std::atomic<int64_t> StepOutDepth;
  std::atomic<bool> PauseRequest;
  std::unique_ptr<cmDebuggerThreadManager> ThreadManager;
  std::shared_ptr<cmDebuggerThread> DefaultThread;
  std::unique_ptr<cmDebuggerBreakpointManager> BreakpointManager;
  std::unique_ptr<cmDebuggerExceptionManager> ExceptionManager;

  // Hard-coded identifiers for the one thread, frame, variable and source.
  // These numbers have no meaning, and just need to remain constant for the
  // duration of the service.
  const dap::integer VariablesReferenceId = 300;
};

} // namespace cmDebugger
