#!/usr/bin/env bash

set -e
set -x
shopt -s dotglob

readonly name="nlohmann_json"
readonly ownership="nlohmann_json Upstream <mail@nlohmann.me>"
readonly subtree="Utilities/cmnlohmann_json"
readonly repo="https://github.com/nlohmann/json.git"
readonly tag="v3.11.2"
readonly shortlog=false
readonly paths="
  cmake
  include
  single_include
  CMakeLists.txt
  LICENSE.MIT
  nlohmann_json.natvis
"

extract_source () {
    git_archive

    pushd "${extractdir}/${name}-reduced"
    echo "* -whitespace" >> .gitattributes
    echo -e "\ninstall(FILES LICENSE.MIT DESTINATION \${CMAKE_DOC_DIR}/cmnlohmann_json)" >> CMakeLists.txt
    popd
}

. "${BASH_SOURCE%/*}/update-third-party.bash"
