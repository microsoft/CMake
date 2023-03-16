/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <atomic>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <cm3p/cppdap/protocol.h>

namespace cmDebugger {
class cmDebuggerStackFrame;
class cmDebuggerVariablesManager;

struct cmDebuggerVariableEntry
{
  cmDebuggerVariableEntry()
    : cmDebuggerVariableEntry("", "", "")
  {
  }
  cmDebuggerVariableEntry(std::string name, std::string value,
                          std::string type)
    : Name(name)
    , Value(value)
    , Type(type)
  {
  }
  cmDebuggerVariableEntry(std::string name, std::string value)
    : Name(name)
    , Value(value)
    , Type("string")
  {
  }
  cmDebuggerVariableEntry(std::string name, const char* value)
    : Name(name)
    , Value(value == nullptr ? "" : value)
    , Type("string")
  {
  }
  cmDebuggerVariableEntry(std::string name, bool value)
    : Name(name)
    , Value(value ? "TRUE" : "FALSE")
    , Type("bool")
  {
  }
  cmDebuggerVariableEntry(std::string name, int64_t value)
    : Name(name)
    , Value(std::to_string(value))
    , Type("int")
  {
  }
  cmDebuggerVariableEntry(std::string name, int value)
    : Name(name)
    , Value(std::to_string(value))
    , Type("int")
  {
  }
  std::string const Name;
  std::string const Value;
  std::string const Type;
};

class cmDebuggerVariables
{
  static std::atomic<int64_t> NextId;
  int64_t Id;
  std::string Name;
  std::string Value;

  std::function<std::vector<cmDebuggerVariableEntry>()> GetKeyValuesFunction;
  std::vector<std::shared_ptr<cmDebuggerVariables>> SubVariables;
  bool IgnoreEmptyStringEntries = false;
  bool EnableSorting = true;

  virtual dap::array<dap::Variable> HandleVariablesRequest(
    dap::VariablesRequest const& request);
  friend class cmDebuggerVariablesManager;

protected:
  const bool SupportsVariableType;
  std::shared_ptr<cmDebuggerVariablesManager> VariablesManager;
  void EnumerateSubVariablesIfAny(
    dap::array<dap::Variable>& toBeReturned) const;
  void ClearSubVariables();

public:
  cmDebuggerVariables(
    std::shared_ptr<cmDebuggerVariablesManager> const& variablesManager,
    std::string name, bool supportsVariableType);
  cmDebuggerVariables(
    std::shared_ptr<cmDebuggerVariablesManager> const& variablesManager,
    std::string name, bool supportsVariableType,
    std::function<std::vector<cmDebuggerVariableEntry>()> getKeyValuesFunc);
  inline int64_t GetId() const noexcept { return this->Id; }
  inline std::string GetName() const noexcept { return this->Name; }
  inline std::string GetValue() const noexcept { return this->Value; }
  inline void SetValue(std::string const& value) noexcept
  {
    this->Value = value;
  }
  void AddSubVariables(std::shared_ptr<cmDebuggerVariables> const& variables);
  inline void SetIgnoreEmptyStringEntries(bool value) noexcept
  {
    this->IgnoreEmptyStringEntries = value;
  }
  inline void SetEnableSorting(bool value) noexcept
  {
    this->EnableSorting = value;
  }
  virtual ~cmDebuggerVariables();
};

} // namespace cmDebugger
