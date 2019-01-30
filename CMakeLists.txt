cmake_minimum_required(VERSION 3.2)
project(ffmpeg-tutorial)

set(ffmpeg_tutorial_source_files
        tutorial01.c
        tutorial02.c
        )

add_definitions(
        -D_THREAD_SAFE
)

link_directories(
        /usr/local/lib
)

foreach(tutorial ${ffmpeg_tutorial_source_files})
    string(REPLACE ".c" "" exec ${tutorial})
    add_executable(${exec} ${tutorial})
    target_link_libraries(${exec}
            avformat
            avcodec
            swresample
            swscale
            avutil
            SDL2
            )
endforeach()
