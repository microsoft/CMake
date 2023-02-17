/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include "cmDebuggerVariablesManager.h"

#include <algorithm>
#include <optional>

#include "cmDebuggerVariables.h"

namespace cmDebugger {

cmDebuggerVariablesManager::cmDebuggerVariablesManager()
{
}

void cmDebuggerVariablesManager::RegisterHandler(
  int64_t id,
  std::function<dap::array<dap::Variable>(dap::VariablesRequest const&)>
    handler)
{
  VariablesHandlers[id] = handler;
}

void cmDebuggerVariablesManager::UnregisterHandler(int64_t id)
{
  VariablesHandlers.erase(id);
}

dap::array<dap::Variable> cmDebuggerVariablesManager::HandleVariablesRequest(
  dap::VariablesRequest const& request)
{
  auto it = VariablesHandlers.find(request.variablesReference);

  if (it != VariablesHandlers.end()) {
    return it->second(request);
  }

  return dap::array<dap::Variable>();
}

} // namespace cmDebugger
