cmake_minimum_required(VERSION 3.16)

project(k10-barrel-emulator VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(k10-barrel-emulatord
    src/daemon/main.c
    src/daemon/daemon.c
    src/ble/advertising.c
    src/dbus/dbus.c
    src/config/config.c
    src/log/log.c
)

target_include_directories(k10-barrel-emulatord PRIVATE include)

target_compile_options(k10-barrel-emulatord PRIVATE -Wall -Wextra -Wpedantic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)
target_compile_definitions(k10-barrel-emulatord PRIVATE K10_USE_SYSTEMD)
target_include_directories(k10-barrel-emulatord PRIVATE ${SYSTEMD_INCLUDE_DIRS})
target_link_libraries(k10-barrel-emulatord PRIVATE ${SYSTEMD_LIBRARIES})

add_executable(k10-barrel-emulatorctl
    src/cli/main.c
)

target_include_directories(k10-barrel-emulatorctl PRIVATE include)

target_compile_options(k10-barrel-emulatorctl PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(k10-barrel-emulatorctl PRIVATE ${SYSTEMD_INCLUDE_DIRS})
target_link_libraries(k10-barrel-emulatorctl PRIVATE ${SYSTEMD_LIBRARIES})

install(TARGETS k10-barrel-emulatord k10-barrel-emulatorctl
    RUNTIME DESTINATION bin
)
