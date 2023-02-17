/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include <iostream>
#include <string>
#include <unordered_set>

#include "cmDebuggerStackFrame.h"
#include "cmDebuggerVariables.h"
#include "cmDebuggerVariablesManager.h"

#include "testCommon.h"

static dap::VariablesRequest CreateVariablesRequest(int64_t reference)
{
  dap::VariablesRequest variableRequest;
  variableRequest.variablesReference = reference;
  return variableRequest;
}

static bool testUniqueIds()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  std::unordered_set<int64_t> variableIds;
  bool noDuplicateIds = true;
  for (int i = 0; i < 10000 && noDuplicateIds; ++i) {
    auto variable =
      cmDebugger::cmDebuggerVariables(variablesManager, "Locals", true, []() {
        return std::vector<cmDebugger::cmDebuggerVariableEntry>();
      });

    if (variableIds.find(variable.GetId()) != variableIds.end()) {
      noDuplicateIds = false;
    }
    variableIds.insert(variable.GetId());
  }

  ASSERT_TRUE(noDuplicateIds);

  return true;
}

static bool testConstructors()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  auto parent = std::make_shared<cmDebugger::cmDebuggerVariables>(
    variablesManager, "Parent", true, [=]() {
      return std::vector<cmDebugger::cmDebuggerVariableEntry>{
        { "ParentKey", "ParentValue", "ParentType" }
      };
    });

  auto children1 = std::make_shared<cmDebugger::cmDebuggerVariables>(
    variablesManager, "Children1", true, [=]() {
      return std::vector<cmDebugger::cmDebuggerVariableEntry>{
        { "ChildKey1", "ChildValue1", "ChildType1" },
        { "ChildKey2", "ChildValue2", "ChildType2" }
      };
    });

  parent->AddSubVariables(children1);

  auto children2 = std::make_shared<cmDebugger::cmDebuggerVariables>(
    variablesManager, "Children2", true);

  auto grandChildren21 = std::make_shared<cmDebugger::cmDebuggerVariables>(
    variablesManager, "GrandChildren21", true);
  grandChildren21->SetValue("GrandChildren21 Value");
  children2->AddSubVariables(grandChildren21);
  parent->AddSubVariables(children2);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(parent->GetId()));
  ASSERT_TRUE(variables.size() == 3);
  ASSERT_TRUE(variables[0].name == "Children1");
  ASSERT_TRUE(variables[0].value == "");
  ASSERT_TRUE(variables[0].type.value() == "collection");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[0].variablesReference == children1->GetId());
  ASSERT_TRUE(variables[1].name == "Children2");
  ASSERT_TRUE(variables[1].value == "");
  ASSERT_TRUE(variables[1].type.value() == "collection");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].variablesReference == children2->GetId());
  ASSERT_TRUE(variables[2].name == "ParentKey");
  ASSERT_TRUE(variables[2].value == "ParentValue");
  ASSERT_TRUE(variables[2].type.value() == "ParentType");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);

  variables = variablesManager->HandleVariablesRequest(
    CreateVariablesRequest(children1->GetId()));
  ASSERT_TRUE(variables.size() == 2);
  ASSERT_TRUE(variables[0].name == "ChildKey1");
  ASSERT_TRUE(variables[0].value == "ChildValue1");
  ASSERT_TRUE(variables[0].type.value() == "ChildType1");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].name == "ChildKey2");
  ASSERT_TRUE(variables[1].value == "ChildValue2");
  ASSERT_TRUE(variables[1].type.value() == "ChildType2");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);

  variables = variablesManager->HandleVariablesRequest(
    CreateVariablesRequest(children2->GetId()));
  ASSERT_TRUE(variables.size() == 1);
  ASSERT_TRUE(variables[0].name == "GrandChildren21");
  ASSERT_TRUE(variables[0].value == "GrandChildren21 Value");
  ASSERT_TRUE(variables[0].type.value() == "collection");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[0].variablesReference == grandChildren21->GetId());

  return true;
}

