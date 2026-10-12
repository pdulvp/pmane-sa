cmake_minimum_required(VERSION 3.16)

# The version, in one place and in the plainest form there is: three numbers
# in a file, which the bump tasks move on and which nothing has to parse out
# of source code.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROGMAN_VERSION)
string(STRIP "${PROGMAN_VERSION}" PROGMAN_VERSION)

project(pmane-sansame VERSION ${PROGMAN_VERSION} LANGUAGES CXX)

# So that bumping it is enough: the next build reconfigures rather than
# carrying yesterday's number.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network DBus)
# Only so an SVG wallpaper can be shown -- GNOME's own themed backgrounds
# are SVG. Optional: without it everything works but those draw nothing.
find_package(Qt6 6.4 QUIET COMPONENTS Svg)

# What build this is. Written at each build rather than at each configure, so
# that the version on the About box and on the package is the build in front
# of you; see cmake/GenerateVersion.cmake.
set(BUILD_VERSION_CPP ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.cpp)
set(BUILD_VERSION_TXT ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.txt)
set(GENERATE_VERSION ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake)

# Once now, so the file exists before anything is compiled.
execute_process(COMMAND ${CMAKE_COMMAND} -DVERSION=${PROGMAN_VERSION}
    -DOUT_CPP=${BUILD_VERSION_CPP} -DOUT_TXT=${BUILD_VERSION_TXT} -P ${GENERATE_VERSION})

add_custom_target(build-version ALL
    BYPRODUCTS ${BUILD_VERSION_CPP} ${BUILD_VERSION_TXT}
    COMMAND ${CMAKE_COMMAND} -DVERSION=${PROGMAN_VERSION}
        -DOUT_CPP=${BUILD_VERSION_CPP} -DOUT_TXT=${BUILD_VERSION_TXT} -P ${GENERATE_VERSION}
    COMMENT "Stamping the build with the time it was made")
set_source_files_properties(${BUILD_VERSION_CPP} PROPERTIES GENERATED TRUE)

# Every source but main.cpp, which is the only thing the program has that
# the screenshot tool below does not: the tool brings its own main so that
# it can drive the windows rather than hand them to the user.
set(PMANE_SOURCES
    src/Version.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/DesktopArea.cpp
    src/DesktopArea.h
    src/DialogChrome.cpp
    src/DialogChrome.h
    src/TextPromptDialog.cpp
    src/TextPromptDialog.h
    src/Launchers.cpp
    src/Launchers.h
    src/LaunchersDialog.cpp
    src/LaunchersDialog.h
    src/AlignmentGuides.cpp
    src/AlignmentGuides.h
    src/ScreenManager.cpp
    src/ScreenManager.h
    src/GroupWindow.cpp
    src/GroupWindow.h
    src/ApplicationPickerDialog.cpp
    src/ApplicationPickerDialog.h
    src/DesktopEntry.cpp
    src/DesktopEntry.h
    src/ItemKind.cpp
    src/ItemKind.h
    src/ItemDetails.cpp
    src/ItemDetails.h
    src/ItemLauncher.cpp
    src/ItemLauncher.h
    src/IconListWidget.cpp
    src/IconListWidget.h
    src/IconPickerDialog.cpp
    src/IconPickerDialog.h
    src/ProgramItem.h
    src/CompatTools.cpp
    src/CompatTools.h
    src/PrefixUsage.cpp
    src/PrefixUsage.h
    src/Autostart.cpp
    src/Autostart.h
    src/Ids.cpp
    src/Ids.h
    src/Kiosk.cpp
    src/Kiosk.h
    src/LaunchNotices.cpp
    src/LaunchNotices.h
    src/NotificationArea.cpp
    src/NotificationArea.h
    src/PeIconExtractor.cpp
    src/PeIconExtractor.h
    src/ItemPropertiesDialog.cpp
    src/ItemPropertiesDialog.h
    src/MinimizedGroupIcon.cpp
    src/MinimizedGroupIcon.h
    src/GroupCaptionStyle.cpp
    src/GroupCaptionStyle.h
    src/IconEffect.cpp
    src/IconEffect.h
    src/Theme.cpp
    src/Theme.h
    src/WindowScale.cpp
    src/WindowScale.h
    src/SteamLibrary.cpp
    src/SteamLibrary.h
    src/DesktopShell.cpp
    src/DesktopShell.h
    src/DesktopColorScheme.cpp
    src/DesktopColorScheme.h
    src/GnomeWallpaper.cpp
    src/GnomeWallpaper.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/SystemVolume.cpp
    src/SystemVolume.h
    src/CalendarEvents.cpp
    src/CalendarEvents.h
    src/CalendarPane.cpp
    src/CalendarPane.h
    src/CalculatorPane.cpp
    src/CalculatorPane.h
    src/SearchProvider.cpp
    src/SearchProvider.h
    src/SteamGamePickerDialog.cpp
    src/SteamGamePickerDialog.h
    src/TitleBar.cpp
    src/TitleBar.h
    src/WindowEdges.cpp
    src/WindowEdges.h
    src/ThemeDialog.cpp
    src/ThemeDialog.h
)

