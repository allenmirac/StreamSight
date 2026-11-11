cmake_minimum_required(VERSION 3.14)
project(StreamSight)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-D_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
endif()

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Include directories
set(INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rtsp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg
    ${CMAKE_CURRENT_SOURCE_DIR}/src/effect
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api
    ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party
)
include_directories(${INC_DIRS})

# Find OpenCV
find_package(OpenCV QUIET)
if(NOT OpenCV_FOUND)
    # Try pkg-config as fallback
    include(FindPkgConfig)
    pkg_check_modules(OpenCV opencv4)
    if(NOT OpenCV_FOUND)
        pkg_check_modules(OpenCV opencv)
    endif()
endif()

if(OpenCV_FOUND)
    message(STATUS "OpenCV found: ${OpenCV_INCLUDE_DIRS}")
    include_directories(${OpenCV_INCLUDE_DIRS})
    set(OPENCV_LIBS ${OpenCV_LIBS})
else()
    message(STATUS "OpenCV not found, building without OpenCV support")
    set(OPENCV_LIBS "")
endif()

# Find FFmpeg
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG
        libavformat
        libavcodec
        libavutil
        libswscale
    )
endif()

if(FFMPEG_FOUND)
    message(STATUS "FFmpeg found: ${FFMPEG_INCLUDE_DIRS}")
    include_directories(${FFMPEG_INCLUDE_DIRS})
    set(FFMPEG_LIBS ${FFMPEG_LIBRARIES})
else()
    message(STATUS "FFmpeg not found, building without streamsight")
    set(FFMPEG_LIBS "")
endif()

# Common libraries
set(COMMON_LIBS
    rt
    pthread
    dl
    m
    ${OPENCV_LIBS}
)

