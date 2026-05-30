cmake_minimum_required(VERSION 3.10)
project(RTSPServer)

set(CMAKE_CXX_STANDARD 11)
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

# Legacy targets (deprecated fork+pipe path)
option(BUILD_LEGACY_TARGETS "Build legacy targets (rtsp_analysis_server, rtsp_edge_analysis_server)" OFF)

# Include directories
set(INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
    ${CMAKE_CURRENT_SOURCE_DIR}/src/xop
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg
    ${CMAKE_CURRENT_SOURCE_DIR}/src/effect
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api
    ${CMAKE_CURRENT_SOURCE_DIR}/src/3rdpart
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
    message(STATUS "FFmpeg not found, building without ffmpeg_streamer")
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

# Xop layer
file(GLOB XOP_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/xop/*.cpp)

# AI layer
file(GLOB AI_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/*.cpp)

# Control layer
set(CONTROL_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/Classifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/Scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/PipelineRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/StreamManager.cpp
)

# CDN Sim layer
set(CDN_SIM_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cdn_sim/EdgeNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cdn_sim/EdgeNodePool.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/PipelineManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg/StreamSession.cpp
)

# API layer
set(API_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api/StreamApiServer.cpp
)

# Effect layer
set(EFFECT_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/effect/FaceRecognitionPlugin.cpp
)

# Common sources for all targets
set(COMMON_SRCS
    ${NET_SRCS}
    ${XOP_SRCS}
    ${OBSERVE_SRCS}
)

# Target 1: rtsp_server
add_executable(rtsp_server
    ${CMAKE_CURRENT_SOURCE_DIR}/example/rtsp_server.cpp
    ${COMMON_SRCS}
)
target_link_libraries(rtsp_server ${COMMON_LIBS})

# Target 2: rtsp_pusher
add_executable(rtsp_pusher
    ${CMAKE_CURRENT_SOURCE_DIR}/example/rtsp_pusher.cpp
    ${COMMON_SRCS}
)
target_link_libraries(rtsp_pusher ${COMMON_LIBS})

# Target 3: rtsp_h264_file
add_executable(rtsp_h264_file
    ${CMAKE_CURRENT_SOURCE_DIR}/example/rtsp_h264_file.cpp
    ${COMMON_SRCS}
)
target_link_libraries(rtsp_h264_file ${COMMON_LIBS})

# Target 4: rtsp_analysis_server [LEGACY — fork+pipe path]
if(BUILD_LEGACY_TARGETS)
    add_executable(rtsp_analysis_server
        ${CMAKE_CURRENT_SOURCE_DIR}/example/rtsp_analysis_server.cpp
        ${COMMON_SRCS}
        ${AI_SRCS}
    )
    target_link_libraries(rtsp_analysis_server ${COMMON_LIBS})
    target_compile_options(rtsp_analysis_server PRIVATE -Wall -Wextra)
endif()

# Target 5: rtsp_edge_analysis_server [LEGACY — fork+pipe path]
if(BUILD_LEGACY_TARGETS)
    add_executable(rtsp_edge_analysis_server
        ${CMAKE_CURRENT_SOURCE_DIR}/example/rtsp_edge_analysis_server.cpp
        ${COMMON_SRCS}
        ${AI_SRCS}
        ${CONTROL_SRCS}
        ${CDN_SIM_SRCS}
        ${FFMPEG_SRCS}
        ${EFFECT_SRCS}
    )
    target_link_libraries(rtsp_edge_analysis_server ${COMMON_LIBS} ${FFMPEG_LIBS})
    target_compile_options(rtsp_edge_analysis_server PRIVATE -Wall -Wextra)
endif()

# Target 6: ffmpeg_streamer (FFmpeg C API full pipeline)
if(FFMPEG_FOUND AND OpenCV_FOUND)
    add_executable(ffmpeg_streamer
        ${CMAKE_CURRENT_SOURCE_DIR}/example/ffmpeg_streamer.cpp
        ${COMMON_SRCS}
        ${AI_SRCS}
        ${FFMPEG_SRCS}
        ${EFFECT_SRCS}
        ${API_SRCS}
    )
    target_link_libraries(ffmpeg_streamer
        ${COMMON_LIBS}
        ${FFMPEG_LIBS}
    )
    target_compile_options(ffmpeg_streamer PRIVATE -Wall -Wextra)
else()
    message(STATUS "ffmpeg_streamer: requires FFmpeg + OpenCV (skipped)")
endif()

# Print summary
message(STATUS "Build targets:")
message(STATUS "  - rtsp_server")
message(STATUS "  - rtsp_pusher")
message(STATUS "  - rtsp_h264_file")
if(BUILD_LEGACY_TARGETS)
    message(STATUS "  - rtsp_analysis_server [LEGACY]")
    message(STATUS "  - rtsp_edge_analysis_server [LEGACY]")
endif()
message(STATUS "  - ffmpeg_streamer")

# ── Tests ────────────────────────────────────────────────────────────────
# test_stream_session: structural tests (no FFmpeg/OpenCV runtime needed)
if(FFMPEG_FOUND AND OpenCV_FOUND)
    add_executable(test_stream_session
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_stream_session.cpp
        ${COMMON_SRCS}
        ${AI_SRCS}
        ${FFMPEG_SRCS}
        ${EFFECT_SRCS}
    )
    target_link_libraries(test_stream_session
        ${COMMON_LIBS}
        ${FFMPEG_LIBS}
    )
    target_compile_options(test_stream_session PRIVATE -Wall -Wextra)
    message(STATUS "  - test_stream_session")
else()
    message(STATUS "  - test_stream_session (requires FFmpeg + OpenCV, skipped)")
endif()

# test_api_server: structural tests for StreamApiServer
if(FFMPEG_FOUND AND OpenCV_FOUND)
    add_executable(test_api_server
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api_server.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/api/StreamApiServer.cpp
        ${COMMON_SRCS}
        ${AI_SRCS}
        ${FFMPEG_SRCS}
        ${EFFECT_SRCS}
    )
    target_link_libraries(test_api_server
        ${COMMON_LIBS}
        ${FFMPEG_LIBS}
    )
    target_compile_options(test_api_server PRIVATE -Wall -Wextra)
    message(STATUS "  - test_api_server")
else()
    message(STATUS "  - test_api_server (requires FFmpeg + OpenCV, skipped)")
endif()

# test_event_bus: header-only, just needs pthread
add_executable(test_event_bus
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_event_bus.cpp
)
target_link_libraries(test_event_bus pthread)
target_compile_options(test_event_bus PRIVATE -Wall -Wextra)
message(STATUS "  - test_event_bus")

# test_effect_factory: needs AI + Effect sources
if(FFMPEG_FOUND AND OpenCV_FOUND)
    add_executable(test_effect_factory
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_effect_factory.cpp
        ${COMMON_SRCS}
        ${AI_SRCS}
        ${EFFECT_SRCS}
    )
    target_link_libraries(test_effect_factory
        ${COMMON_LIBS}
        ${FFMPEG_LIBS}
    )
    target_compile_options(test_effect_factory PRIVATE -Wall -Wextra)
    message(STATUS "  - test_effect_factory")
else()
    message(STATUS "  - test_effect_factory (requires FFmpeg + OpenCV, skipped)")
endif()

# BUILD_TESTS: unified test target
option(BUILD_TESTS "Build all test binaries" OFF)
if(BUILD_TESTS)
    add_executable(test_smoke
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_smoke_driver.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_event_bus.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_effect_factory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_stream_session.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api_server.cpp
        ${COMMON_SRCS}
        ${AI_SRCS}
        ${FFMPEG_SRCS}
        ${EFFECT_SRCS}
        ${API_SRCS}
    )
    target_link_libraries(test_smoke ${COMMON_LIBS} ${FFMPEG_LIBS})
    target_compile_options(test_smoke PRIVATE -Wall -Wextra -DTEST_SMOKE_BUILD)
    message(STATUS "  - test_smoke (all tests)")
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