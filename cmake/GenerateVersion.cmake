# The build's own version: the moment it was made.
#
# Run at every build rather than at configure time, so that two builds of the
# same checkout are told apart -- which is the whole point of a version on
# something that is not released but rebuilt.
#
# Written in two forms: a source file for the program to carry, and a bare
# line for the packaging to read, cpack running long after the compiler has
# finished.

# Packaging builds the program again from inside cpack, after cpack has read
# the version it is naming the package with. Stamping it again there would
# put one version on the package and a later one in the binary inside it, so
# while packaging the stamp is left alone.
if(DEFINED ENV{PROGMAN_KEEP_VERSION} AND EXISTS "${OUT_TXT}")
    return()
endif()

string(TIMESTAMP BUILD_VERSION "%y.%m.%d%H%M%S")

file(WRITE "${OUT_CPP}"
"// Written at every build by cmake/GenerateVersion.cmake. Not to be edited,
// and not to be checked in: it is this build, not this source.
// extern, because a const at namespace scope is this file's own otherwise,
// and the program is to read it from elsewhere.
extern const char *const kProgmanBuildVersion;
const char *const kProgmanBuildVersion = \"${BUILD_VERSION}\";
")

file(WRITE "${OUT_TXT}" "${BUILD_VERSION}")
