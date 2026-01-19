cmake_minimum_required(VERSION 3.16)

project(k10-barrel-emulator VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(k10-barrel-emulatord
    src/daemon/main.c
    src/daemon/daemon.c
)

target_include_directories(k10-barrel-emulatord PRIVATE include)

add_executable(k10-barrel-emulatorctl
    src/cli/main.c
)

target_include_directories(k10-barrel-emulatorctl PRIVATE include)

install(TARGETS k10-barrel-emulatord k10-barrel-emulatorctl
    RUNTIME DESTINATION bin
)
