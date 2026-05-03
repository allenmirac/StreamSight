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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/xop
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai
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

# Target 4: rtsp_analysis_server
add_executable(rtsp_analysis_server
    ${CMAKE_CURRENT_SOURCE_DIR}/example/rtsp_analysis_server.cpp
    ${COMMON_SRCS}
    ${AI_SRCS}
)
target_link_libraries(rtsp_analysis_server ${COMMON_LIBS})

# Target 5: rtsp_edge_analysis_server (new)
add_executable(rtsp_edge_analysis_server
    ${CMAKE_CURRENT_SOURCE_DIR}/example/rtsp_edge_analysis_server.cpp
    ${COMMON_SRCS}
    ${AI_SRCS}
    ${CONTROL_SRCS}
    ${CDN_SIM_SRCS}
)
target_link_libraries(rtsp_edge_analysis_server ${COMMON_LIBS})

# Optional: Add compile features or specific flags per target
target_compile_options(rtsp_analysis_server PRIVATE -Wall -Wextra)
target_compile_options(rtsp_edge_analysis_server PRIVATE -Wall -Wextra)

# Print summary
message(STATUS "Build targets:")
message(STATUS "  - rtsp_server")
message(STATUS "  - rtsp_pusher")
message(STATUS "  - rtsp_h264_file")
message(STATUS "  - rtsp_analysis_server")
message(STATUS "  - rtsp_edge_analysis_server")