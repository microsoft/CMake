include(RunCMake)

set(RunCMake_GENERATOR_PLATFORM "")
run_cmake(NoPlatform)

if("${RunCMake_GENERATOR}" MATCHES "^Visual Studio [0-9]+( 20[0-9][0-9])?$")
  set(RunCMake_GENERATOR_PLATFORM "x64")
  run_cmake(x64Platform)
else()
  set(RunCMake_GENERATOR_PLATFORM "Bad Platform")
  run_cmake(BadPlatform)
endif()

set(RunCMake_GENERATOR_PLATFORM "")

set(RunCMake_TEST_OPTIONS -A "Test Platform" -A "Extra Platform")
run_cmake(TwoPlatforms)
unset(RunCMake_TEST_OPTIONS)

if("${RunCMake_GENERATOR}" MATCHES "^Visual Studio [0-9]+( 20[0-9][0-9])?$")
  set(RunCMake_TEST_OPTIONS -DCMAKE_TOOLCHAIN_FILE=${RunCMake_SOURCE_DIR}/TestPlatform-toolchain.cmake)
  run_cmake(TestPlatformToolchain)
  unset(RunCMake_TEST_OPTIONS)
else()
  set(RunCMake_TEST_OPTIONS -DCMAKE_TOOLCHAIN_FILE=${RunCMake_SOURCE_DIR}/BadPlatform-toolchain.cmake)
  run_cmake(BadPlatformToolchain)
  unset(RunCMake_TEST_OPTIONS)
endif()

if("${RunCMake_GENERATOR}" MATCHES "^Visual Studio (1[4567])( 20[0-9][0-9])?$")
  unset(ENV{WindowsSDKVersion})

  set(RunCMake_GENERATOR_PLATFORM "Test Platform,nocomma")
  run_cmake(BadFieldNoComma)
  set(RunCMake_GENERATOR_PLATFORM "Test Platform,unknown=")
  run_cmake(BadFieldUnknown)
  set(RunCMake_GENERATOR_PLATFORM "version=")
  run_cmake(BadVersionEmpty)
  set(RunCMake_GENERATOR_PLATFORM "version=10.0.0.0")
  run_cmake(BadVersionMissing)
  set(RunCMake_GENERATOR_PLATFORM "version=1.2.3.4")
  run_cmake(BadVersionUnsupported)

  if(RunCMake_GENERATOR MATCHES "^Visual Studio (1[45]) ")
    set(RunCMake_GENERATOR_PLATFORM "version=10.0")
    run_cmake(BadVersionPre2019)
    unset(RunCMake_GENERATOR_PLATFORM)
  else()
    set(expect_version "10.0")
    set(RunCMake_GENERATOR_PLATFORM "version=${expect_version}")
    set(RunCMake_TEST_VARIANT_DESCRIPTION "-${expect_version}")
    run_cmake_with_options(VersionExists -DCMAKE_SYSTEM_VERSION=10.0)
    unset(RunCMake_GENERATOR_PLATFORM)
  endif()

  set(kits "")
  cmake_host_system_information(RESULT kitsRoot10
    QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
    VALUE "KitsRoot10"
    VIEW 64_32
    ERROR_VARIABLE kitsRoot10Error
    )
  if(NOT kitsRoot10Error AND IS_DIRECTORY "${kitsRoot10}/include")
    cmake_path(SET kitsInclude "${kitsRoot10}/include")
    file(GLOB kits RELATIVE "${kitsInclude}" "${kitsInclude}/*/um/windows.h")
    list(TRANSFORM kits REPLACE "/.*" "")
  endif()
  cmake_host_system_information(RESULT kitsRoot81
    QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
    VALUE "KitsRoot81"
    VIEW 64_32
    ERROR_VARIABLE kitsRoot81Error
    )
  if(NOT kitsRoot81Error AND EXISTS "${kitsRoot81}/include/um/windows.h")
    list(PREPEND kits "8.1")
  endif()

  if(kits)
    message(STATUS "Available Kits: ${kits}")
    if(RunCMake_GENERATOR MATCHES "^Visual Studio 14 ")
      set(kitMax 10.0.14393.0)
    else()
      set(kitMax "")
    endif()
    if(kitMax)
      set(kitsIn "${kits}")
      set(kits "")
      foreach(kit IN LISTS kitsIn)
        if(kit VERSION_LESS_EQUAL "${kitMax}")
          list(APPEND kits "${kit}")
        else()
          message(STATUS "Excluding Kit ${kit} > ${kitMax}")
        endif()
      endforeach()
    endif()
  elseif(NOT RunCMake_GENERATOR MATCHES "^Visual Studio 14 ")
    message(FATAL_ERROR "Could not find any Windows SDKs to drive test cases.")
  endif()

  foreach(expect_version IN LISTS kits)
    set(RunCMake_GENERATOR_PLATFORM "version=${expect_version}")
    set(RunCMake_TEST_VARIANT_DESCRIPTION "-${expect_version}")
    run_cmake_with_options(VersionExists -DCMAKE_SYSTEM_VERSION=10.0)
    unset(RunCMake_GENERATOR_PLATFORM)
  endforeach()
  foreach(expect_version IN LISTS kits)
    set(RunCMake_TEST_VARIANT_DESCRIPTION "-CMP0149-OLD-${expect_version}")
    run_cmake_with_options(VersionExists -DCMAKE_SYSTEM_VERSION=${expect_version} -DCMAKE_POLICY_DEFAULT_CMP0149=OLD)
  endforeach()
  if(kits MATCHES "(^|;)([0-9.]+)$")
    set(expect_version "${CMAKE_MATCH_2}")
    foreach(test_version IN LISTS kits)
      set(RunCMake_TEST_VARIANT_DESCRIPTION "-CMP0149-NEW-${test_version}")
      run_cmake_with_options(VersionExists -DCMAKE_SYSTEM_VERSION=${test_version} -DCMAKE_POLICY_DEFAULT_CMP0149=NEW)
    endforeach()
  endif()
  list(REMOVE_ITEM kits 8.1)
  foreach(expect_version IN LISTS kits)
    set(RunCMake_TEST_VARIANT_DESCRIPTION "-WindowsSDKVersion-${expect_version}")
    set(ENV{WindowsSDKVersion} "${expect_version}\\")
    run_cmake_with_options(VersionExists -DCMAKE_SYSTEM_VERSION=10.0 -DCMAKE_POLICY_DEFAULT_CMP0149=NEW)
    unset(ENV{WindowsSDKVersion})
  endforeach()
endif()
