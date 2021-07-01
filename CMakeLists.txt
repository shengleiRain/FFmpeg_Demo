cmake_minimum_required(VERSION 3.4.1)

project(ffmpeg_demo)

set(CMAKE_CXX_STANDARD 11) 
set(CMAKE_CXX_FLAGS "-D__STDC_CONSTANT_MACROS")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

# 查找当前目录下的所有源文件
# 并将名称保存到 DIR_SRCS 变量
aux_source_directory(. DIR_SRC_MAIN)
aux_source_directory(./src DIR_SRC)
aux_source_directory(./src/player DIR_SRC_PLAYER)

include_directories(./include)
include_directories(./include/player) 


find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY avcodec)

find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_library(AVFORMAT_LIBRARY avformat)

find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
find_library(AVUTIL_LIBRARY avutil)

find_path(AVDEVICE_INCLUDE_DIR libavdevice/avdevice.h)
find_library(AVDEVICE_LIBRARY avdevice)

find_path(AVSWSCALE_INCLUDE_DIR libswscale/swscale.h)
find_library(SWSCALE_LIBRARY swscale)

find_path(AVSWSAMPLE_INCLUDE_DIR libswresample/swresample.h)
find_library(SWSAMPLE_LIBRARY swresample)


# 声明一个变量 SDL_DIR 为 SDL 安装路径
set(SDL_DIR "/usr/local/Cellar/sdl2/2.0.14_1")
# 设置要包含的头文件的路径
include_directories(${SDL_DIR}/include)
# 设置要关联的库的路径
link_directories(${SDL_DIR}/lib)

add_executable(ffmpeg_demo ${DIR_SRC_MAIN} ${DIR_SRC} ${DIR_SRC_PLAYER})
target_include_directories(ffmpeg_demo PRIVATE ${AVCODEC_INCLUDE_DIR} ${AVFORMAT_INCLUDE_DIR} ${AVUTIL_INCLUDE_DIR} ${AVDEVICE_INCLUDE_DIR} ${AVSWSCALE_INCLUDE_DIR} ${AVSWSAMPLE_INCLUDE_DIR})
target_link_libraries(ffmpeg_demo PRIVATE ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${AVDEVICE_LIBRARY} ${SWSCALE_LIBRARY} ${SWSAMPLE_LIBRARY} SDL2)