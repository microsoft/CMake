#!/usr/bin/env bash

set -e
set -x
shopt -s dotglob

readonly name="cppdap"
readonly ownership="cppdap Upstream <kwrobot@kitware.com>"
readonly subtree="Utilities/cmcppdap"
readonly repo="https://github.com/google/cppdap.git"
readonly tag="a4bc9773434f045d28a17c27396f469b63fb4f6a"
readonly shortlog=false
readonly paths="
  cmake
  CMakeLists.txt
  LICENSE
  include
  src
"

extract_source () {
    git_archive

    pushd "${extractdir}/${name}-reduced"
    echo "* -whitespace" > .gitattributes
    echo -e "'cppdap' is a C++11 library implementation of the Debug Adapter Protocol
version 5b796454c1, Jan 05, 2023\nCopyright Google LLC\n\nThis product includes software developed at Google." > NOTICE
    echo -e "\ninstall(FILES NOTICE DESTINATION \${CMAKE_DOC_DIR}/cmcppdap)" >> CMakeLists.txt
    sed -i 's/"\/wd4267"/# "\/wd4267"/' CMakeLists.txt
    sed -i 's/matched_idx\ +=\ len;/matched_idx\ +=\ \(uint32_t\)len;/' src/content_stream.cpp
    popd
}

. "${BASH_SOURCE%/*}/update-third-party.bash"
