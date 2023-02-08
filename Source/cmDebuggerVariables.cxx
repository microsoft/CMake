/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include <algorithm>
#include <optional>

#include "cmDebuggerStackFrame.h"
#include "cmDebuggerVariables.h"

namespace cmDebugger {

namespace {
static const dap::VariablePresentationHint PublicPropertyHint = { {},
                                                                  "property",
                                                                  {},
                                                                  "public" };

dap::Variable CreateVariable(
  std::string const& name, std::string const& value, std::string const& type,
  bool supportsVariableType, std::optional<std::string> evaluateName = {},
  std::optional<dap::integer> variablesReference = {},
  std::optional<dap::VariablePresentationHint> presentationHint = {})
{
  dap::Variable variable;
  variable.name = name;
  variable.value = value;
  if (supportsVariableType) {
    variable.type = type;
  }
  if (evaluateName.has_value()) {
    variable.evaluateName = evaluateName.value();
  }
  if (variablesReference.has_value()) {
    variable.variablesReference = variablesReference.value();
  }
  if (presentationHint.has_value()) {
    variable.presentationHint = presentationHint.value();
  }
  return variable;
}
}

std::atomic<int64_t> cmDebuggerVariables::NextId = 1;

cmDebuggerVariables::cmDebuggerVariables(bool supportsVariableType)
  : Id(NextId.fetch_add(1))
  , SupportsVariableType(supportsVariableType)
{
}



cmDebuggerVariablesLocal::cmDebuggerVariablesLocal(
  bool supportsVariableType,
  std::function<int64_t()> const& getLine,
  int64_t cacheVariableReference)
  : cmDebuggerVariables(supportsVariableType)
  , GetLine(getLine)
  , CacheVariableReference(cacheVariableReference)
{
}

dap::array<dap::Variable> cmDebuggerVariablesLocal::GetVariables(
  dap::VariablesRequest const& request)
{
  dap::array<dap::Variable> variables;
  variables.push_back(CreateVariable(
    "CurrentLine",
    std::to_string(GetLine()), "int",
    SupportsVariableType));

  variables.push_back(CreateVariable(
    "CMAKE CACHE VARIABLES", "", "collection", SupportsVariableType, {},
    std::make_optional(CacheVariableReference), PublicPropertyHint));

  return variables;
}

cmDebuggerVariablesCache::cmDebuggerVariablesCache(
  bool supportsVariableType,
  std::function<std::vector<std::string>()> getKeys,
  std::function<cmValue(std::string const&)> getDefinition)
  : cmDebuggerVariables(supportsVariableType)
  , GetKeys(getKeys)
  , GetDefinition(getDefinition)
{
}

dap::array<dap::Variable> cmDebuggerVariablesCache::GetVariables(
  dap::VariablesRequest const& request)
{
  std::vector<std::string> keys = GetKeys();
  std::sort(keys.begin(), keys.end());

  dap::array<dap::Variable> variables;
  for (auto& varStr : keys) {
    auto val = GetDefinition(varStr);
    if (val) {
      variables.push_back(CreateVariable(varStr, *val, "string",
                                         SupportsVariableType,
                                         std::make_optional(varStr)));
    }
  }

  return variables;
}

} // namespace cmDebugger
