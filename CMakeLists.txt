cmake_minimum_required(VERSION 3.24)

project(NordTerminal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

option(NORD_TERMINAL_BUILD_EXAMPLES "Build example applications" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

add_library(NordTerminal
    include/nordterminal/NordTerminal.h
    include/nordterminal/TerminalCell.h
    include/nordterminal/TerminalEmulator.h
    include/nordterminal/TerminalKeyMapper.h
    include/nordterminal/TerminalProfile.h
    include/nordterminal/TerminalScrollback.h
    include/nordterminal/TerminalSelection.h
    include/nordterminal/TerminalSession.h
    include/nordterminal/TerminalTheme.h
    include/nordterminal/TerminalThemeLoader.h
    include/nordterminal/TerminalWidget.h
    src/terminal/TerminalEmulator.cpp
    src/terminal/TerminalKeyMapper.cpp
    src/terminal/TerminalProfile.cpp
    src/terminal/TerminalSelection.cpp
    src/terminal/TerminalScrollback.cpp
    src/terminal/TerminalSession.cpp
    src/terminal/TerminalTheme.cpp
    src/terminal/TerminalThemeLoader.cpp
    src/terminal/TerminalWidget.cpp
)

target_include_directories(NordTerminal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(NordTerminal
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

if(UNIX AND NOT APPLE)
    target_link_libraries(NordTerminal PRIVATE util)
endif()

if(NORD_TERMINAL_BUILD_EXAMPLES)
    add_executable(nord_terminal_demo examples/basic_widget_demo/main.cpp)
    target_link_libraries(nord_terminal_demo PRIVATE NordTerminal)
endif()
