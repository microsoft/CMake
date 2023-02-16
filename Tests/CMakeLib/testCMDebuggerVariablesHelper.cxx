/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>

#include "cmDebuggerStackFrame.h"
#include "cmDebuggerVariables.h"
#include "cmDebuggerVariablesHelper.h"
#include "cmDebuggerVariablesManager.h"
#include "cmGlobalGenerator.h"
#include "cmListFileCache.h"
#include "cmMakefile.h"
#include "cmState.h"
#include "cmStateDirectory.h"
#include "cmTest.h"
#include "cmake.h"

#include "testCommon.h"

static dap::VariablesRequest CreateVariablesRequest(int64_t reference)
{
  dap::VariablesRequest variableRequest;
  variableRequest.variablesReference = reference;
  return variableRequest;
}

struct Dummies
{
  std::shared_ptr<cmake> CMake;
  std::shared_ptr<cmMakefile> Makefile;
  std::shared_ptr<cmGlobalGenerator> GlobalGenerator;
};

static Dummies CreateDummies(
  std::string targetName,
  std::string currentSourceDirectory = "c:/CurrentSourceDirectory",
  std::string currentBinaryDirectory = "c:/CurrentBinaryDirectory")
{
  Dummies dummies;
  dummies.CMake =
    std::make_shared<cmake>(cmake::RoleProject, cmState::Project);
  cmState* state = dummies.CMake->GetState();
  dummies.GlobalGenerator =
    std::make_shared<cmGlobalGenerator>(dummies.CMake.get());
  cmStateSnapshot snapshot = state->CreateBaseSnapshot();
  snapshot.GetDirectory().SetCurrentSource(currentSourceDirectory);
  snapshot.GetDirectory().SetCurrentBinary(currentBinaryDirectory);
  dummies.Makefile =
    std::make_shared<cmMakefile>(dummies.GlobalGenerator.get(), snapshot);
  dummies.Makefile->CreateNewTarget(targetName, cmStateEnums::EXECUTABLE);
  return dummies;
}

static bool testCreateFromPolicyMap()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  cmPolicies::PolicyMap policyMap;
  policyMap.Set(cmPolicies::CMP0000, cmPolicies::NEW);
  policyMap.Set(cmPolicies::CMP0003, cmPolicies::WARN);
  policyMap.Set(cmPolicies::CMP0005, cmPolicies::OLD);
  auto vars = cmDebugger::cmDebuggerVariablesHelper::Create(
    variablesManager, "Locals", true, policyMap);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));
  ASSERT_TRUE(variables.size() == 3);
  ASSERT_TRUE(variables[0].name == "CMP0000");
  ASSERT_TRUE(variables[0].value == "NEW");
  ASSERT_TRUE(variables[0].type.value() == "string");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);

  ASSERT_TRUE(variables[1].name == "CMP0003");
  ASSERT_TRUE(variables[1].value == "WARN");
  ASSERT_TRUE(variables[1].type.value() == "string");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);

  ASSERT_TRUE(variables[2].name == "CMP0005");
  ASSERT_TRUE(variables[2].value == "OLD");
  ASSERT_TRUE(variables[2].type.value() == "string");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);

  return true;
}

static bool testCreateFromPairVector()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  std::vector<std::pair<std::string, std::string>> pairs = {
    { "Foo1", "Bar1" }, { "Foo2", "Bar2" }
  };

  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, pairs);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(vars->GetValue() == std::to_string(pairs.size()));
  ASSERT_TRUE(variables.size() == 2);
  ASSERT_TRUE(variables[0].name == "Foo1");
  ASSERT_TRUE(variables[0].value == "Bar1");
  ASSERT_TRUE(variables[0].type.value() == "string");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);

  ASSERT_TRUE(variables[1].name == "Foo2");
  ASSERT_TRUE(variables[1].value == "Bar2");
  ASSERT_TRUE(variables[1].type.value() == "string");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);

  auto none = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true,
    std::vector<std::pair<std::string, std::string>>());

  ASSERT_TRUE(none == nullptr);

  return true;
}

