# Read by cpack as it runs, which is after the build: by then this build has
# stamped itself with the time it was made, and that stamp is what separates
# one package of a version from the next. Setting it in CMakeLists would
# freeze it at the moment the project was configured, which may be weeks of
# builds ago.
#
# The stamp goes into the version, behind a `~`, and not into Debian's
# release field. Two reasons:
#
#  - `~` sorts before everything, the empty string included:
#    `1.2.1~261021143413` is *earlier* than `1.2.1`, which is what a working
#    build is. In the release field it would be later, and a working build
#    installed would stop the version from installing over it;
#  - the release field is for the nth packaging of one same version -- a
#    dependency corrected, a summary rewritten. Putting this there takes up a
#    slot that is for something else.
#
# The release target leaves no stamp at all: the package is called `1.2.1`
# and that is the version. Nothing to do here for that -- the test below
# simply finds an empty stamp.

if(EXISTS "${CPACK_BUILD_VERSION_FILE}")
    file(READ "${CPACK_BUILD_VERSION_FILE}" _progman_build)
    string(STRIP "${_progman_build}" _progman_build)
    if(_progman_build)
        set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION}~${_progman_build}")
    endif()
endif()
