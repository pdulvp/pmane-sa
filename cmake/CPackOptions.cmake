# Read by cpack as it runs, which is after the build: by then the build has
# stamped itself with the time it was made, and that stamp is what distinguishes
# one package of a version from the next. Setting it in CMakeLists would freeze
# it at the moment the project was configured, possibly many builds ago.
#
# The stamp goes into the version behind a `~` rather than into Debian's release
# field, for two reasons:
#
#  - `~` sorts before everything, including the empty string, so
#    `1.2.1~261021143413` is earlier than `1.2.1`, which is correct for a
#    development build. In the release field it would sort later, and an
#    installed development build would then block the release from installing
#    over it;
#  - the release field is for the nth packaging of the same version, such as a
#    corrected dependency or a rewritten summary, so using it here would take a
#    field meant for something else.
#
# The release target writes no stamp at all: the package is called `1.2.1` and
# that is the version. Nothing is needed here for that case, the test below
# simply finding an empty stamp.

if(EXISTS "${CPACK_BUILD_VERSION_FILE}")
    file(READ "${CPACK_BUILD_VERSION_FILE}" _progman_build)
    string(STRIP "${_progman_build}" _progman_build)
    if(_progman_build)
        set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION}~${_progman_build}")
    endif()
endif()