add_executable(pmane-sansame
    ${BUILD_VERSION_CPP}
    src/main.cpp
    ${PMANE_SOURCES}
)

# So the stamp is written before the sources that carry it are compiled.
add_dependencies(pmane-sansame build-version)

target_link_libraries(pmane-sansame PRIVATE Qt6::Widgets Qt6::Network Qt6::DBus)

# The pictures in README.md, taken off the desktop in docs/ -- see
# tools/Screenshots.cpp. Not part of a normal build: it is wanted when the
# documentation is being refreshed and never otherwise.
#
#   cmake --build build --target screenshots
#   QT_QPA_PLATFORM=offscreen ./build/screenshots docs
add_executable(screenshots EXCLUDE_FROM_ALL
    ${BUILD_VERSION_CPP}
    tools/Screenshots.cpp
    ${PMANE_SOURCES}
)
target_include_directories(screenshots PRIVATE src)
target_link_libraries(screenshots PRIVATE Qt6::Widgets Qt6::Network Qt6::DBus)
if(Qt6Svg_FOUND)
    target_link_libraries(pmane-sansame PRIVATE Qt6::Svg)
    target_link_libraries(screenshots PRIVATE Qt6::Svg)
endif()

install(TARGETS pmane-sansame RUNTIME DESTINATION bin)

# So a desktop shell can tell which application a window belongs to, and
# therefore offer to pin it. The file's name must match the app_id the
# application sets with setDesktopFileName().
install(FILES data/pmane-sansame.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

# What tells the shell to ask this application for search results. The bus
# name and object path in it are the ones SearchProvider registers.
install(FILES data/pmane-sansame-search-provider.ini
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/gnome-shell/search-providers)

# Every package has to say what it may be done with, in the one place a
# Debian system looks: /usr/share/doc/<package>/copyright. The same file an
# install from the source tree lays down, so the two cannot drift apart.
install(FILES debian/copyright
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME})

# The Debian package: `cmake --build build --target deb`.
#
# What goes in it is what install() puts there -- the program, its .desktop
# file and the search provider's -- which is also what an install from the
# source tree lays down, so the two cannot drift apart.
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "pmane-sansame")
set(CPACK_PACKAGE_VENDOR "pmane-sansame")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A Windows 3.1 Program Manager for Linux")
# One string, not three: a list would reach the control file with the
# semicolons that join it still in place.
set(CPACK_PACKAGE_DESCRIPTION
    "A Progman-style desktop for Windows programs run through Wine or Proton, with
program groups, a wine prefix per program, scaling for programs written for
smaller screens, and a kiosk mode for a machine that boots straight into it.")
set(CPACK_PACKAGE_CONTACT "pdulvp <pdulvp@laposte.net>")
# What a graphical installer shows and what cpack checks is there.
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION "x11")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/pdulvp/pmane-sansame")
# The Qt libraries it was built against, worked out from the binary itself
# rather than written down and left to go stale.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Debug symbols are for the build tree, not for everyone's disk: the binary
# is twenty megabytes with them and two without.
set(CPACK_STRIP_FILES TRUE)
# What it runs things with. None of it is needed to start the Program
# Manager, so none of it is a dependency: an item is simply told that what
# runs it is not installed.
# The icons the strip and the dialogs wear come out of the desktop's icon
# theme, and a current theme ships them as SVG: without the plugin that
# reads one, every icon in the application is blank. Not a dependency --
# it runs without them -- but close to one.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "wine, qt6-svg-plugins")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "gamescope, steam, dosbox, scummvm, retroarch")
# Told that an application arrived or went; see debian/postinst.
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/debian/postrm")
# <name>_<version>_<arch>.deb, the version being read at packing time.
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_BUILD_VERSION_FILE "${BUILD_VERSION_TXT}")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPackOptions.cmake")
# The version is the project's; the build stamp goes on as the Debian
# release, so that two packages of 1.1.0 built on different days are told
# apart and the later one is the newer one.
set(CPACK_PACKAGE_VERSION "${PROGMAN_VERSION}")
include(CPack)

# One step from a working tree to a package.
add_custom_target(deb
    COMMAND ${CMAKE_COMMAND} -E env PROGMAN_KEEP_VERSION=1
        ${CMAKE_CPACK_COMMAND} -G DEB --config ${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake
    DEPENDS pmane-sansame
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Building the Debian package")
