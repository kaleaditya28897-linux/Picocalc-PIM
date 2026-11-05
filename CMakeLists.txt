cmake_minimum_required(VERSION 3.16)

option(PIM_BUILD_FIRMWARE "Build the Pico 2 W UF2 firmware" ON)
option(PIM_BUILD_TESTS "Build host-side C tests" ON)
option(PIM_ENABLE_SANITIZERS "Use AddressSanitizer and UBSan for host tests" OFF)

if(PIM_BUILD_FIRMWARE)
    set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk" CACHE PATH
        "Pico SDK checkout")
    set(PICOCALC_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/PicoCalc" CACHE PATH
        "Official PicoCalc checkout")
    set(PICO_VFS_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-vfs" CACHE PATH
        "pico-vfs checkout")
    foreach(path PICO_SDK_PATH PICOCALC_PATH PICO_VFS_PATH)
        if(NOT EXISTS "${${path}}")
            message(FATAL_ERROR "${path} is missing; run git submodule update --init --recursive")
        endif()
    endforeach()
    set(PICO_BOARD pico2_w CACHE STRING "Pico SDK board")
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    project(picocalc_pim C CXX ASM)
else()
    project(picocalc_pim C)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(pim_core STATIC
    src/core/pim.c
    src/core/calendar.c
    src/core/games.c
    src/core/storage.c
)
target_include_directories(pim_core PUBLIC include)
target_compile_options(pim_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(PIM_BUILD_TESTS)
    enable_testing()
    add_executable(pim_tests tests/test_pim.c)
    target_link_libraries(pim_tests PRIVATE pim_core)
    target_compile_options(pim_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pim_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(pim_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME pim_tests COMMAND pim_tests)
endif()

if(PIM_BUILD_FIRMWARE)
    pico_sdk_init()

    set(PICOCALC_LVGL_DIR "${PICOCALC_PATH}/Code/picocalc_lvgl_graphics_demo")
    add_subdirectory(${PICOCALC_LVGL_DIR}/i2ckbd picocalc_i2ckbd)
    add_subdirectory(${PICOCALC_LVGL_DIR}/lcdspi picocalc_lcdspi)
    add_subdirectory(${PICOCALC_LVGL_DIR}/lib/lvgl lvgl)
    target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
    target_include_directories(lvgl PUBLIC ${PICOCALC_LVGL_DIR})

    add_library(pim_vfs STATIC
        ${PICO_VFS_PATH}/src/blockdevice/sd.c
        ${PICO_VFS_PATH}/src/filesystem/fat.c
        ${PICO_VFS_PATH}/src/filesystem/vfs.c
        ${PICO_VFS_PATH}/vendor/ff15/source/ff.c
        ${PICO_VFS_PATH}/vendor/ff15/source/ffsystem.c
        ${PICO_VFS_PATH}/vendor/ff15/source/ffunicode.c
    )
    target_include_directories(pim_vfs PUBLIC
        ${PICO_VFS_PATH}/include
        ${PICO_VFS_PATH}/include/filesystem/ChaN
        ${PICO_VFS_PATH}/vendor/ff15/source
    )
    target_compile_options(pim_vfs PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_LIST_DIR}/include/pim/vfs_compat.h>"
        "$<$<COMPILE_LANGUAGE:C>:-Dstrdup=pim_vfs_strdup>"
    )
    target_link_libraries(pim_vfs PUBLIC pico_stdlib pico_sync hardware_spi)

    add_executable(picocalc_pim
        src/firmware/main.c
        src/firmware/fs_init.c
        src/firmware/input.c
        src/firmware/ui.c
        src/firmware/vfs_compat.c
        ${PICOCALC_LVGL_DIR}/lv_port_disp_picocalc_ILI9488.c
    )
    target_include_directories(picocalc_pim PRIVATE
        include
        ${PICOCALC_LVGL_DIR}
        ${PICOCALC_LVGL_DIR}/lib/lvgl
        ${PICO_VFS_PATH}/include
    )
    target_link_libraries(picocalc_pim PRIVATE
        pim_core pim_vfs pico_stdlib hardware_i2c hardware_spi i2ckbd lcdspi lvgl
    )
    target_compile_options(picocalc_pim PRIVATE -Wall -Wextra -Wpedantic)
    pico_enable_stdio_usb(picocalc_pim 0)
    pico_enable_stdio_uart(picocalc_pim 1)
    pico_add_extra_outputs(picocalc_pim)
endif()
