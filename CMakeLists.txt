cmake_minimum_required(VERSION 2.8)
project(ffmpeg-tutorial)

set(CMAKE_C_STANDARD 99)

set(ffmpeg_tutorial_source_files
    tutorial01.c
    tutorial02.c
    )

add_definitions(
    -D_THREAD_SAFE
    )

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale libswresample)
pkg_check_modules(SDL REQUIRED sdl)

include_directories(
    ${FFMPEG_INCLUDE_DIRS}
    )

link_directories(
    ${FFMPEG_LIBRARY_DIRS}
    )

foreach(tutorial ${ffmpeg_tutorial_source_files})
    string(REPLACE ".c" "" exec ${tutorial})
    add_executable(${exec} ${tutorial})
    target_link_libraries(${exec}
        ${FFMPEG_LIBRARIES}
        ${SDL2_LIBRARIES}
        pthread
        )
endforeach()
