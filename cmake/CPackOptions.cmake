# Read by cpack as it runs, which is after the build: by then this build has
# stamped itself with the time it was made, and that is the package's version
# too. Setting it in CMakeLists would freeze it at the moment the project was
# configured, which may be weeks of builds ago.

if(EXISTS "${CPACK_BUILD_VERSION_FILE}")
    file(READ "${CPACK_BUILD_VERSION_FILE}" _progman_version)
    string(STRIP "${_progman_version}" _progman_version)
    if(_progman_version)
        set(CPACK_PACKAGE_VERSION "${_progman_version}")
        set(CPACK_DEBIAN_PACKAGE_VERSION "${_progman_version}")
    endif()
endif()