# Collect source files
# Net layer
file(GLOB NET_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/net/*.cpp)

# RTSP/RTP protocol stack layer
file(GLOB RTSP_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/rtsp/*.cpp)

# AI layer — algorithm components driven by src/effect/FaceRecognitionPlugin
set(AI_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/EventLogger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/FaceDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/FaceDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/FaceDetectorDetail.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/FaceRecognizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/FrameAnalyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/FrameOverlay.cpp
)

# Observe layer
set(OBSERVE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/observe/MetricsRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/observe/LatencyTracer.cpp
)

# FFmpeg layer
set(FFMPEG_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/FFmpegStreamer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/FFmpegUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/RtspOutputAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/RtmpOutputAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/AudioOutputAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/StreamPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/StreamSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/StreamServer.cpp
)

# API layer
set(API_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api/JsonUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api/StreamApiServer.cpp
)

# Effect layer
set(EFFECT_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/effect/FaceRecognitionPlugin.cpp
)

# Common sources for all targets
set(COMMON_SRCS
    ${NET_SRCS}
    ${RTSP_SRCS}
    ${OBSERVE_SRCS}
)

# Target 1: streamsight (FFmpeg C API full pipeline)
if(FFMPEG_FOUND AND OpenCV_FOUND)
    add_executable(streamsight
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${COMMON_SRCS}
        ${AI_SRCS}
        ${FFMPEG_SRCS}
        ${EFFECT_SRCS}
        ${API_SRCS}
    )
    target_link_libraries(streamsight
        ${COMMON_LIBS}
        ${FFMPEG_LIBS}
    )
    target_compile_options(streamsight PRIVATE -Wall -Wextra)
else()
    message(STATUS "streamsight: requires FFmpeg + OpenCV (skipped)")
endif()

# Print summary
message(STATUS "Build targets:")
message(STATUS "  - streamsight")

# ── Tests ────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build all test binaries" OFF)

if(BUILD_TESTS)
    enable_testing()

    # GoogleTest: system package first, FetchContent fallback.
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://gitee.com/mirrors/googletest.git
            GIT_TAG        v1.14.0)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_library(ss_gtest_main INTERFACE)
    if(TARGET GTest::gtest_main)
        target_link_libraries(ss_gtest_main INTERFACE GTest::gtest_main)
    elseif(TARGET GTest::Main)
        target_link_libraries(ss_gtest_main INTERFACE GTest::Main)
    else()
        target_link_libraries(ss_gtest_main INTERFACE gtest_main)
    endif()

    # Comma-separated: address,undefined OR thread.
    set(ENABLE_SANITIZERS "" CACHE STRING "Sanitizers: address,undefined or thread")
    set(SS_SAN_FLAGS "")
    if(ENABLE_SANITIZERS AND
       NOT ENABLE_SANITIZERS STREQUAL "address,undefined" AND
       NOT ENABLE_SANITIZERS STREQUAL "thread")
        message(FATAL_ERROR
            "ENABLE_SANITIZERS must be 'address,undefined' or 'thread'")
    endif()
    if(ENABLE_SANITIZERS MATCHES "thread" AND
       ENABLE_SANITIZERS MATCHES "address")
        message(FATAL_ERROR
            "ENABLE_SANITIZERS=thread cannot be combined with address")
    endif()
    if(ENABLE_SANITIZERS)
        string(REPLACE "," ";" SS_SAN_LIST "${ENABLE_SANITIZERS}")
        string(REPLACE ";" "," SS_SAN_CSV "${SS_SAN_LIST}")
        set(SS_SAN_FLAGS
            "-fsanitize=${SS_SAN_CSV}"
            "-fno-omit-frame-pointer"
            "-g")
    endif()

    include(GoogleTest)

    function(streamsight_add_test target)
        cmake_parse_arguments(T "" "" "SOURCES;LIBS;LABELS" ${ARGN})
        add_executable(${target} ${T_SOURCES})
        target_include_directories(${target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(${target} PRIVATE ss_gtest_main ${T_LIBS})
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        if(SS_SAN_FLAGS)
            target_compile_options(${target} PRIVATE ${SS_SAN_FLAGS})
            target_link_options(${target} PRIVATE ${SS_SAN_FLAGS})
        endif()
        gtest_discover_tests(${target}
            PROPERTIES TIMEOUT 60
            DISCOVERY_TIMEOUT 30)
        # gtest_discover_tests forwards PROPERTIES as a list, so every label
        # after the first one would be parsed as another property name.
        # Apply labels from a test-time include script instead, where the
        # discovered <target>_TESTS list is available.
        set(SS_LABELS_SCRIPT
            "${CMAKE_CURRENT_BINARY_DIR}/${target}_labels.cmake")
        file(WRITE "${SS_LABELS_SCRIPT}"
            "if(${target}_TESTS)\n"
            "  set_tests_properties(\${${target}_TESTS} PROPERTIES LABELS \"${T_LABELS}\")\n"
            "endif()\n")
        set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
            "${SS_LABELS_SCRIPT}")
    endfunction()

    set(CORE_TEST_LIBS rt pthread dl m)

    set(TEST_CORE_SRCS ${NET_SRCS} ${RTSP_SRCS} ${OBSERVE_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/FaceDatabase.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/api/JsonUtil.cpp)
    file(GLOB TEST_CORE_TESTS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/core/*.cpp)
    streamsight_add_test(test_core
        SOURCES ${TEST_CORE_SRCS} ${TEST_CORE_TESTS}
        LIBS ${CORE_TEST_LIBS}
        LABELS "fast;tsan")

    if(OpenCV_FOUND)
        file(GLOB TEST_AI_TESTS CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/ai/*.cpp)
        streamsight_add_test(test_ai_core
            SOURCES ${AI_SRCS} ${EFFECT_SRCS} ${OBSERVE_SRCS} ${TEST_AI_TESTS}
            LIBS ${CORE_TEST_LIBS} ${OPENCV_LIBS}
            LABELS "heavy")
    endif()

    if(FFMPEG_FOUND AND OpenCV_FOUND)
        file(GLOB TEST_FFMPEG_TESTS CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/ffmpeg/*.cpp)
        streamsight_add_test(test_ffmpeg
            SOURCES ${COMMON_SRCS} ${AI_SRCS} ${EFFECT_SRCS}
                    ${FFMPEG_SRCS} ${TEST_FFMPEG_TESTS}
            LIBS ${COMMON_LIBS} ${FFMPEG_LIBS}
            LABELS "heavy")

        file(GLOB TEST_API_TESTS CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/api/*.cpp)
        streamsight_add_test(test_api
            SOURCES ${COMMON_SRCS} ${AI_SRCS} ${EFFECT_SRCS}
                    ${FFMPEG_SRCS} ${API_SRCS} ${TEST_API_TESTS}
            LIBS ${COMMON_LIBS} ${FFMPEG_LIBS}
            LABELS "heavy;http")
    endif()
endif()

# streamsight-stress: stress testing harness
if(FFMPEG_FOUND AND OpenCV_FOUND)
    add_executable(streamsight-stress
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/stress_tester.cpp
        ${COMMON_SRCS}
        ${AI_SRCS}
        ${FFMPEG_SRCS}
        ${EFFECT_SRCS}
        ${API_SRCS}
    )
    target_link_libraries(streamsight-stress
        ${COMMON_LIBS}
        ${FFMPEG_LIBS}
    )
    target_compile_options(streamsight-stress PRIVATE -Wall -Wextra)
    message(STATUS "  - streamsight-stress")
else()
    message(STATUS "  - streamsight-stress (requires FFmpeg + OpenCV, skipped)")
endif()

# streamsight-netbench: network-layer microbenchmark.
# Only needs src/net/, so it builds even without FFmpeg/OpenCV.
add_executable(streamsight-netbench
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/net_bench.cpp
    ${NET_SRCS}
)
target_include_directories(streamsight-netbench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
)
target_link_libraries(streamsight-netbench pthread dl rt m)
target_compile_options(streamsight-netbench PRIVATE -Wall -Wextra)
# Stamp the build type into the binary so a stored JSON can never be mistaken
# for one produced by a different optimisation level.
target_compile_definitions(streamsight-netbench PRIVATE
    STREAMSIGHT_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
message(STATUS "  - streamsight-netbench")