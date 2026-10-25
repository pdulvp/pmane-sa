# What the program reports about itself: the version it was given and the moment
# this build was made.
#
# The version comes from the VERSION file, incremented by hand or by the bump
# targets; the stamp is taken here at every build, so two builds of the same
# version can still be told apart, which a version alone cannot do for
# something rebuilt rather than released.
#
# Written in two forms: a source file for the program to compile in, and a bare
# line for the packaging to read, cpack running long after the compiler has
# finished.
#
# With RELEASE, no stamp is written: a release is identified by its version
# alone, and there is only one package per version. Only the release target asks
# for that; see CMakeLists.txt.

# Packaging rebuilds the program from inside cpack, after cpack has read the
# version it names the package with. Stamping again there would put one version
# on the package and a later one in the binary inside it, so during packaging
# the stamp is left alone.
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
