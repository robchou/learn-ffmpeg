cmake_minimum_required(VERSION 3.2)
project(ffmpeg-tutorial)

set(ffmpeg_tutorial_source_files
        tutorial01.c
        )

include_directories(
        /usr/local/Cellar/ffmpeg@2.8/2.8.15_3/include
        /usr/local/include/SDL
)

add_definitions(
        -D_GNU_SOURCE=1
        -D_THREAD_SAFE
)

find_library(COCOA_FRAMEWORK Cocoa)

link_directories(
        /usr/local/Cellar/ffmpeg@2.8/2.8.15_3/lib
        /usr/local/lib
)

foreach(tutorial ${ffmpeg_tutorial_source_files})
    string(REPLACE ".c" "" exec ${tutorial})
    add_executable(${exec} ${tutorial})
    target_link_libraries(${exec}
            ${COCOA_FRAMEWORK}
            avformat
            avcodec
            swresample
            swscale
            avutil
            SDLmain
            SDL
            )
endforeach()