static bool testCreateFromSet()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  std::set<std::string> set = { "Foo", "Bar" };

  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, set);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(vars->GetValue() == std::to_string(set.size()));
  ASSERT_TRUE(variables.size() == 2);
  ASSERT_TRUE(variables[0].name == "[0]");
  ASSERT_TRUE(variables[0].value == "Bar");
  ASSERT_TRUE(variables[0].type.value() == "string");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);

  ASSERT_TRUE(variables[1].name == "[1]");
  ASSERT_TRUE(variables[1].value == "Foo");
  ASSERT_TRUE(variables[1].type.value() == "string");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);

  auto none = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, std::set<std::string>());

  ASSERT_TRUE(none == nullptr);

  return true;
}

static bool testCreateFromStringVector()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  std::vector<std::string> list = { "Foo", "Bar" };

  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, list);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(vars->GetValue() == std::to_string(list.size()));
  ASSERT_TRUE(variables.size() == 2);
  ASSERT_TRUE(variables[0].name == "[0]");
  ASSERT_TRUE(variables[0].value == "Foo");
  ASSERT_TRUE(variables[0].type.value() == "string");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);

  ASSERT_TRUE(variables[1].name == "[1]");
  ASSERT_TRUE(variables[1].value == "Bar");
  ASSERT_TRUE(variables[1].type.value() == "string");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);

  auto none = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, std::vector<std::string>());

  ASSERT_TRUE(none == nullptr);

  return true;
}

static bool testCreateFromTarget()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  auto dummies = CreateDummies("Foo");

  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, dummies.Makefile->GetOrderedTargets());

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(variables.size() == 1);
  ASSERT_TRUE(variables[0].name == "Foo");
  ASSERT_TRUE(variables[0].value == "EXECUTABLE");
  ASSERT_TRUE(variables[0].type.value() == "collection");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);

  variables = variablesManager->HandleVariablesRequest(
    CreateVariablesRequest(variables[0].variablesReference));

  ASSERT_TRUE(variables.size() == 15);
  ASSERT_TRUE(variables[0].name == "GlobalGenerator");
  ASSERT_TRUE(variables[0].value == "Generic");
  ASSERT_TRUE(variables[0].type.value() == "collection");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].name == "IsAIX");
  ASSERT_TRUE(variables[1].value == "FALSE");
  ASSERT_TRUE(variables[1].type.value() == "bool");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[2].name == "IsAndroidGuiExecutable");
  ASSERT_TRUE(variables[2].value == "FALSE");
  ASSERT_TRUE(variables[2].type.value() == "bool");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[3].name == "IsAppBundleOnApple");
  ASSERT_TRUE(variables[3].value == "FALSE");
  ASSERT_TRUE(variables[3].type.value() == "bool");
  ASSERT_TRUE(variables[3].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[4].name == "IsDLLPlatform");
  ASSERT_TRUE(variables[4].value == "FALSE");
  ASSERT_TRUE(variables[4].type.value() == "bool");
  ASSERT_TRUE(variables[4].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[5].name == "IsExecutableWithExports");
  ASSERT_TRUE(variables[5].value == "FALSE");
  ASSERT_TRUE(variables[5].type.value() == "bool");
  ASSERT_TRUE(variables[5].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[6].name == "IsFrameworkOnApple");
  ASSERT_TRUE(variables[6].value == "FALSE");
  ASSERT_TRUE(variables[6].type.value() == "bool");
  ASSERT_TRUE(variables[6].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[7].name == "IsImported");
  ASSERT_TRUE(variables[7].value == "FALSE");
  ASSERT_TRUE(variables[7].type.value() == "bool");
  ASSERT_TRUE(variables[7].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[8].name == "IsImportedGloballyVisible");
  ASSERT_TRUE(variables[8].value == "FALSE");
  ASSERT_TRUE(variables[8].type.value() == "bool");
  ASSERT_TRUE(variables[8].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[9].name == "IsPerConfig");
  ASSERT_TRUE(variables[9].value == "TRUE");
  ASSERT_TRUE(variables[9].type.value() == "bool");
  ASSERT_TRUE(variables[9].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[10].name == "Makefile");
  ASSERT_TRUE(!variables[10].value.empty());
  ASSERT_TRUE(variables[10].type.value() == "collection");
  ASSERT_TRUE(variables[10].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[11].name == "Name");
  ASSERT_TRUE(variables[11].value == "Foo");
  ASSERT_TRUE(variables[11].type.value() == "string");
  ASSERT_TRUE(variables[11].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[12].name == "PolicyMap");
  ASSERT_TRUE(variables[12].value == "");
  ASSERT_TRUE(variables[12].type.value() == "collection");
  ASSERT_TRUE(variables[12].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[13].name == "Properties");
  ASSERT_TRUE(variables[13].value ==
              std::to_string(dummies.Makefile->GetOrderedTargets()[0]
                               ->GetProperties()
                               .GetList()
                               .size()));
  ASSERT_TRUE(variables[13].type.value() == "collection");
  ASSERT_TRUE(variables[13].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[14].name == "Type");
  ASSERT_TRUE(variables[14].value == "EXECUTABLE");
  ASSERT_TRUE(variables[14].type.value() == "string");
  ASSERT_TRUE(variables[14].evaluateName.has_value() == false);

  auto none = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, std::vector<cmTarget*>());

  ASSERT_TRUE(none == nullptr);

  return true;
}

