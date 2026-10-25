# Increments the version in whichever of its three parts is named:
#
#   cmake -DFILE=VERSION -DPART=major|minor|patch -P cmake/BumpVersion.cmake
#
# Incrementing a part resets the parts below it to zero, so a minor bump of
# 1.1.9 gives 1.2.0 rather than 1.2.9. The build stamp is untouched: it records
# when a build was made, this records which version it is.

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
