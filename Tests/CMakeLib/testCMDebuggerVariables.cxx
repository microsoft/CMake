/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include <iostream>
#include <unordered_set>

#include "cmDebuggerVariables.h"

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

int testCMDebuggerVariables(int, char*[])
{
  int failed = 0;
  std::unordered_set<int64_t> variableIds;
  bool noDuplicateIds = true;
  for (int i = 0; i < 100 && noDuplicateIds; ++i) {
    auto variable = cmDebugger::cmDebuggerVariablesLocal(
      true, [=]() { return i; }, i);
    if (variableIds.find(variable.GetId()) != variableIds.end()) {
      noDuplicateIds = false;
    }
    variableIds.insert(variable.GetId());
  }

  cmAssert(noDuplicateIds,
           "cmDebuggerVariables generates unique IDs.");
  dap::VariablesRequest variableRequest;
  {
    int64_t expectedLine = 5;
    int64_t expectedCacheVariableReference = 999;
    auto local = cmDebugger::cmDebuggerVariablesLocal(
      true, [=]() { return expectedLine; }, expectedCacheVariableReference);

    dap::array<dap::Variable> variables = local.GetVariables(variableRequest);
    cmAssert(variables.size() == 2,
             "cmDebuggerVariablesLocal returns expected variable array size.");
    cmAssert(variables[0].name == "CurrentLine",
             "cmDebuggerVariablesLocal returns expected variable[0].name.");
    cmAssert(variables[0].value == std::to_string(expectedLine),
             "cmDebuggerVariablesLocal returns expected variable[0].value.");
    cmAssert(variables[0].type.value() == "int",
             "cmDebuggerVariablesLocal returns expected variable[0].type.");
    cmAssert(
      variables[0].evaluateName.has_value() == false,
      "cmDebuggerVariablesLocal returns expected variable[0].evaluateName.");

    cmAssert(variables[1].name == "CMAKE CACHE VARIABLES",
             "cmDebuggerVariablesLocal returns expected variable[1].name.");
    cmAssert(variables[1].value == "",
             "cmDebuggerVariablesLocal returns expected variable[1].value.");
    cmAssert(variables[1].type.value() == "collection",
             "cmDebuggerVariablesLocal returns expected variable[1].type.");
    cmAssert(
      variables[1].evaluateName.has_value() == false,
      "cmDebuggerVariablesLocal returns expected variable[1].evaluateName.");
  }

  {
    std::vector<std::string> expectedKeys = { "foo", "bar" };
    std::unordered_map<std::string, cmValue> expectedCache = {
      { "foo", cmValue("1234") },
      { "bar", cmValue("9876") },
    };
    auto cache = cmDebugger::cmDebuggerVariablesCache(
      true, [=]() { return expectedKeys; },
      [=](std::string const& key) { return expectedCache.at(key); });

    dap::array<dap::Variable> sortedVariables = cache.GetVariables(variableRequest);
    cmAssert(sortedVariables.size() == 2,
             "cmDebuggerVariablesCache returns expected variable array size.");
    cmAssert(sortedVariables[0].name == expectedKeys[1],
             "cmDebuggerVariablesCache returns expected variables[0].name.");
    cmAssert(sortedVariables[0].value ==
               *expectedCache.at(expectedKeys[1]).Get(),
             "cmDebuggerVariablesCache returns expected variables[0].value.");
    cmAssert(sortedVariables[0].type.value() == "string",
             "cmDebuggerVariablesCache returns expected variables[0].type.");
    cmAssert(
      sortedVariables[0].evaluateName.value() == expectedKeys[1],
      "cmDebuggerVariablesCache returns expected variables[0].evaluateName.");

    cmAssert(sortedVariables[1].name == expectedKeys[0],
             "cmDebuggerVariablesCache returns expected variables[1].name.");
    cmAssert(sortedVariables[1].value ==
               *expectedCache.at(expectedKeys[0]).Get(),
             "cmDebuggerVariablesCache returns expected variables[1].value.");
    cmAssert(sortedVariables[1].type.value() == "string",
             "cmDebuggerVariablesCache returns expected variables[1].type.");
    cmAssert(
      sortedVariables[1].evaluateName.value() == expectedKeys[0],
      "cmDebuggerVariablesCache returns expected variables[1].evaluateName.");
  }

  return failed;
}