static bool testCreateFromGlobalGenerator()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  auto dummies = CreateDummies("Foo");

  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, dummies.GlobalGenerator.get());

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(variables.size() == 10);
  ASSERT_TRUE(variables[0].name == "AllTargetName");
  ASSERT_TRUE(variables[0].value == "ALL_BUILD");
  ASSERT_TRUE(variables[0].type.value() == "string");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].name == "ForceUnixPaths");
  ASSERT_TRUE(variables[1].value == "FALSE");
  ASSERT_TRUE(variables[1].type.value() == "bool");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[2].name == "InstallTargetName");
  ASSERT_TRUE(variables[2].value == "INSTALL");
  ASSERT_TRUE(variables[2].type.value() == "string");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[3].name == "IsMultiConfig");
  ASSERT_TRUE(variables[3].value == "FALSE");
  ASSERT_TRUE(variables[3].type.value() == "bool");
  ASSERT_TRUE(variables[3].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[4].name == "MakefileEncoding");
  ASSERT_TRUE(variables[4].value == "None");
  ASSERT_TRUE(variables[4].type.value() == "string");
  ASSERT_TRUE(variables[4].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[5].name == "Name");
  ASSERT_TRUE(variables[5].value == "Generic");
  ASSERT_TRUE(variables[5].type.value() == "string");
  ASSERT_TRUE(variables[5].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[6].name == "NeedSymbolicMark");
  ASSERT_TRUE(variables[6].value == "FALSE");
  ASSERT_TRUE(variables[6].type.value() == "bool");
  ASSERT_TRUE(variables[6].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[7].name == "PackageTargetName");
  ASSERT_TRUE(variables[7].value == "PACKAGE");
  ASSERT_TRUE(variables[7].type.value() == "string");
  ASSERT_TRUE(variables[7].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[8].name == "TestTargetName");
  ASSERT_TRUE(variables[8].value == "RUN_TESTS");
  ASSERT_TRUE(variables[8].type.value() == "string");
  ASSERT_TRUE(variables[8].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[9].name == "UseLinkScript");
  ASSERT_TRUE(variables[9].value == "FALSE");
  ASSERT_TRUE(variables[9].type.value() == "bool");
  ASSERT_TRUE(variables[9].evaluateName.has_value() == false);

  auto none = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true,
    static_cast<cmGlobalGenerator*>(nullptr));

  ASSERT_TRUE(none == nullptr);

  return true;
}

