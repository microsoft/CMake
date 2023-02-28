/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

#include <stddef.h>

#include "cmDebuggerAdapter.h"
#include "cmDebuggerProtocol.h"
#include "cmStateSnapshot.h"
#include "cmVersionConfig.h"

#include "testCommon.h"

bool testBasicProtocol()
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
    [&](const dap::ExitedEvent& e) { exitedEventReceived.store(true); });
  client->registerHandler([&](const dap::TerminatedEvent& e) {
    terminatedEventReceived.store(true);
  });
  client->registerHandler([&](const dap::ThreadEvent& e) {
    if (e.reason == "started") {
      threadStarted.store(true);
    } else if (e.reason == "exited") {
      threadExited.store(true);
    }
  });

  client->bind(debugger2Client, client2Debugger);

  std::thread debuggerThread([&]() {
    std::shared_ptr<cmDebugger::cmDebuggerAdapter> debuggerAdapter =
      std::make_shared<cmDebugger::cmDebuggerAdapter>(client2Debugger,
                                                      debugger2Client, "");

    debuggerAdapterInitializedPromise.set_value(true);

    debuggerAdapter->ReportExitCode(0);

    // Give disconnectResponse some time to be received before
    // destructing debuggerAdapter.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    return 0;
  });

  dap::CMakeInitializeRequest initializeRequest;
  auto initializeResponse = client->send(initializeRequest).get();
  ASSERT_TRUE(initializeResponse.response.cmakeVersion.full == CMake_VERSION);
  ASSERT_TRUE(initializeResponse.response.cmakeVersion.major ==
              CMake_VERSION_MAJOR);
  ASSERT_TRUE(initializeResponse.response.cmakeVersion.minor ==
              CMake_VERSION_MINOR);
  ASSERT_TRUE(initializeResponse.response.cmakeVersion.patch ==
              CMake_VERSION_PATCH);
  ASSERT_TRUE(initializeResponse.response.supportsExceptionInfoRequest);
  ASSERT_TRUE(
    initializeResponse.response.exceptionBreakpointFilters.has_value());

  dap::LaunchRequest launchRequest;
  auto launchResponse = client->send(launchRequest).get();
  ASSERT_TRUE(!launchResponse.error);

  dap::ConfigurationDoneRequest configurationDoneRequest;
  auto configurationDoneResponse =
    client->send(configurationDoneRequest).get();
  ASSERT_TRUE(!configurationDoneResponse.error);

  debuggerAdapterInitializedFuture.get();

  ASSERT_TRUE(initializedEventReceived.load());

  dap::DisconnectRequest disconnectRequest;
  auto disconnectResponse = client->send(disconnectRequest).get();
  ASSERT_TRUE(!disconnectResponse.error);

  ASSERT_TRUE(threadStarted.load());
  ASSERT_TRUE(threadExited.load());
  ASSERT_TRUE(exitedEventReceived.load());
  ASSERT_TRUE(terminatedEventReceived.load());

  debuggerThread.join();
  return true;
}

int testCMDebuggerAdapter(int, char*[])
{
  return runTests(std::vector<std::function<bool()>>{
    testBasicProtocol,
  });
}
