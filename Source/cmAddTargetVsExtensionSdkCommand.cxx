/*============================================================================
CMake - Cross Platform Makefile Generator
Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

Distributed under the OSI-approved BSD License (the "License");
see accompanying file Copyright.txt for details.

This software is distributed WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the License for more information.
============================================================================*/
#include "cmAddTargetVsExtensionSdkCommand.h"

#include <sstream>

//----------------------------------------------------------------------------
bool cmAddTargetVsExtensionSdkCommand
::InitialPass(std::vector<std::string> const& args, cmExecutionStatus &)
{
    // only compile this for win32 to avoid coverage errors
#ifdef _WIN32
    if (this->Makefile->GetDefinition("WIN32"))
    {
        return this->HandleArguments(args);
    }
#endif
    return true;
}

bool cmAddTargetVsExtensionSdkCommand
::HandleArguments(std::vector<std::string> const& args)
{
    if (args.size() != 3)
    {
        this->SetError("called with incorrect number of arguments");
        return false;
    }
    const std::string &targetName = args[0];

    if (this->Makefile->IsAlias(targetName))
    {
        this->SetError("can not be used on an ALIAS target.");
        return false;
    }
    cmTarget *target =
        this->Makefile->GetCMakeInstance()
        ->GetGlobalGenerator()->FindTarget(targetName);
    if (!target)
    {
        target = this->Makefile->FindTargetToUse(targetName);
    }
    if (!target)
    {
        this->HandleMissingTarget(targetName);
        return false;
    }
    if ((target->GetType() != cmTarget::SHARED_LIBRARY)
        && (target->GetType() != cmTarget::STATIC_LIBRARY)
        && (target->GetType() != cmTarget::OBJECT_LIBRARY)
        && (target->GetType() != cmTarget::MODULE_LIBRARY)
        && (target->GetType() != cmTarget::INTERFACE_LIBRARY)
        && (target->GetType() != cmTarget::EXECUTABLE))
    {
        this->SetError("called with non-compilable target type");
        return false;
    }

    const std::string &sdkName = args[1];
    const std::string &sdkVersion = args[2];

    this->HandleArgs(target, sdkName, sdkVersion);

    return true;
}

//----------------------------------------------------------------------------
void cmAddTargetVsExtensionSdkCommand
::HandleImportedTarget(const std::string &tgt)
{
    std::ostringstream e;
    e << "Cannot specify SDK References for imported target \""
        << tgt << "\".";
    this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
}

//----------------------------------------------------------------------------
void cmAddTargetVsExtensionSdkCommand
::HandleMissingTarget(const std::string &name)
{
    std::ostringstream e;
    e << "Cannot specify SDK References for target \"" << name << "\" "
        "which is not built by this project.";
    this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
}

//----------------------------------------------------------------------------
std::string cmAddTargetVsExtensionSdkCommand
::Join(const std::string &sdkName, const std::string &sdkVersion)
{
    std::ostringstream os;
    os << sdkName << ", Version=" << sdkVersion;

    return os.str();
}

//----------------------------------------------------------------------------
void cmAddTargetVsExtensionSdkCommand
::HandleArgs(cmTarget *tgt, const std::string &sdkName, const std::string &sdkVersion)
{
    tgt->AppendProperty("VS_EXTENSION_SDK_REFERENCES", this->Join(sdkName, sdkVersion).c_str());
}
