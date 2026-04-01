cmake_minimum_required(VERSION 3.16)
project(xrt-sync
    VERSION 0.3.1
    DESCRIPTION "Cross-platform real-time state synchronization middleware for immersive computing and AR/XR engineering"
    HOMEPAGE_URL "https://github.com/canshenglin/xrt-sync"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XRTSYNC_BUILD_EXAMPLES "Build example programs" ON)
option(XRTSYNC_BUILD_TESTS    "Build unit tests"       ON)
option(XRTSYNC_BUILD_SHARED   "Build as a shared library" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
endif()

set(XRTSYNC_SOURCES
    src/session.cc
    src/platform_socket.cc
)

if(XRTSYNC_BUILD_SHARED)
    add_library(xrtsync SHARED ${XRTSYNC_SOURCES})
    target_compile_definitions(xrtsync PRIVATE XRTSYNC_BUILDING XRTSYNC_SHARED)
    target_compile_definitions(xrtsync INTERFACE XRTSYNC_SHARED)
else()
    add_library(xrtsync STATIC ${XRTSYNC_SOURCES})
endif()

target_include_directories(xrtsync
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(xrtsync PUBLIC ws2_32)
endif()

# Threads for std::thread used by Run() and tests.
find_package(Threads REQUIRED)
target_link_libraries(xrtsync PUBLIC Threads::Threads)

if(XRTSYNC_BUILD_EXAMPLES)
    add_executable(pose_streamer examples/pose_streamer.cc)
    target_link_libraries(pose_streamer PRIVATE xrtsync)
endif()

if(XRTSYNC_BUILD_TESTS)
    enable_testing()
    add_executable(xrtsync_jitter_buffer_test tests/jitter_buffer_test.cc)
    target_include_directories(xrtsync_jitter_buffer_test PRIVATE src)
    target_link_libraries(xrtsync_jitter_buffer_test PRIVATE Threads::Threads)
    add_test(NAME jitter_buffer COMMAND xrtsync_jitter_buffer_test)

    add_executable(xrtsync_wire_format_test tests/wire_format_test.cc)
    target_include_directories(xrtsync_wire_format_test PRIVATE src)
    add_test(NAME wire_format COMMAND xrtsync_wire_format_test)
endif()

# Installation rules (so downstream CMake projects can find_package us).
include(GNUInstallDirs)
install(TARGETS xrtsync
        EXPORT xrtsyncTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/xrtsync
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
