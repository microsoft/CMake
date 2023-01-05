/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include "cmConfigure.h" // IWYU pragma: keep

#include <cstring>
#include <iostream>
#include <sstream>

#include "cmake.h"
#include "cmDebuggerAdapter.h"
#include "cmDebuggerProtocol.h"
#include "cmDebuggerThread.h"
#include "cmDebuggerThreadManager.h"
#include "cmVersionConfig.h"

namespace cmDebugger {

cmDebuggerAdapter::cmDebuggerAdapter(
  std::shared_ptr<dap::Reader> reader, std::shared_ptr<dap::Writer> writer,
  std::function<cmStateSnapshot()> getCurrentSnapshot,
  std::string dapLogPath)
  : GetCurrentSnapshot(getCurrentSnapshot)
  , ThreadManager(std::make_unique<cmDebuggerThreadManager>())
  , SessionActive(true)
{
  if (!dapLogPath.empty()) {
    SessionLog = dap::file(dapLogPath.c_str());
  }
  ClearStepRequests();

  Session = dap::Session::create();
  BreakpointManager =
    std::make_unique<cmDebuggerBreakpointManager>(Session.get());
  ExceptionManager =
    std::make_unique<cmDebuggerExceptionManager>(Session.get());

  // Handle errors reported by the Session. These errors include protocol
  // parsing errors and receiving messages with no handler.
  Session->onError([&](const char* msg) {
    if (SessionLog) {
      dap::writef(SessionLog, "dap::Session error: %s\n", msg);
    }

    std::cout << "[CMake Debugger] DAP session error: " << msg << std::endl;

    BreakpointManager->ClearAll();
    ExceptionManager->ClearAll();
    ClearStepRequests();
    ContinueSem.Notify();
    DisconnectEvent.Fire();
    SessionActive.store(false);
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Initialize
  Session->registerHandler([&](const dap::CMakeInitializeRequest& req) {
    dap::CMakeInitializeResponse response;
    response.supportsConfigurationDoneRequest = true;
    response.cmakeVersion.major = CMake_VERSION_MAJOR;
    response.cmakeVersion.minor = CMake_VERSION_MINOR;
    response.cmakeVersion.patch = CMake_VERSION_PATCH;
    response.cmakeVersion.full = CMake_VERSION;
    ExceptionManager->HandleInitializeRequest(response);
    return response;
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Events_Initialized
  Session->registerSentHandler(
    [&](const dap::ResponseOrError<dap::CMakeInitializeResponse>&) {
      Session->send(dap::InitializedEvent());
    });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Threads
  Session->registerHandler([&](const dap::ThreadsRequest& req) {
    std::unique_lock<std::mutex> lock(Mutex);
    dap::ThreadsResponse response;
    dap::Thread thread;
    thread.id = DefaultThread->GetId();
    thread.name = DefaultThread->GetName();
    response.threads.push_back(thread);
    return response;
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StackTrace
  Session->registerHandler(
    [&](const dap::StackTraceRequest& request)
      -> dap::ResponseOrError<dap::StackTraceResponse> {
      std::unique_lock<std::mutex> lock(Mutex);

      cm::optional<dap::StackTraceResponse> respone =
        ThreadManager->GetThreadStackTraceResponse(request.threadId);
      if (respone.has_value()) {
        return respone.value();
      }

      return dap::Error("Unknown threadId '%d'", int(request.threadId));
    });

  // The Scopes request reports all the scopes of the given stack frame.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Scopes
  Session->registerHandler(
    [&](const dap::ScopesRequest& request)
      -> dap::ResponseOrError<dap::ScopesResponse> {
      std::unique_lock<std::mutex> lock(Mutex);
      dap::Scope scope;
      scope.name = "Locals";
      scope.presentationHint = "locals";
      scope.variablesReference = VariablesReferenceId;

      dap::Source source;
      source.name = DefaultThread->GetTopStackFrame()->FileName;
      source.path = source.name;
      scope.source = source;

      dap::ScopesResponse response;
      response.scopes.push_back(scope);
      return response;
    });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Variables
  Session->registerHandler(
    [&](const dap::VariablesRequest& request)
      -> dap::ResponseOrError<dap::VariablesResponse> {
      std::unique_lock<std::mutex> lock(Mutex);
      dap::VariablesResponse response;
      if (request.variablesReference == VariablesReferenceId) {
        dap::Variable currentLineVar;
        currentLineVar.name = "CurrentLine";
        currentLineVar.value =
          std::to_string(DefaultThread->GetTopStackFrame()->Line);
        currentLineVar.type = "int";
        response.variables.push_back(currentLineVar);

        dap::Variable cacheVars;
        cacheVars.name = "CMAKE CACHE VARIABLES";
        cacheVars.type = "collection";
        cacheVars.variablesReference = 123;
        dap::VariablePresentationHint hint;
        hint.kind = "property";
        hint.visibility = "public";
        cacheVars.presentationHint = hint;
        response.variables.push_back(cacheVars);
      }

      if (request.variablesReference == 123) {
        std::vector<std::string> closureKeys =
          GetCurrentSnapshot().ClosureKeys();
        std::sort(closureKeys.begin(), closureKeys.end());

        for each (std::string varStr in closureKeys) {
          auto val = GetCurrentSnapshot().GetDefinition(varStr);
          if (val) {
            dap::Variable var;
            var.name = varStr;
            var.type = "string";
            var.value = *val;
            var.evaluateName = varStr;
            response.variables.push_back(var);
          }
        }
      }

      return response;
    });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Pause
  Session->registerHandler([&](const dap::PauseRequest& req) {
    PauseRequest.store(true);
    return dap::PauseResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Continue
  Session->registerHandler([&](const dap::ContinueRequest& req) {
    ContinueSem.Notify();
    return dap::ContinueResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Next
  Session->registerHandler([&](const dap::NextRequest& req) {
    NextStepFrom.store(DefaultThread->GetStackFrameSize());
    ContinueSem.Notify();
    return dap::NextResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepIn
  Session->registerHandler([&](const dap::StepInRequest& req) {
    // This would stop after stepped in, single line stepped or stepped out.
    StepInRequest.store(true);
    ContinueSem.Notify();
    return dap::StepInResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepOut
  Session->registerHandler([&](const dap::StepOutRequest& req) {
    StepOutDepth.store(DefaultThread->GetStackFrameSize() - 1);
    ContinueSem.Notify();
    return dap::StepOutResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Launch
  Session->registerHandler(
    [&](const dap::LaunchRequest& req) { return dap::LaunchResponse(); });

  // Handler for disconnect requests
  Session->registerHandler([&](const dap::DisconnectRequest& request) {
    BreakpointManager->ClearAll();
    ExceptionManager->ClearAll();
    ClearStepRequests();
    ContinueSem.Notify();
    DisconnectEvent.Fire();
    SessionActive.store(false);
    return dap::DisconnectResponse();
  });

  Session->registerHandler([&](const dap::EvaluateRequest& request) {
    dap::EvaluateResponse response;
    if (request.context.has_value() &&
        request.context.value() == "hover") {
      auto var = GetCurrentSnapshot().GetDefinition(request.expression);
      if (var) {
        response.type = "string";
        response.result = var->c_str();
      }
    }
    return response;
  });

  // The ConfigurationDone request is made by the client once all configuration
  // requests have been made.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ConfigurationDone
  Session->registerHandler(
    [&](const dap::ConfigurationDoneRequest& req) {
      ConfigurationDoneEvent.Fire();
      return dap::ConfigurationDoneResponse();
    });

  // Connect to the client.
  if (SessionLog) {
    Session->connect(spy(reader, SessionLog), spy(writer, SessionLog));
  } else {
    Session->connect(reader, writer);
  }

  // Start the processing thread.
  SessionThread = std::thread([this] {
    while (SessionActive.load()) {
      if (auto payload = Session->getPayload()) {
        payload();
      }
    }
  });

  ConfigurationDoneEvent.Wait();

  DefaultThread = ThreadManager->StartThread("CMake script");
  dap::ThreadEvent threadEvent;
  threadEvent.reason = "started";
  threadEvent.threadId = DefaultThread->GetId();
  Session->send(threadEvent);
}

cmDebuggerAdapter::~cmDebuggerAdapter() {
  if (SessionThread.joinable()) {
    SessionThread.join();
  }

  Session.reset(nullptr);

  if (SessionLog) {
    SessionLog->close();
  }
}

void cmDebuggerAdapter::Terminate(int exitCode)
{
  ThreadManager->EndThread(DefaultThread);
  dap::ThreadEvent threadEvent;
  threadEvent.reason = "exited";
  threadEvent.threadId = DefaultThread->GetId();
  DefaultThread.reset();

  dap::ExitedEvent exitEvent;
  exitEvent.exitCode = exitCode;

  dap::TerminatedEvent terminatedEvent;

  if (SessionActive.load()) {
    Session->send(threadEvent);
    Session->send(exitEvent);
    Session->send(terminatedEvent);
  }

  // Wait until disconnected or error.
  DisconnectEvent.Wait();
}

void cmDebuggerAdapter::SourceFileLoaded(std::string const& sourcePath,
                                         cmListFile const& listFile)
{
  BreakpointManager->SourceFileLoaded(sourcePath, listFile);
}

void cmDebuggerAdapter::BeginFunction(std::string const& sourcePath,
                                      std::string const& functionName,
                                      int64_t line)
{
  std::unique_lock<std::mutex> lock(Mutex);
  cmDebuggerStackFrame frame(sourcePath, functionName, line);
  DefaultThread->PushStackFrame(frame);

  if (line == 0) {
    // File just loaded, continue to first valid function call.
    return;
  }

  auto hits = BreakpointManager->GetBreakpoints(sourcePath, line);
  lock.unlock();

  bool waitSem = false;
  dap::StoppedEvent stoppedEvent;
  stoppedEvent.allThreadsStopped = true;
  stoppedEvent.threadId = DefaultThread->GetId();
  if (hits.size() > 0) {
    ClearStepRequests();
    waitSem = true;

    dap::array<dap::integer> hitBreakpoints;
    hitBreakpoints.resize(hits.size());
    std::transform(hits.begin(), hits.end(), hitBreakpoints.begin(),
                   [&](const auto& id) { return dap::integer(id); });
    stoppedEvent.reason = "breakpoint";
    stoppedEvent.hitBreakpointIds = hitBreakpoints;
  }

  if (long(DefaultThread->GetStackFrameSize()) <= NextStepFrom.load() ||
      StepInRequest.load() ||
      long(DefaultThread->GetStackFrameSize()) <= StepOutDepth.load()) {
    ClearStepRequests();
    waitSem = true;

    stoppedEvent.reason = "step";
  }

  if (PauseRequest.load()) {
    ClearStepRequests();
    waitSem = true;

    stoppedEvent.reason = "pause";
  }

  if (waitSem) {
    Session->send(stoppedEvent);
    ContinueSem.Wait();
  }
}

void cmDebuggerAdapter::EndFunction()
{
  DefaultThread->PopStackFrame();
}

void cmDebuggerAdapter::CheckException(MessageType t, std::string const& text)
{
  std::optional<dap::StoppedEvent> stoppedEvent =
    ExceptionManager->RaiseExceptionIfAny(t, text);
  if (stoppedEvent.has_value()) {
    stoppedEvent->threadId = DefaultThread->GetId();
    Session->send(*stoppedEvent);
    ContinueSem.Wait();
  }
}

void cmDebuggerAdapter::ClearStepRequests()
{
  NextStepFrom.store(INT_MIN);
  StepInRequest.store(false);
  StepOutDepth.store(INT_MIN);
  PauseRequest.store(false);
}

} // namespace cmDebugger
