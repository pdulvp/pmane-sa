# Read by cpack as it runs, which is after the build: by then this build has
# stamped itself with the time it was made, and that stamp is what separates
# one package of a version from the next. Setting it in CMakeLists would
# freeze it at the moment the project was configured, which may be weeks of
# builds ago.
#
# The version itself -- 1.1.0 and the like -- is the project's and is already
# set; this only adds the build as the Debian release, giving 1.1.0-26.10....

if(EXISTS "${CPACK_BUILD_VERSION_FILE}")
    file(READ "${CPACK_BUILD_VERSION_FILE}" _progman_build)
    string(STRIP "${_progman_build}" _progman_build)
    if(_progman_build)
        set(CPACK_DEBIAN_PACKAGE_RELEASE "${_progman_build}")
    endif()
endif()