static bool testIgnoreEmptyStringEntries()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  auto vars = std::make_shared<cmDebugger::cmDebuggerVariables>(
    variablesManager, "Variables", true, []() {
      return std::vector<cmDebugger::cmDebuggerVariableEntry>{
        { "IntValue1", 5 },           { "StringValue1", "" },
        { "StringValue2", "foo" },    { "StringValue3", "" },
        { "StringValue4", "bar" },    { "StringValue5", "" },
        { "IntValue2", int64_t(99) }, { "BooleanTrue", true },
        { "BooleanFalse", false },
      };
    });

  vars->SetIgnoreEmptyStringEntries(true);
  vars->SetEnableSorting(false);

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));
  ASSERT_TRUE(variables.size() == 6);
  ASSERT_TRUE(variables[0].name == "IntValue1");
  ASSERT_TRUE(variables[0].value == "5");
  ASSERT_TRUE(variables[0].type.value() == "int");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].name == "StringValue2");
  ASSERT_TRUE(variables[1].value == "foo");
  ASSERT_TRUE(variables[1].type.value() == "string");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[2].name == "StringValue4");
  ASSERT_TRUE(variables[2].value == "bar");
  ASSERT_TRUE(variables[2].type.value() == "string");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[3].name == "IntValue2");
  ASSERT_TRUE(variables[3].value == "99");
  ASSERT_TRUE(variables[3].type.value() == "int");
  ASSERT_TRUE(variables[3].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[4].name == "BooleanTrue");
  ASSERT_TRUE(variables[4].value == "TRUE");
  ASSERT_TRUE(variables[4].type.value() == "bool");
  ASSERT_TRUE(variables[4].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[5].name == "BooleanFalse");
  ASSERT_TRUE(variables[5].value == "FALSE");
  ASSERT_TRUE(variables[5].type.value() == "bool");
  ASSERT_TRUE(variables[5].evaluateName.has_value() == false);

  return true;
}

static bool testSortTheResult()
{
  auto variablesManager =
    std::make_shared<cmDebugger::cmDebuggerVariablesManager>();

  auto vars = std::make_shared<cmDebugger::cmDebuggerVariables>(
    variablesManager, "Variables", true, []() {
      return std::vector<cmDebugger::cmDebuggerVariableEntry>{
        { "4", "4" }, { "2", "2" }, { "1", "1" }, { "3", "3" }, { "5", "5" },
      };
    });

  dap::array<dap::Variable> variables =
    variablesManager->HandleVariablesRequest(
      CreateVariablesRequest(vars->GetId()));
  ASSERT_TRUE(variables.size() == 5);
  ASSERT_TRUE(variables[0].name == "1");
  ASSERT_TRUE(variables[0].value == "1");
  ASSERT_TRUE(variables[0].type.value() == "string");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].name == "2");
  ASSERT_TRUE(variables[1].value == "2");
  ASSERT_TRUE(variables[1].type.value() == "string");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[2].name == "3");
  ASSERT_TRUE(variables[2].value == "3");
  ASSERT_TRUE(variables[2].type.value() == "string");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[3].name == "4");
  ASSERT_TRUE(variables[3].value == "4");
  ASSERT_TRUE(variables[3].type.value() == "string");
  ASSERT_TRUE(variables[3].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[4].name == "5");
  ASSERT_TRUE(variables[4].value == "5");
  ASSERT_TRUE(variables[4].type.value() == "string");
  ASSERT_TRUE(variables[4].evaluateName.has_value() == false);

  vars->SetEnableSorting(false);

  variables = variablesManager->HandleVariablesRequest(
    CreateVariablesRequest(vars->GetId()));
  ASSERT_TRUE(variables.size() == 5);
  ASSERT_TRUE(variables[0].name == "4");
  ASSERT_TRUE(variables[0].value == "4");
  ASSERT_TRUE(variables[0].type.value() == "string");
  ASSERT_TRUE(variables[0].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[1].name == "2");
  ASSERT_TRUE(variables[1].value == "2");
  ASSERT_TRUE(variables[1].type.value() == "string");
  ASSERT_TRUE(variables[1].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[2].name == "1");
  ASSERT_TRUE(variables[2].value == "1");
  ASSERT_TRUE(variables[2].type.value() == "string");
  ASSERT_TRUE(variables[2].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[3].name == "3");
  ASSERT_TRUE(variables[3].value == "3");
  ASSERT_TRUE(variables[3].type.value() == "string");
  ASSERT_TRUE(variables[3].evaluateName.has_value() == false);
  ASSERT_TRUE(variables[4].name == "5");
  ASSERT_TRUE(variables[4].value == "5");
  ASSERT_TRUE(variables[4].type.value() == "string");
  ASSERT_TRUE(variables[4].evaluateName.has_value() == false);

  return true;
}

int testCMDebuggerVariables(int, char*[])
{
  return runTests(std::vector<std::function<bool()>>{
    testUniqueIds,
    testConstructors,
    testIgnoreEmptyStringEntries,
    testSortTheResult,
  });
}
