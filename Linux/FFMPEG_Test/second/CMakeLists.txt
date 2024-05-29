cmake_minimum_required(VERSION 3.5)
project(second)


SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

include_directories(${PROJECT_SOURCE_DIR}/../ffmpeg/include)

link_directories(${PROJECT_SOURCE_DIR}/../ffmpeg/lib)

add_executable(second main.cpp)

target_link_libraries(second avdevice avformat avfilter avcodec swresample avutil 
                                      swscale z fdk-aac mp3lame opus vpx x264 x265 X11 vdpau dl)
