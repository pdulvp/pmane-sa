cmake_minimum_required(VERSION 3.16)
project(progman-linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

add_executable(progman-linux
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/DesktopArea.cpp
    src/DesktopArea.h
    src/ProfileManager.cpp
    src/ProfileManager.h
    src/GroupWindow.cpp
    src/GroupWindow.h
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
    src/NoCloseBoxStyle.cpp
    src/NoCloseBoxStyle.h
    src/IconEffect.cpp
    src/IconEffect.h
    src/Theme.cpp
    src/Theme.h
    src/ThemeDialog.cpp
    src/ThemeDialog.h
)

target_link_libraries(progman-linux PRIVATE Qt6::Widgets Qt6::Network)

install(TARGETS progman-linux RUNTIME DESTINATION bin)
