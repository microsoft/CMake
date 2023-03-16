/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include "cmDebuggerVariables.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <optional>
#include <vector>

#include "cmDebuggerStackFrame.h"
#include "cmDebuggerVariablesManager.h"

namespace cmDebugger {

namespace {
static const dap::VariablePresentationHint PrivatePropertyHint = { {},
                                                                   "property",
                                                                   {},
                                                                   "private" };
static const dap::VariablePresentationHint PrivateDataHint = { {},
                                                               "data",
                                                               {},
                                                               "private" };
}

std::atomic<int64_t> cmDebuggerVariables::NextId = 1;

cmDebuggerVariables::cmDebuggerVariables(
  std::shared_ptr<cmDebuggerVariablesManager> const& variablesManager,
  std::string name, bool supportsVariableType)
  : Id(NextId.fetch_add(1))
  , Name(name)
  , SupportsVariableType(supportsVariableType)
  , VariablesManager(variablesManager)
{
  VariablesManager->RegisterHandler(
    Id, [this](dap::VariablesRequest const& request) {
      return this->HandleVariablesRequest(request);
    });
}

cmDebuggerVariables::cmDebuggerVariables(
  std::shared_ptr<cmDebuggerVariablesManager> const& variablesManager,
  std::string name, bool supportsVariableType,
  std::function<std::vector<cmDebuggerVariableEntry>()> getKeyValuesFunction)
  : Id(NextId.fetch_add(1))
  , Name(name)
  , SupportsVariableType(supportsVariableType)
  , VariablesManager(variablesManager)
  , GetKeyValuesFunction(getKeyValuesFunction)
{
  VariablesManager->RegisterHandler(
    Id, [this](dap::VariablesRequest const& request) {
      return this->HandleVariablesRequest(request);
    });
}

void cmDebuggerVariables::AddSubVariables(
  std::shared_ptr<cmDebuggerVariables> const& variables)
{
  if (variables != nullptr) {
    SubVariables.emplace_back(variables);
  }
}

dap::array<dap::Variable> cmDebuggerVariables::HandleVariablesRequest(
  dap::VariablesRequest const& request)
{
  dap::array<dap::Variable> variables;

  if (GetKeyValuesFunction != nullptr) {
    auto values = GetKeyValuesFunction();
    for (auto const& entry : values) {
      if (IgnoreEmptyStringEntries && entry.Type == "string" &&
          entry.Value.empty()) {
        continue;
      }
      variables.push_back(dap::Variable{ {},
                                         {},
                                         {},
                                         entry.Name,
                                         {},
                                         PrivateDataHint,
                                         entry.Type,
                                         entry.Value,
                                         0 });
    }
  }

  EnumerateSubVariablesIfAny(variables);

  if (EnableSorting) {
    std::sort(variables.begin(), variables.end(),
              [](dap::Variable const& a, dap::Variable const& b) {
                return a.name < b.name;
              });
  }
  return variables;
}

void cmDebuggerVariables::EnumerateSubVariablesIfAny(
  dap::array<dap::Variable>& toBeReturned) const
{
  dap::array<dap::Variable> ret;
  for (auto const& variables : SubVariables) {
    toBeReturned.emplace_back(
      dap::Variable{ {},
                     {},
                     {},
                     variables->GetName(),
                     {},
                     PrivatePropertyHint,
                     SupportsVariableType ? "collection" : nullptr,
                     variables->GetValue(),
                     variables->GetId() });
  }
}

void cmDebuggerVariables::ClearSubVariables()
{
  SubVariables.clear();
}

cmDebuggerVariables::~cmDebuggerVariables()
{
  ClearSubVariables();
  VariablesManager->UnregisterHandler(Id);
}

} // namespace cmDebugger