static bool testCreateFromTests()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  auto dummies = CreateDummies("Foo");
  cmTest test1 = cmTest(dummies.Makefile.get());
  test1.SetName("Test1");
  test1.SetOldStyle(false);
  test1.SetCommandExpandLists(true);
  test1.SetCommand(std::vector<std::string>{ "Foo1", "arg1" });
  test1.SetProperty("Prop1", "Prop1");
  cmTest test2 = cmTest(dummies.Makefile.get());
  test2.SetName("Test2");
  test2.SetOldStyle(false);
  test2.SetCommandExpandLists(false);
  test2.SetCommand(std::vector<std::string>{ "Bar1", "arg1", "arg2" });
  test2.SetProperty("Prop2", "Prop2");
  test2.SetProperty("Prop3", "Prop3");

  std::vector<cmTest*> tests = { &test1, &test2 };

  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, tests);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(vars->GetValue() == std::to_string(tests.size()));
  ASSERT_TRUE(variables.size() == 2);
  ASSERT_TRUE(variables[0].name == test1.GetName());
  ASSERT_TRUE(variables[0].value == "");
  ASSERT_TRUE(variables[0].type.value() == "collection");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[0].variablesReference != 0);

  ASSERT_TRUE(variables[1].name == test2.GetName());
  ASSERT_TRUE(variables[1].value == "");
  ASSERT_TRUE(variables[1].type.value() == "collection");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[0].variablesReference != 0);

  dap::array<dap::Variable> testVariables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(variables[0].variablesReference));
  ASSERT_TRUE(testVariables.size() == 5);
  ASSERT_TRUE(testVariables[0].name == "Command");
  ASSERT_TRUE(testVariables[0].value ==
              std::to_string(test1.GetCommand().size()));
  ASSERT_TRUE(testVariables[0].type.value() == "collection");
  ASSERT_TRUE(testVariables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[0].variablesReference != 0);
  ASSERT_TRUE(testVariables[1].name == "CommandExpandLists");
  ASSERT_TRUE(testVariables[1].value ==
              BOOL_STRING(test1.GetCommandExpandLists()));
  ASSERT_TRUE(testVariables[1].type.value() == "bool");
  ASSERT_TRUE(testVariables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[2].name == "Name");
  ASSERT_TRUE(testVariables[2].value == test1.GetName());
  ASSERT_TRUE(testVariables[2].type.value() == "string");
  ASSERT_TRUE(testVariables[2].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[3].name == "OldStyle");
  ASSERT_TRUE(testVariables[3].value == BOOL_STRING(test1.GetOldStyle()));
  ASSERT_TRUE(testVariables[3].type.value() == "bool");
  ASSERT_TRUE(testVariables[3].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[4].name == "Properties");
  ASSERT_TRUE(testVariables[4].value == "1");
  ASSERT_TRUE(testVariables[4].type.value() == "collection");
  ASSERT_TRUE(testVariables[4].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[4].variablesReference != 0);

  dap::array<dap::Variable> commandVariables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(testVariables[0].variablesReference));
  ASSERT_TRUE(commandVariables.size() == test1.GetCommand().size());
  for (int i = 0; i < commandVariables.size(); ++i) {
    ASSERT_TRUE(commandVariables[i].name == "[" + std::to_string(i) + "]");
    ASSERT_TRUE(commandVariables[i].value == test1.GetCommand()[i]);
    ASSERT_TRUE(commandVariables[i].type.value() == "string");
    ASSERT_TRUE(commandVariables[i].evaluateName.has_value() == false);
  }

  dap::array<dap::Variable> propertiesVariables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(testVariables[4].variablesReference));
  ASSERT_TRUE(propertiesVariables.size() == 1);
  ASSERT_TRUE(propertiesVariables[0].name == "Prop1");
  ASSERT_TRUE(propertiesVariables[0].value == "Prop1");
  ASSERT_TRUE(propertiesVariables[0].type.value() == "string");
  ASSERT_TRUE(propertiesVariables[0].evaluateName.has_value() == false);

  testVariables = variablesManager->HandleVariablesRequest(
    CreateVariablesRequest(variables[1].variablesReference));
  ASSERT_TRUE(testVariables.size() == 5);
  ASSERT_TRUE(testVariables[0].name == "Command");
  ASSERT_TRUE(testVariables[0].value ==
              std::to_string(test2.GetCommand().size()));
  ASSERT_TRUE(testVariables[0].type.value() == "collection");
  ASSERT_TRUE(testVariables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[0].variablesReference != 0);
  ASSERT_TRUE(testVariables[1].name == "CommandExpandLists");
  ASSERT_TRUE(testVariables[1].value ==
              BOOL_STRING(test2.GetCommandExpandLists()));
  ASSERT_TRUE(testVariables[1].type.value() == "bool");
  ASSERT_TRUE(testVariables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[2].name == "Name");
  ASSERT_TRUE(testVariables[2].value == test2.GetName());
  ASSERT_TRUE(testVariables[2].type.value() == "string");
  ASSERT_TRUE(testVariables[2].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[3].name == "OldStyle");
  ASSERT_TRUE(testVariables[3].value == BOOL_STRING(test2.GetOldStyle()));
  ASSERT_TRUE(testVariables[3].type.value() == "bool");
  ASSERT_TRUE(testVariables[3].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[4].name == "Properties");
  ASSERT_TRUE(testVariables[4].value == "2");
  ASSERT_TRUE(testVariables[4].type.value() == "collection");
  ASSERT_TRUE(testVariables[4].evaluateName.has_value() == false);
  ASSERT_TRUE(testVariables[4].variablesReference != 0);

  commandVariables = variablesManager->HandleVariablesRequest(
    CreateVariablesRequest(testVariables[0].variablesReference));
  ASSERT_TRUE(commandVariables.size() == test2.GetCommand().size());
  for (int i = 0; i < commandVariables.size(); ++i) {
    ASSERT_TRUE(commandVariables[i].name == "[" + std::to_string(i) + "]");
    ASSERT_TRUE(commandVariables[i].value == test2.GetCommand()[i]);
    ASSERT_TRUE(commandVariables[i].type.value() == "string");
    ASSERT_TRUE(commandVariables[i].evaluateName.has_value() == false);
  }

  propertiesVariables = variablesManager->HandleVariablesRequest(
    CreateVariablesRequest(testVariables[4].variablesReference));
  ASSERT_TRUE(propertiesVariables.size() == 2);
  ASSERT_TRUE(propertiesVariables[0].name == "Prop2");
  ASSERT_TRUE(propertiesVariables[0].value == "Prop2");
  ASSERT_TRUE(propertiesVariables[0].type.value() == "string");
  ASSERT_TRUE(propertiesVariables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(propertiesVariables[1].name == "Prop3");
  ASSERT_TRUE(propertiesVariables[1].value == "Prop3");
  ASSERT_TRUE(propertiesVariables[1].type.value() == "string");
  ASSERT_TRUE(propertiesVariables[1].evaluateName.has_value() == false);

  auto none = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, std::vector<cmTest*>());

  ASSERT_TRUE(none == nullptr);

  return true;
}

