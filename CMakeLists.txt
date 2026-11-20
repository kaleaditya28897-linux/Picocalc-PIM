cmake_minimum_required(VERSION 3.16)

option(PIM_BUILD_FIRMWARE "Build the Pico 2 W UF2 firmware" ON)
# Off by default on purpose: it swaps the cooperative LVGL loop for FreeRTOS and
# lwIP sockets, which the Tailscale client needs and nothing else does. The
# default firmware is unaffected until this is switched on.
option(PIM_ENABLE_TAILSCALE "Build with FreeRTOS and lwIP sockets for the Tailscale client" OFF)
# The host build needs roughly twice the device's LVGL heap, because every
# pointer is twice the size. Set it to double the device figure to check that
# the interface still fits in what the device has.
set(PIM_LV_MEM_KB_HOST 320 CACHE STRING "LVGL heap for host builds, in KB")
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
    if(PIM_ENABLE_TAILSCALE)
        # Read while the SDK defines pico_mbedtls, so it has to be set before the
        # import below rather than next to the target that uses it.
        set(PICO_MBEDTLS_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/src/firmware/mbedtls_config.h")
    endif()
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    if(PIM_ENABLE_TAILSCALE)
        set(FREERTOS_KERNEL_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/FreeRTOS-Kernel" CACHE PATH
            "FreeRTOS kernel checkout")
        if(NOT EXISTS "${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2350_ARM_NTZ")
            message(FATAL_ERROR "FREERTOS_KERNEL_PATH is missing; run git submodule update --init --recursive")
        endif()
        include(${CMAKE_CURRENT_LIST_DIR}/FreeRTOS_Kernel_import.cmake)
    endif()
    project(picocalc_pim C CXX ASM)
else()
    project(picocalc_pim C)
endif()

# Source paths reach the binary through __FILE__ in assertion messages. Mapped
# to relative ones, so a published image does not carry the path - and with it
# the user name - of whichever machine built it. Every target is created after
# this line, the SDK's and LVGL's included.
add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=" "-ffile-prefix-map=${CMAKE_BINARY_DIR}/=build/")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(pim_core STATIC
    src/core/pim.c
    src/core/calendar.c
    src/core/games.c
    src/core/net.c
    src/core/storage.c
    src/core/web.c
    src/core/telnet.c
    src/core/vterm.c
    src/core/hosts.c
    src/core/sshkey.c
    src/core/sshcrypto.c
    src/core/ssh.c
    src/core/lock.c
    src/core/tsconfig.c
    src/core/boottrace.c
    src/core/jsonstrip.c
    third_party/tweetnacl/tweetnacl.c
    src/core/sound.c
    src/core/textfile.c
    src/core/calc.c
    src/core/recur.c
    src/core/search.c
    src/core/backup.c
    src/core/sdgames.c
    src/core/samples_data.c
)
target_link_libraries(pim_core PUBLIC m)
target_include_directories(pim_core PUBLIC include)
target_compile_options(pim_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(pim_core PUBLIC third_party/tweetnacl)
# Vendored curve arithmetic: compiled, not linted.
set_source_files_properties(third_party/tweetnacl/tweetnacl.c PROPERTIES COMPILE_OPTIONS "-w")

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
    add_executable(pim_tools_tests tests/test_tools.c)
    target_link_libraries(pim_tools_tests PRIVATE pim_core)
    target_compile_options(pim_tools_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pim_tools_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(pim_tools_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME pim_tools_tests COMMAND pim_tools_tests)
    add_executable(pim_ssh_tests tests/test_ssh.c)
    target_link_libraries(pim_ssh_tests PRIVATE pim_core)
    target_compile_options(pim_ssh_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pim_ssh_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(pim_ssh_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME pim_ssh_tests COMMAND pim_ssh_tests)
    # Talks to a real sshd over a socket; skips when there is none to talk to.
    add_executable(pim_ssh_live_tests tests/test_ssh_live.c)
    target_link_libraries(pim_ssh_live_tests PRIVATE pim_core)
    target_compile_options(pim_ssh_live_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pim_ssh_live_tests COMMAND pim_ssh_live_tests)
    add_executable(pim_jsonstrip_tests tests/test_jsonstrip.c third_party/cJSON/cJSON.c)
    target_link_libraries(pim_jsonstrip_tests PRIVATE pim_core)
    target_include_directories(pim_jsonstrip_tests PRIVATE third_party/cJSON)
    target_compile_options(pim_jsonstrip_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pim_jsonstrip_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(pim_jsonstrip_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME pim_jsonstrip_tests COMMAND pim_jsonstrip_tests)

    add_executable(pim_boot_tests tests/test_boot.c)
    target_link_libraries(pim_boot_tests PRIVATE pim_core)
    target_compile_options(pim_boot_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pim_boot_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(pim_boot_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME pim_boot_tests COMMAND pim_boot_tests)

    add_executable(pim_ts_tests tests/test_ts.c)
    target_link_libraries(pim_ts_tests PRIVATE pim_core)
    target_compile_options(pim_ts_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pim_ts_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(pim_ts_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME pim_ts_tests COMMAND pim_ts_tests)

    add_executable(pim_hosts_tests tests/test_hosts.c)
    target_link_libraries(pim_hosts_tests PRIVATE pim_core)
    target_compile_options(pim_hosts_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pim_hosts_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(pim_hosts_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME pim_hosts_tests COMMAND pim_hosts_tests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME pim_hosts_import_tests
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/pim-hosts-import.py --self-test)
    endif()
    add_executable(pim_vterm_tests tests/test_vterm.c)
    target_link_libraries(pim_vterm_tests PRIVATE pim_core)
    target_compile_options(pim_vterm_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pim_vterm_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(pim_vterm_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME pim_vterm_tests COMMAND pim_vterm_tests)
endif()

option(PIM_BUILD_UI_TESTS "Build the headless LVGL UI simulation tests" OFF)
if(PIM_BUILD_UI_TESTS AND PIM_BUILD_TESTS)
    set(PIM_LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/PicoCalc/Code/picocalc_lvgl_graphics_demo")
    file(GLOB_RECURSE PIM_LVGL_SOURCES "${PIM_LVGL_DIR}/lib/lvgl/lvgl/src/*.c")
    add_library(pim_lvgl_host STATIC ${PIM_LVGL_SOURCES})
    target_compile_definitions(pim_lvgl_host PUBLIC LV_CONF_INCLUDE_SIMPLE LV_FONT_UNSCII_8=1 PIM_DEVICE_LV_CONF="${PIM_LVGL_DIR}/lv_conf.h" PIM_LV_MEM_KB=${PIM_LV_MEM_KB_HOST})
    target_include_directories(pim_lvgl_host PUBLIC "${CMAKE_CURRENT_LIST_DIR}/src/firmware/lvgl_conf" "${PIM_LVGL_DIR}" "${PIM_LVGL_DIR}/lib/lvgl")
    target_compile_options(pim_lvgl_host PRIVATE -w)
    add_executable(pim_ui_tests tests/ui_sim/ui_sim.c src/firmware/ui.c)
    target_compile_definitions(pim_ui_tests PRIVATE PIM_ROOT="pim_sim_root")
    target_include_directories(pim_ui_tests PRIVATE tests/ui_sim/stubs)
    target_link_libraries(pim_ui_tests PRIVATE pim_core pim_lvgl_host m)
    target_compile_options(pim_ui_tests PRIVATE -Wall -Wextra)
    if(PIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pim_ui_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(pim_ui_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME pim_ui_tests COMMAND pim_ui_tests)
endif()

if(PIM_BUILD_FIRMWARE)
    pico_sdk_init()

    set(PICOCALC_LVGL_DIR "${PICOCALC_PATH}/Code/picocalc_lvgl_graphics_demo")
    add_subdirectory(${PICOCALC_LVGL_DIR}/i2ckbd picocalc_i2ckbd)
    add_subdirectory(${PICOCALC_LVGL_DIR}/lcdspi picocalc_lcdspi)
    add_subdirectory(${PICOCALC_LVGL_DIR}/lib/lvgl lvgl)
    target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE LV_FONT_UNSCII_8=1
        PIM_DEVICE_LV_CONF="${PICOCALC_LVGL_DIR}/lv_conf.h")
    if(PIM_ENABLE_TAILSCALE)
        # The Tailscale client needs the room more than LVGL does. 112 KB is
        # still over twice the vendor default; the shipping firmware keeps 160.
        # A single screenful of plain widgets needs a fraction of this; what
        # used it was long lists building every row at once. SSH and the tunnel
        # are what this device is for, so the interface takes the smaller share.
        target_compile_definitions(lvgl PUBLIC PIM_LV_MEM_KB=48)
    endif()
    target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/firmware/lvgl_conf ${PICOCALC_LVGL_DIR})

    add_library(pim_vfs STATIC
        ${PICO_VFS_PATH}/src/blockdevice/sd.c
        ${PICO_VFS_PATH}/src/filesystem/fat.c
        ${PICO_VFS_PATH}/src/filesystem/vfs.c
        ${PICO_VFS_PATH}/vendor/ff15/source/ff.c
        ${PICO_VFS_PATH}/vendor/ff15/source/ffsystem.c
        ${PICO_VFS_PATH}/vendor/ff15/source/ffunicode.c
    )
    target_include_directories(pim_core PRIVATE ${PICO_VFS_PATH}/include)
    target_compile_definitions(pim_core PRIVATE PIM_VFS_DIRENT=1)
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

    if(PIM_ENABLE_TAILSCALE)
        # Every translation unit that touches pico_sync has to agree on what a
        # mutex is. With FreeRTOS in the picture, lock_owner_id_t is 32 bits
        # instead of 8, so recursive_mutex_t is 12 bytes instead of 8. A library
        # built without that knowledge lays down 8-byte entries in the mutex
        # array, while the SDK's own initialiser strides 12 through it and
        # writes LOCK_INVALID_OWNER_ID - 0xffffffff - over the next entry's spin
        # lock. The first thing the filesystem does is take one of those, and an
        # exclusive access to 0xffffffff is a bus fault, not a failed lock.
        # Linking the kernel brings in the definitions that settle it.
        # Only the headers, not the kernel's sources: linking FreeRTOS-Kernel
        # would compile its port into every one of these libraries. What they
        # need is the SDK's RTOS adapter header, which is what redefines
        # lock_owner_id_t, plus the headers it pulls in.
        add_library(pim_rtos_headers INTERFACE)
        target_compile_definitions(pim_rtos_headers INTERFACE
            PICO_CONFIG_RTOS_ADAPTER_HEADER=${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/non_secure/freertos_sdk_config.h)
        target_include_directories(pim_rtos_headers SYSTEM INTERFACE
            ${FREERTOS_KERNEL_PATH}/include
            ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/non_secure
            ${CMAKE_CURRENT_LIST_DIR}/src/firmware/freertos_conf)
        foreach(pim_sync_target pim_vfs lvgl pim_core)
            target_link_libraries(${pim_sync_target} PUBLIC pim_rtos_headers)
        endforeach()
    endif()

    if(PIM_ENABLE_TAILSCALE)
        set(PIM_FREERTOS_SOURCES src/firmware/freertos_hooks.c src/firmware/ts_platform.c
            src/firmware/tailscale.c)
        # An allocation the Tailscale client cannot have is a thing to report,
        # not to die of: the SDK's default is to panic, which takes the whole
        # device down and tells the user nothing. With this off, malloc returns
        # NULL, the client's own checks run, and the screen says how much was
        # left - which is the number that matters while this is being fitted in.
        set(PIM_MALLOC_DEFS PICO_MALLOC_PANIC=0
            # Serialise malloc with a mutex rather than by stopping the
            # scheduler; see the allocator note in ts_platform.c.
            PICO_USE_MALLOC_MUTEX=1)
        # The vendored Tailscale client, built as its own library so its warnings
        # stay out of ours. It is upstream's code: we compile it, we do not lint it.
        file(GLOB PIM_MICROLINK_SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/third_party/microlink/src/*.c"
            "${CMAKE_CURRENT_LIST_DIR}/third_party/microlink/wireguard_lwip/src/*.c"
            "${CMAKE_CURRENT_LIST_DIR}/third_party/microlink/wireguard_lwip/src/crypto/refc/*.c")
        add_library(pim_microlink STATIC ${PIM_MICROLINK_SOURCES} third_party/cJSON/cJSON.c)
        target_include_directories(pim_microlink PUBLIC
            third_party/microlink/include
            third_party/microlink/src
            third_party/microlink/wireguard_lwip/src
            third_party/microlink/wireguard_lwip/src/crypto
            third_party/microlink/wireguard_lwip/src/crypto/refc
            third_party/cJSON
            src/firmware                      # esp_compat/... as upstream spells it
            src/firmware/esp_compat           # and bare, as upstream also spells it
            src/firmware/freertos_conf
            include
            "${PICO_LWIP_PATH}/src/include/compat/posix"   # sys/socket.h, arpa/inet.h, netdb.h
        )
        target_compile_definitions(pim_microlink PUBLIC PIM_TAILSCALE=1)
        # lwIP renames close/read/write/fcntl onto its socket calls, which breaks
        # if newlib declares them afterwards: forcing these two first means
        # newlib is parsed before the macros exist.
        # C only: the SDK contributes an assembly file to this target, and a
        # forced C header is not something the assembler can read. Same spelling
        # as pim_vfs uses below.
        target_compile_options(pim_microlink PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-w>"
            "$<$<COMPILE_LANGUAGE:C>:-includefcntl.h>"
            "$<$<COMPILE_LANGUAGE:C>:-includeunistd.h>"
            "$<$<COMPILE_LANGUAGE:C>:-includeesp_lwip_ext.h>")
        target_link_libraries(pim_microlink PUBLIC
            pico_stdlib pico_rand pico_cyw43_arch_lwip_sys_freertos
            FreeRTOS-Kernel pico_mbedtls
            # for pim_json_strip, which trims the network map before it is
            # parsed; stated here so the static libraries link in that order
            pim_core
        )
    endif()
    add_executable(picocalc_pim
        src/firmware/main.c
        ${PIM_FREERTOS_SOURCES}
        src/firmware/boottrace.c
        src/firmware/fs_init.c
        src/firmware/input.c
        src/firmware/ui.c
        src/firmware/wifi.c
        src/firmware/sound.c
        src/firmware/vfs_compat.c
        ${PICOCALC_LVGL_DIR}/lv_port_disp_picocalc_ILI9488.c
    )
    target_include_directories(picocalc_pim PRIVATE
        include
        ${PICOCALC_LVGL_DIR}
        ${PICOCALC_LVGL_DIR}/lib/lvgl
        ${PICO_VFS_PATH}/include
    )
    if(PIM_ENABLE_TAILSCALE)
        target_link_libraries(picocalc_pim PRIVATE
            pim_core pim_vfs pico_stdlib hardware_i2c hardware_spi hardware_pwm hardware_watchdog i2ckbd lcdspi lvgl
            pico_cyw43_arch_lwip_sys_freertos FreeRTOS-Kernel
        )
        target_include_directories(picocalc_pim PRIVATE src/firmware/freertos_conf src/firmware)
        target_link_libraries(picocalc_pim PRIVATE pim_microlink)
        target_compile_definitions(picocalc_pim PRIVATE ${PIM_MALLOC_DEFS} PIM_TAILSCALE=1)
        # lwipopts.h is read while building the SDK's lwIP too, so the switch has
        # to reach those targets, not just ours.
        target_compile_definitions(pim_core PUBLIC PIM_TAILSCALE=1)
    else()
        target_link_libraries(picocalc_pim PRIVATE
            pim_core pim_vfs pico_stdlib hardware_i2c hardware_spi hardware_pwm hardware_watchdog i2ckbd lcdspi lvgl
            pico_cyw43_arch_lwip_poll
        )
    endif()
    target_compile_definitions(picocalc_pim PRIVATE PICO_STACK_SIZE=0x1000)
    target_compile_options(picocalc_pim PRIVATE -Wall -Wextra -Wpedantic)
    if(PIM_ENABLE_TAILSCALE)
        # Bringing the tunnel up is the one job where the logs matter more than
        # the few KB of RAM TinyUSB costs: they come out of the same cable the
        # board is flashed with, so no UART pins need wiring.
        pico_enable_stdio_usb(picocalc_pim 1)
    else()
        pico_enable_stdio_usb(picocalc_pim 0)
    endif()
    pico_enable_stdio_uart(picocalc_pim 1)
    pico_add_extra_outputs(picocalc_pim)
endif()
