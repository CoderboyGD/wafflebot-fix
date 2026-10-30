cmake_minimum_required(VERSION 3.16)

project(WaffleBot-GUI VERSION 2.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Xml)

# libsndfile
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED sndfile)

# Everything that doesn't need a window (macro parsing, click classification, audio rendering).
add_library(wafflecore STATIC
    Action.h
    AudioFile.h
    AudioHandling.cpp
    AudioHandling.h
    ClickFolder.h
    Input.h
    Macro.cpp
    Macro.h
    MacroLoader.cpp
    MacroLoader.h
)
target_include_directories(wafflecore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${SNDFILE_INCLUDE_DIRS})
target_link_directories(wafflecore PUBLIC ${SNDFILE_LIBRARY_DIRS})
target_link_libraries(wafflecore PUBLIC Qt6::Core ${SNDFILE_LIBRARIES})

qt_add_executable(WaffleBot-GUI
    MANUAL_FINALIZATION
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    Timer.h
)
target_link_libraries(WaffleBot-GUI PRIVATE wafflecore Qt6::Widgets Qt6::Xml)

set_target_properties(WaffleBot-GUI PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS WaffleBot-GUI
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(WaffleBot-GUI)

# Command line test harness (cmake -DWAFFLEBOT_BUILD_TESTS=ON ..)
option(WAFFLEBOT_BUILD_TESTS "Build the command line test harness" OFF)
if(WAFFLEBOT_BUILD_TESTS)
    add_executable(wafflebot-test tests/test_main.cpp)
    target_link_libraries(wafflebot-test PRIVATE wafflecore)
endif()