static bool testCreateFromMakefile()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  auto dummies = CreateDummies("Foo");
  auto snapshot = dummies.Makefile->GetStateSnapshot();
  auto state = dummies.Makefile->GetState();
  state->SetSourceDirectory("c:/HomeDirectory");
  state->SetBinaryDirectory("c:/HomeOutputDirectory");
  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, dummies.Makefile.get());

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(variables.size() == 12);
  ASSERT_TRUE(variables[0].name == "AppleSDKType");
  ASSERT_TRUE(variables[0].value == "MacOS");
  ASSERT_TRUE(variables[0].type.value() == "string");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].name == "CurrentBinaryDirectory");
  ASSERT_TRUE(variables[1].value ==
              snapshot.GetDirectory().GetCurrentBinary());
  ASSERT_TRUE(variables[1].type.value() == "string");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[2].name == "CurrentSourceDirectory");
  ASSERT_TRUE(variables[2].value ==
              snapshot.GetDirectory().GetCurrentSource());
  ASSERT_TRUE(variables[2].type.value() == "string");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[3].name == "DefineFlags");
  ASSERT_TRUE(variables[3].value == " ");
  ASSERT_TRUE(variables[3].type.value() == "string");
  ASSERT_TRUE(variables[3].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[4].name == "DirectoryId");
  ASSERT_TRUE(!variables[4].value.empty());
  ASSERT_TRUE(variables[4].type.value() == "string");
  ASSERT_TRUE(variables[4].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[5].name == "HomeDirectory");
  ASSERT_TRUE(variables[5].value == state->GetSourceDirectory());
  ASSERT_TRUE(variables[5].type.value() == "string");
  ASSERT_TRUE(variables[5].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[6].name == "HomeOutputDirectory");
  ASSERT_TRUE(variables[6].value == state->GetBinaryDirectory());
  ASSERT_TRUE(variables[6].type.value() == "string");
  ASSERT_TRUE(variables[6].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[7].name == "IsRootMakefile");
  ASSERT_TRUE(variables[7].value == "TRUE");
  ASSERT_TRUE(variables[7].type.value() == "bool");
  ASSERT_TRUE(variables[7].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[8].name == "PlatformIs32Bit");
  ASSERT_TRUE(variables[8].value == "FALSE");
  ASSERT_TRUE(variables[8].type.value() == "bool");
  ASSERT_TRUE(variables[8].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[9].name == "PlatformIs64Bit");
  ASSERT_TRUE(variables[9].value == "FALSE");
  ASSERT_TRUE(variables[9].type.value() == "bool");
  ASSERT_TRUE(variables[9].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[10].name == "PlatformIsAppleEmbedded");
  ASSERT_TRUE(variables[10].value == "FALSE");
  ASSERT_TRUE(variables[10].type.value() == "bool");
  ASSERT_TRUE(variables[10].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[11].name == "PlatformIsx32");
  ASSERT_TRUE(variables[11].value == "FALSE");
  ASSERT_TRUE(variables[11].type.value() == "bool");
  ASSERT_TRUE(variables[11].evaluateName.has_value() == false);

  auto none = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, static_cast<cmMakefile*>(nullptr));

  ASSERT_TRUE(none == nullptr);

  return true;
}

