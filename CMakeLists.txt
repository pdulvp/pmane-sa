cmake_minimum_required(VERSION 3.16)
project(progman-linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

add_executable(progman-linux
    src/main.cpp
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
    src/SteamGamePickerDialog.cpp
    src/SteamGamePickerDialog.h
    src/TitleBar.cpp
    src/TitleBar.h
    src/WindowEdges.cpp
    src/WindowEdges.h
    src/ThemeDialog.cpp
    src/ThemeDialog.h
)

target_link_libraries(progman-linux PRIVATE Qt6::Widgets Qt6::Network)

install(TARGETS progman-linux RUNTIME DESTINATION bin)

# So a desktop shell can tell which application a window belongs to, and
# therefore offer to pin it. The file's name must match the app_id the
# application sets with setDesktopFileName().
install(FILES data/progman-linux.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
