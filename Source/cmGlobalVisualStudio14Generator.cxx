/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2014 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#include "cmGlobalVisualStudio14Generator.h"
#include "cmLocalVisualStudio10Generator.h"
#include "cmMakefile.h"
#include "cmAlgorithms.h"

static const char vs14generatorName[] = "Visual Studio 14 2015";

// Map generator name without year to name with year.
static const char* cmVS14GenName(const std::string& name, std::string& genName)
{
  if(strncmp(name.c_str(), vs14generatorName,
             sizeof(vs14generatorName)-6) != 0)
    {
    return 0;
    }
  const char* p = name.c_str() + sizeof(vs14generatorName) - 6;
  if(cmHasLiteralPrefix(p, " 2015"))
    {
    p += 5;
    }
  genName = std::string(vs14generatorName) + p;
  return p;
}

class cmGlobalVisualStudio14Generator::Factory
  : public cmGlobalGeneratorFactory
{
public:
  virtual cmGlobalGenerator*
  CreateGlobalGenerator(const std::string& name, cmake* cm) const
    {
    std::string genName;
    const char* p = cmVS14GenName(name, genName);
    if(!p)
      { return 0; }
    if(!*p)
      {
      return new cmGlobalVisualStudio14Generator(cm, genName, "");
      }
    if(*p++ != ' ')
      { return 0; }
    if(strcmp(p, "Win64") == 0)
      {
      return new cmGlobalVisualStudio14Generator(cm, genName, "x64");
      }
    if(strcmp(p, "ARM") == 0)
      {
      return new cmGlobalVisualStudio14Generator(cm, genName, "ARM");
      }
    return 0;
    }

  virtual void GetDocumentation(cmDocumentationEntry& entry) const
    {
    entry.Name = std::string(vs14generatorName) + " [arch]";
    entry.Brief =
      "Generates Visual Studio 2015 project files.  "
      "Optional [arch] can be \"Win64\" or \"ARM\"."
      ;
    }

  virtual void GetGenerators(std::vector<std::string>& names) const
    {
    names.push_back(vs14generatorName);
    names.push_back(vs14generatorName + std::string(" ARM"));
    names.push_back(vs14generatorName + std::string(" Win64"));
    }
};

//----------------------------------------------------------------------------
cmGlobalGeneratorFactory* cmGlobalVisualStudio14Generator::NewFactory()
{
  return new Factory;
}

//----------------------------------------------------------------------------
cmGlobalVisualStudio14Generator::cmGlobalVisualStudio14Generator(cmake* cm,
  const std::string& name, const std::string& platformName)
  : cmGlobalVisualStudio12Generator(cm, name, platformName)
{
  std::string vc14Express;
  this->ExpressEdition = cmSystemTools::ReadRegistryValue(
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VCExpress\\14.0\\Setup\\VC;"
    "ProductDir", vc14Express, cmSystemTools::KeyWOW64_32);
  this->DefaultPlatformToolset = "v140";
  this->Version = VS14;
}

//----------------------------------------------------------------------------
bool
cmGlobalVisualStudio14Generator::MatchesGeneratorName(
                                                const std::string& name) const
{
  std::string genName;
  if(cmVS14GenName(name, genName))
    {
    return genName == this->GetName();
    }
  return false;
}

//----------------------------------------------------------------------------
bool cmGlobalVisualStudio14Generator::InitializeWindowsStore(cmMakefile* mf)
{
  if(!this->SelectWindowsStoreToolset(this->DefaultPlatformToolset))
    {
    std::ostringstream  e;
    if(this->DefaultPlatformToolset.empty())
      {
      e << this->GetName() << " supports Windows Store '8.0', '8.1' and "
        "'10.0', but not '" << this->SystemVersion <<
        "'.  Check CMAKE_SYSTEM_VERSION.";
      }
    else
      {
      e << "A Windows Store component with CMake requires both the Windows "
        << "Desktop SDK as well as the Windows Store '" << this->SystemVersion
        << "' SDK. Please make sure that you have both installed";
      }
    mf->IssueMessage(cmake::FATAL_ERROR, e.str());
    return false;
    }
  if (this->SystemVersion == "10.0")
    {
    // Find the lastest SDK and set VS_DEFAULT_TARGET_PLATFORM_VERSION
    std::string sdkVersion = GetWindows10SDKVersion();
    mf->AddDefinition("VS_DEFAULT_TARGET_PLATFORM_VERSION",
      sdkVersion.c_str());
    }
  return true;
}

//----------------------------------------------------------------------------
bool
cmGlobalVisualStudio14Generator::SelectWindowsStoreToolset(
  std::string& toolset) const
{
  if(this->SystemVersion == "10.0")
    {
    if (this->IsWindowsStoreToolsetInstalled() &&
        this->IsWindowsDesktopToolsetInstalled())
      {
      toolset = "v140";
      return true;
      }
    else
      {
      return false;
      }
    }
  return
    this->cmGlobalVisualStudio12Generator::SelectWindowsStoreToolset(toolset);
}

//----------------------------------------------------------------------------
void cmGlobalVisualStudio14Generator::WriteSLNHeader(std::ostream& fout)
{
  // Visual Studio 14 writes .sln format 12.00
  fout << "Microsoft Visual Studio Solution File, Format Version 12.00\n";
  if (this->ExpressEdition)
    {
    fout << "# Visual Studio Express 14 for Windows Desktop\n";
    }
  else
    {
    fout << "# Visual Studio 14\n";
    }
}
//----------------------------------------------------------------------------
bool
cmGlobalVisualStudio14Generator::IsWindowsDesktopToolsetInstalled() const
{
  const char desktop10Key[] =
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\"
    "VisualStudio\\14.0\\VC\\Runtimes";

  std::vector<std::string> vc14;
  return cmSystemTools::GetRegistrySubKeys(desktop10Key,
    vc14, cmSystemTools::KeyWOW64_32);
}

//----------------------------------------------------------------------------
bool
cmGlobalVisualStudio14Generator::IsWindowsStoreToolsetInstalled() const
{
  const char universal10Key[] =
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\"
    "VisualStudio\\14.0\\Setup\\Build Tools for Windows 10";

  std::vector<std::string> subkeys;
  return cmSystemTools::GetRegistrySubKeys(universal10Key,
    subkeys, cmSystemTools::KeyWOW64_32);
}

//----------------------------------------------------------------------------
std::string cmGlobalVisualStudio14Generator::GetWindows10SDKVersion()
{
  // This logic is taken from the vcvarsqueryregistry.bat file from VS2015
  std::string win10Root;
  if (!cmSystemTools::ReadRegistryValue(
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;"
    "KitsRoot10", win10Root, cmSystemTools::KeyWOW64_32))
  {
    // If we can't find the root in HKLM try HKCU
    if (!cmSystemTools::ReadRegistryValue(
      "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;"
      "KitsRoot10", win10Root, cmSystemTools::KeyWOW64_32))
    {
      return NULL;
    }
  }

  std::vector<std::string> sdks;
  std::string path = win10Root + "Include/*";
  cmSystemTools::GlobDirs(path, sdks);
  if (!sdks.empty())
  {
    // Sort the result to make sure to use the latest one.
    std::sort(sdks.begin(), sdks.end());
    std::string sdkVersion = cmSystemTools::GetFilenameName(sdks.back());
    return sdkVersion;
  }
  return NULL;
}