static bool testCreateFromStackFrame()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();
  auto dummies = CreateDummies("Foo");

  cmListFileFunction lff = cmListFileFunction("set", 99, 99, {});
  auto frame = std::make_shared<cmDebugger::cmDebuggerStackFrame>(
    dummies.Makefile.get(), "c:/CMakeLists.txt", lff);

  auto locals = cmDebugger::cmDebuggerVariablesHelper::Create(
    variablesManager, "Locals", true, frame);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(locals->GetId()));

  ASSERT_TRUE(variables.size() == 3);
  ASSERT_TRUE(variables[0].name == "CacheVariables");
  ASSERT_TRUE(variables[0].value == "2");
  ASSERT_TRUE(variables[0].type.value() == "collection");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].name == "CurrentLine");
  ASSERT_TRUE(variables[1].value == std::to_string(lff.Line()));
  ASSERT_TRUE(variables[1].type.value() == "int");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[2].name == "Targets");
  ASSERT_TRUE(variables[2].value == "1");
  ASSERT_TRUE(variables[2].type.value() == "collection");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);
  return true;
}

static bool testCreateFromBTStringVector()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  std::vector<BT<std::string>> list(2);
  list[0].Value = "Foo";
  list[1].Value = "Bar";

  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, list);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(vars->GetValue() == std::to_string(list.size()));
  ASSERT_TRUE(variables.size() == 2);
  ASSERT_TRUE(variables[0].name == "[0]");
  ASSERT_TRUE(variables[0].value == "Foo");
  ASSERT_TRUE(variables[0].type.value() == "string");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);

  ASSERT_TRUE(variables[1].name == "[1]");
  ASSERT_TRUE(variables[1].value == "Bar");
  ASSERT_TRUE(variables[1].type.value() == "string");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);

  auto none = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, std::vector<std::string>());

  ASSERT_TRUE(none == nullptr);

  return true;
}

