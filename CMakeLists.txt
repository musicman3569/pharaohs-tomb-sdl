cmake_minimum_required(VERSION 3.21)
project(ptomb C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED CONFIG)

add_executable(ptomb
    src/main.c
    src/platform.c
    src/gamepad.c
    src/game.c
    src/sound.c
    src/text.c
    src/font8x8.c
    src/screenvm.c
    src/settings.c
    src/pcx.c
    src/hiscore.c
    src/menu.c
    src/cga.c
    src/graph3.c
    src/lzexe.c
    src/gamedata.c
    src/level.c
    src/timer.c
    src/tp.c
)

if(TARGET SDL2::SDL2main)
    target_link_libraries(ptomb PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(ptomb PRIVATE SDL2::SDL2)

if(MSVC)
    target_compile_options(ptomb PRIVATE /W4)
else()
    target_compile_options(ptomb PRIVATE -Wall -Wextra -Wpedantic)
    if(MINGW)
        target_link_options(ptomb PRIVATE -static-libgcc)
    endif()
endif()

# Put SDL2.dll next to the executable so it runs outside the build shell.
if(WIN32)
    add_custom_command(TARGET ptomb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:ptomb> $<TARGET_FILE_DIR:ptomb>
        COMMAND_EXPAND_LISTS)
endif()
