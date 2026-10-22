# What the program says about itself: the version it was given, and the
# moment this particular build was made.
#
# The version comes from the VERSION file, moved on by hand (or by the bump
# tasks); the stamp is taken here at every build, so that two builds of the
# same version are still told apart -- which is what a version alone cannot
# do for something that is rebuilt rather than released.
#
# Written in two forms: a source file for the program to carry, and a bare
# line for the packaging to read, cpack running long after the compiler has
# finished.
#
# With RELEASE, no stamp at all: a version is named by its number and nothing
# else, and two packages of one version have nothing to tell apart -- there is
# only one. The release target is what asks for that, and only it; see
# CMakeLists.txt.

# Packaging builds the program again from inside cpack, after cpack has read
# the version it is naming the package with. Stamping it again there would
# put one version on the package and a later one in the binary inside it, so
# while packaging the stamp is left alone.
if(DEFINED ENV{PROGMAN_KEEP_VERSION} AND EXISTS "${OUT_TXT}")
    return()
endif()

if(RELEASE)
    set(BUILD_VERSION "")
else()
    string(TIMESTAMP BUILD_VERSION "%y%m%d%H%M%S")
endif()

file(WRITE "${OUT_CPP}"
"// Written at every build by cmake/GenerateVersion.cmake. Not to be edited,
// and not to be checked in: it is this build, not this source.
//
// extern, because a const at namespace scope is this file's own otherwise,
// and the program is to read them from elsewhere.
extern const char *const kProgmanVersion;
const char *const kProgmanVersion = \"${VERSION}\";
extern const char *const kProgmanBuildVersion;
const char *const kProgmanBuildVersion = \"${BUILD_VERSION}\";
")

file(WRITE "${OUT_TXT}" "${BUILD_VERSION}")
