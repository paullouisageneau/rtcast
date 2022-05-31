cmake_minimum_required(VERSION 3.13)
project(rtcast
	DESCRIPTION "WebRTC real-time audio and video casting"
	VERSION 0.1.0
	LANGUAGES CXX)

set(SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/src/endpoint.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/encoder.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/videoencoder.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/drmvideoencoder.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/videodevice.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/cameradevice.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/audioencoder.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/audiodevice.cpp)

set(HEADERS
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast/common.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast/endpoint.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast/encoder.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast/videoencoder.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast/drmvideoencoder.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast/videodevice.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast/cameradevice.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast/audioencoder.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast/audiodevice.hpp)

set(CLI_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/cli/main.cpp)

add_subdirectory(deps/libdatachannel EXCLUDE_FROM_ALL)

find_package(Threads REQUIRED)

find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY avcodec)

find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_library(AVFORMAT_LIBRARY avformat)

find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
find_library(AVUTIL_LIBRARY avutil)

find_path(AVDEVICE_INCLUDE_DIR libavdevice/avdevice.h)
find_library(AVDEVICE_LIBRARY avdevice)

find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h)
find_library(SWSCALE_LIBRARY swscale)

find_path(SWRESAMPLE_INCLUDE_DIR libswresample/swresample.h)
find_library(SWRESAMPLE_LIBRARY swresample)

set(AV_INCLUDE_DIRS
	${AVCODEC_INCLUDE_DIR}
	${AVFORMAT_INCLUDE_DIR}
	${AVUTIL_INCLUDE_DIR}
	${AVDEVICE_INCLUDE_DIR}
	${SWSCALE_INCLUDE_DIR}
	${SWRESAMPLE_INCLUDE_DIR})

set(AV_LIBRARIES
	${AVCODEC_LIBRARY}
	${AVFORMAT_LIBRARY}
	${AVUTIL_LIBRARY}
	${AVDEVICE_LIBRARY}
	${SWSCALE_LIBRARY}
	${SWRESAMPLE_LIBRARY})

find_package(PkgConfig)
pkg_check_modules(LIBCAMERA IMPORTED_TARGET libcamera)

add_library(rtcast ${SOURCES} ${HEADERS})
set_target_properties(rtcast PROPERTIES
	VERSION ${PROJECT_VERSION}
	CXX_STANDARD 17)
target_include_directories(rtcast PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>)
target_include_directories(rtcast PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include/rtcast)
target_include_directories(rtcast PRIVATE
	${LIBCAMERA_INCLUDE_DIRS}
	${AV_INCLUDE_DIRS})
target_link_libraries(rtcast PRIVATE
	LibDataChannel::LibDataChannel
	Threads::Threads
	${AV_LIBRARIES}
	nlohmann_json) # from libdatachannel examples
if(LIBCAMERA_FOUND)
	target_include_directories(rtcast PRIVATE ${LIBCAMERA_INCLUDE_DIRS})
	target_link_libraries(rtcast PRIVATE ${LIBCAMERA_LINK_LIBRARIES})
	target_compile_definitions(rtcast PUBLIC RTCAST_HAS_LIBCAMERA)
endif()
add_library(LibRtCast::LibRtCast ALIAS rtcast)

add_executable(rtcast-cli ${CLI_SOURCES})
set_target_properties(rtcast-cli PROPERTIES
	VERSION ${PROJECT_VERSION}
    CXX_STANDARD 17
	OUTPUT_NAME rtcast)
target_link_libraries(rtcast-cli PRIVATE rtcast)

if(NOT MSVC)
        target_compile_options(rtcast PRIVATE -Wall -Wextra)
        target_compile_options(rtcast-cli PRIVATE -Wall -Wextra)
endif()

