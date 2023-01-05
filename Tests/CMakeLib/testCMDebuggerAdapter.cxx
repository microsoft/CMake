#include <future>
#include <iostream>
#include <stddef.h>
#include <string>
#include <thread>

#include "cmDebuggerAdapter.h"
#include "cmDebuggerProtocol.h"
#include "cmStateSnapshot.h"
#include "cmVersionConfig.h"

#define cmPassed(m) std::cout << "Passed: " << (m) << "\n"
#define cmFailed(m)                                                           \
  std::cout << "FAILED: " << (m) << "\n";                                     \
  failed = 1

#define cmAssert(exp, m)                                                      \
  do {                                                                        \
    if ((exp)) {                                                              \
      cmPassed(m);                                                            \
    } else {                                                                  \
      cmFailed(m);                                                            \
    }                                                                         \
  } while (false)

int debugger(std::shared_ptr<dap::ReaderWriter> in,
             std::shared_ptr<dap::ReaderWriter> out,
             cmStateSnapshot& currentSnapshot,
             int exitCode,
             std::promise<bool>&& debuggerAdapterInitializedPromise)
{
  std::shared_ptr<cmDebugger::cmDebuggerAdapter> debuggerAdapter =
    std::make_shared<cmDebugger::cmDebuggerAdapter>(
      in, out, [&]() { return currentSnapshot; }, "");

  debuggerAdapterInitializedPromise.set_value(true);

  debuggerAdapter->Terminate(exitCode);
  return 0;
}

int testCMDebuggerAdapter(int, char*[])
{
  std::promise<bool> debuggerAdapterInitializedPromise;
  std::future<bool> debuggerAdapterInitializedFuture =
    debuggerAdapterInitializedPromise.get_future();
  int failed = 0;
  std::atomic<bool> initializedEventReceived = false;
  std::atomic<bool> exitedEventReceived = false;
  std::atomic<bool> terminatedEventReceived = false;
  std::atomic<bool> threadStarted = false;
  std::atomic<bool> threadExited = false;

  std::shared_ptr<dap::ReaderWriter> client2Debugger = dap::pipe();
  std::shared_ptr<dap::ReaderWriter> debugger2Client = dap::pipe();
  std::unique_ptr<dap::Session> client = dap::Session::create();
  client->registerHandler([&](const dap::InitializedEvent& e) {
    initializedEventReceived.store(true);
  });
  client->registerHandler(
    [&](const dap::ExitedEvent& e) { exitedEventReceived.store(true);
  });
  client->registerHandler([&](const dap::TerminatedEvent& e) {
    terminatedEventReceived.store(true);
  });
  client->registerHandler(
    [&](const dap::ThreadEvent& e) {
    if (e.reason == "started") {
      threadStarted.store(true);
    } else if (e.reason == "exited") {
      threadExited.store(true);
    }
  });

  client->bind(debugger2Client, client2Debugger);

  cmStateSnapshot currentSnapshot;
  std::thread debuggerThread(debugger, client2Debugger, debugger2Client,
                             currentSnapshot, 0,
                             std::move(debuggerAdapterInitializedPromise));
  dap::CMakeInitializeRequest initializeRequest;
  auto initializeResponse = client->send(initializeRequest).get();
  cmAssert(initializeResponse.response.cmakeVersion.full == CMake_VERSION,
           "CMakeInitializeRequest response contains correct CMake version");
  cmAssert(
    initializeResponse.response.cmakeVersion.major == CMake_VERSION_MAJOR,
    "CMakeInitializeRequest response contains correct CMake major version");
  cmAssert(
    initializeResponse.response.cmakeVersion.minor == CMake_VERSION_MINOR,
    "CMakeInitializeRequest response contains correct CMake minor version");
  cmAssert(
    initializeResponse.response.cmakeVersion.patch == CMake_VERSION_PATCH,
    "CMakeInitializeRequest response contains correct CMake patch version");
  cmAssert(
    initializeResponse.response.supportsExceptionInfoRequest,
    "CMakeInitializeRequest response contains supportsExceptionInfoRequest");
  cmAssert(
    initializeResponse.response.exceptionBreakpointFilters.has_value(),
    "CMakeInitializeRequest response contains exceptionBreakpointFilters");

  dap::LaunchRequest launchRequest;
  auto launchResponse = client->send(launchRequest).get();
  cmAssert(!launchResponse.error,
           "LaunchRequest responded correctly");

  dap::ConfigurationDoneRequest configurationDoneRequest;
  auto configurationDoneResponse = client->send(configurationDoneRequest).get();
  cmAssert(!configurationDoneResponse.error,
           "ConfigurationDoneRequest responded correctly");

  debuggerAdapterInitializedFuture.get();

  cmAssert(initializedEventReceived.load(),
           "Received initialized event");

  dap::DisconnectRequest disconnectRequest;
  auto disconnectResponse = client->send(disconnectRequest).get();
  cmAssert(!disconnectResponse.error,
           "DisconnectRequest responded correctly");

  cmAssert(threadStarted.load(), "Received thread started event");
  cmAssert(threadExited.load(), "Received thread exited event");
  cmAssert(exitedEventReceived.load(), "Received exited event");
  cmAssert(terminatedEventReceived.load(), "Received terminated event");

  debuggerThread.join();
  return failed;
}