static bool testCreateFromFileSet()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  cmFileSet fileSet("Foo", "HEADERS", cmFileSetVisibility::Public);
  BT<std::string> directory;
  directory.Value = "c:/";
  fileSet.AddDirectoryEntry(directory);
  BT<std::string> file;
  file.Value = "c:/foo.cxx";
  fileSet.AddFileEntry(file);

  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, &fileSet);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(variables.size() == 5);
  ASSERT_TRUE(variables[0].name == "Directories");
  ASSERT_TRUE(variables[0].value == "1");
  ASSERT_TRUE(variables[0].type.value() == "collection");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[0].variablesReference != 0);
  ASSERT_TRUE(variables[1].name == "Files");
  ASSERT_TRUE(variables[1].value == "1");
  ASSERT_TRUE(variables[1].type.value() == "collection");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].variablesReference != 0);
  ASSERT_TRUE(variables[2].name == "Name");
  ASSERT_TRUE(variables[2].value == "Foo");
  ASSERT_TRUE(variables[2].type.value() == "string");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[3].name == "Type");
  ASSERT_TRUE(variables[3].value == "HEADERS");
  ASSERT_TRUE(variables[3].type.value() == "string");
  ASSERT_TRUE(variables[3].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[4].name == "Visibility");
  ASSERT_TRUE(variables[4].value == "Public");
  ASSERT_TRUE(variables[4].type.value() == "string");
  ASSERT_TRUE(variables[4].evaluateName.has_value() == false);

  dap::array<dap::Variable> directoriesVariables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(variables[0].variablesReference));
  ASSERT_TRUE(directoriesVariables.size() == 1);
  ASSERT_TRUE(directoriesVariables[0].name == "[0]");
  ASSERT_TRUE(directoriesVariables[0].value == directory.Value);
  ASSERT_TRUE(directoriesVariables[0].type.value() == "string");
  ASSERT_TRUE(directoriesVariables[0].evaluateName.has_value() == false);

  dap::array<dap::Variable> filesVariables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(variables[1].variablesReference));
  ASSERT_TRUE(filesVariables.size() == 1);
  ASSERT_TRUE(filesVariables[0].name == "[0]");
  ASSERT_TRUE(filesVariables[0].value == file.Value);
  ASSERT_TRUE(filesVariables[0].type.value() == "string");
  ASSERT_TRUE(filesVariables[0].evaluateName.has_value() == false);

  return true;
}

static bool testCreateFromFileSets()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  cmFileSet fileSet("Foo", "HEADERS", cmFileSetVisibility::Public);
  BT<std::string> directory;
  directory.Value = "c:/";
  fileSet.AddDirectoryEntry(directory);
  BT<std::string> file;
  file.Value = "c:/foo.cxx";
  fileSet.AddFileEntry(file);

  auto fileSets = std::vector<cmFileSet*>{ &fileSet };
  auto vars = cmDebugger::cmDebuggerVariablesHelper::CreateIfAny(
    variablesManager, "Locals", true, fileSets);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));

  ASSERT_TRUE(variables.size() == 1);
  ASSERT_TRUE(variables[0].name == "Foo");
  ASSERT_TRUE(variables[0].value == "");
  ASSERT_TRUE(variables[0].type.value() == "collection");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[0].variablesReference != 0);

  return true;
}

int testCMDebuggerVariablesHelper(int, char*[])
{
  return runTests(std::vector<std::function<bool()>>{
    testCreateFromPolicyMap,
    testCreateFromPairVector,
    testCreateFromSet,
    testCreateFromStringVector,
    testCreateFromTarget,
    testCreateFromGlobalGenerator,
    testCreateFromMakefile,
    testCreateFromStackFrame,
    testCreateFromTests,
    testCreateFromBTStringVector,
    testCreateFromFileSet,
    testCreateFromFileSets,
  });
}
