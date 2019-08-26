cmake_minimum_required(VERSION 3.2)
project(ffmpeg-tutorial)

set(CMAKE_C_STANDARD 99)

if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(LINUX TRUE)
endif()

set(ffmpeg_tutorial_source_files
    tutorial01.c
    tutorial02.c
    tutorial03.c
    )

# make sure ffmpeg 2.8.x(2.8.5 and 2.8.15 tested) and sdl1.2 list in PKG_CONFIG_PATH
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG libavcodec libavformat libavutil libswscale libswresample)
pkg_check_modules(SDL sdl)

include_directories(
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL_INCLUDE_DIRS}
    )

link_directories(
    ${FFMPEG_LIBRARY_DIRS}
    ${SDL_LIBRARY_DIRS}
    )

add_definitions(
    -D_GNU_SOURCE=1
    -D_THREAD_SAFE
    )

if(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa)
    set(LINKED_LIBS
        ${COCOA_FRAMEWORK}
        )
elseif(LINUX)
    set(LINKED_LIBS
        pthread
        m
        )
    message(${LINKED_LIBS})
endif()

set(LINKED_LIBS
    ${FFMPEG_LIBRARIES}
    ${SDL_LIBRARIES}
    ${LINKED_LIBS}
    )

foreach(tutorial ${ffmpeg_tutorial_source_files})
    string(REPLACE ".c" "" exec ${tutorial})
    add_executable(${exec} ${tutorial})
    target_link_libraries(${exec}
        ${LINKED_LIBS}
        )
endforeach()
