cmake_minimum_required(VERSION 3.5)
project(first)


SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

include_directories(${PROJECT_SOURCE_DIR}/../ffmpeg/include)

link_directories(${PROJECT_SOURCE_DIR}/../ffmpeg/lib)

add_executable(first main.cpp Utils.h Utils.cpp)

target_link_libraries(first avdevice avformat avfilter avcodec swresample avutil 
                                      swscale z fdk-aac mp3lame opus vpx x264 x265 X11 vdpau dl)

