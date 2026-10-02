cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD scopocket)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)

project(scopocket_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(scopocket
    src/main.c
    src/acquisition.c
    src/buttons.c
    src/calibrate.c
    src/capmeter.c
    src/calibration.c
    src/display.c
    src/gfx.c
    src/menu.c
    src/meters.c
    src/scope_view.c
    src/settings.c
    src/usbproto.c
    src/wavegen.c
)

target_include_directories(scopocket PRIVATE src)
target_compile_options(scopocket PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(scopocket
    pico_stdlib
    pico_multicore
    pico_stdio_usb
    pico_bootrom
    hardware_adc
    hardware_clocks
    hardware_divider
    hardware_dma
    hardware_flash
    hardware_pwm
    hardware_spi
    hardware_sync
)

pico_set_printf_implementation(scopocket compiler)
pico_enable_stdio_usb(scopocket 0)
pico_enable_stdio_uart(scopocket 0)

pico_set_program_name(scopocket "Scopocket firmware")
pico_set_program_description(scopocket "Scopocket v1.3 oscilloscope firmware (C rewrite)")

pico_add_extra_outputs(scopocket)
