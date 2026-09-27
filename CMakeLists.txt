cmake_minimum_required(VERSION 3.16)
project(progman-linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(progman-linux
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/GroupWindow.cpp
    src/GroupWindow.h
    src/IconListWidget.cpp
    src/IconListWidget.h
    src/ProgramItem.h
    src/PeIconExtractor.cpp
    src/PeIconExtractor.h
    src/ItemPropertiesDialog.cpp
    src/ItemPropertiesDialog.h
)

target_link_libraries(progman-linux PRIVATE Qt6::Widgets)

install(TARGETS progman-linux RUNTIME DESTINATION bin)
