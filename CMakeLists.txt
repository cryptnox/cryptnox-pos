cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS cryptnox-sdk-esp32/components)

# Version and BUILD_ID come from git, never from a file someone could edit back:
#   PROJECT_VER = <latest tag>-<first-parent commit count>-g<short hash>
#   BUILD_ID    = that commit count (settings_wipe_if_new_build wipes NVS when it
#                 differs from the one stored on the unit)
# CI must check out with fetch-depth: 0 and the tags, or the count is 1 and there
# is no tag to find.
find_package(Git REQUIRED)
function(pos_git out)
    execute_process(COMMAND ${GIT_EXECUTABLE} ${ARGN}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE v OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE rc ERROR_QUIET)
    if(NOT rc EQUAL 0)
        set(v "")
    endif()
    set(${out} "${v}" PARENT_SCOPE)
endfunction()

pos_git(POS_GIT_COUNT rev-list --count --first-parent HEAD)
pos_git(POS_GIT_HASH  rev-parse --short=7 HEAD)
pos_git(POS_GIT_TAG   describe --tags --abbrev=0)
if(NOT POS_GIT_COUNT MATCHES "^[0-9]+$")
    message(FATAL_ERROR "BUILD_ID needs the git history: build from a git checkout "
                        "(in CI: actions/checkout with fetch-depth: 0)")
endif()
if(DEFINED ENV{POS_VERSION_TAG})
    # Test builds only (docs/ota-testing.md): a "newer" or "older" image without
    # touching the tags. BUILD_ID is never overridable.
    set(POS_GIT_TAG "$ENV{POS_VERSION_TAG}")
elseif(POS_GIT_TAG STREQUAL "")
    # ponytail: no release tag yet; tag one (git tag v1.0.0) and this goes away.
    message(WARNING "No git tag found - version falls back to v0.0.0")
    set(POS_GIT_TAG "v0.0.0")
endif()

set(PROJECT_VER "${POS_GIT_TAG}-${POS_GIT_COUNT}-g${POS_GIT_HASH}")
set(POS_BUILD_ID ${POS_GIT_COUNT})
message(STATUS "Firmware ${PROJECT_VER}, BUILD_ID ${POS_BUILD_ID}")

# Re-run this configure step on every commit or checkout, not only when a
# CMakeLists changes — otherwise a rebuild keeps the previous version and ID.
pos_git(POS_GIT_DIR rev-parse --absolute-git-dir)
if(EXISTS "${POS_GIT_DIR}/logs/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${POS_GIT_DIR}/logs/HEAD")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(POS_BUILD_ID ${POS_BUILD_ID})   # read by main/CMakeLists.txt
project(cryptnox_pos)
