cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD seeed_xiao_rp2350 CACHE STRING "Target board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "RP2350 Arm Secure platform")
set(PICO_FLASH_SIZE_BYTES 2097152 CACHE STRING "XIAO RP2350 onboard flash size" FORCE)

include(pico_sdk_import.cmake)

project(hidpilot C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(hidpilot
    src/config.c
    src/executor.c
    src/flash_store.c
    src/main.c
    src/protocol.c
    src/usb_descriptors.c
)

target_include_directories(hidpilot PRIVATE include)
target_compile_definitions(hidpilot PRIVATE
    HIDPILOT_FIRMWARE_VERSION_MAJOR=1
    HIDPILOT_FIRMWARE_VERSION_MINOR=0
    HIDPILOT_FIRMWARE_VERSION_PATCH=0
    PICO_FLASH_SIZE_BYTES=2097152
)
target_compile_options(hidpilot PRIVATE -Wall -Wextra -Werror -Wshadow -Wconversion)
target_link_libraries(hidpilot PRIVATE
    hardware_flash
    hardware_watchdog
    pico_bootrom
    pico_flash
    pico_stdlib
    pico_unique_id
    tinyusb_device
)

pico_enable_stdio_uart(hidpilot 0)
pico_enable_stdio_usb(hidpilot 0)
pico_set_program_name(hidpilot "HIDPilot")
pico_set_program_description(hidpilot "Configurable USB HID activity firmware")
pico_set_program_version(hidpilot "1.0.0")
pico_set_program_url(hidpilot "https://github.com/yuangezhizao/HIDPilot")
pico_add_extra_outputs(hidpilot)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET hidpilot POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/scripts/check_flash_bounds.py
            --nm ${CMAKE_NM} --elf $<TARGET_FILE:hidpilot> --limit 0x101fd000
    VERBATIM
)
