# Moves the version on by one, in whichever of its three parts is asked for:
#
#   cmake -DFILE=VERSION -DPART=major|minor|patch -P cmake/BumpVersion.cmake
#
# Raising a part sets the ones under it back to zero, which is what makes
# 1.1.9 followed by a minor bump read 1.2.0 rather than 1.2.9. The build
# stamp beside it is untouched: that one says when, this one says what.

if(NOT DEFINED FILE OR NOT DEFINED PART)
    message(FATAL_ERROR "BumpVersion.cmake wants -DFILE=<path> and -DPART=major|minor|patch")
endif()

file(READ "${FILE}" CURRENT)
string(STRIP "${CURRENT}" CURRENT)

if(NOT CURRENT MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "'${CURRENT}' in ${FILE} is not a version of three numbers")
endif()

set(MAJOR ${CMAKE_MATCH_1})
set(MINOR ${CMAKE_MATCH_2})
set(PATCH ${CMAKE_MATCH_3})

if(PART STREQUAL "major")
    math(EXPR MAJOR "${MAJOR} + 1")
    set(MINOR 0)
    set(PATCH 0)
elseif(PART STREQUAL "minor")
    math(EXPR MINOR "${MINOR} + 1")
    set(PATCH 0)
elseif(PART STREQUAL "patch" OR PART STREQUAL "micro")
    math(EXPR PATCH "${PATCH} + 1")
else()
    message(FATAL_ERROR "PART is major, minor or patch, not '${PART}'")
endif()

set(NEXT "${MAJOR}.${MINOR}.${PATCH}")
file(WRITE "${FILE}" "${NEXT}\n")
message(STATUS "${CURRENT} -> ${NEXT}")
