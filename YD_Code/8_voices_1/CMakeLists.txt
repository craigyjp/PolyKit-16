cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(YD-dco-lfo C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(YD-dco-lfo
  YD-dco-lfo.c
  usb_descriptors.c
)

pico_generate_pio_header(YD-dco-lfo ${CMAKE_CURRENT_LIST_DIR}/pico-dco.pio)

pico_enable_stdio_usb(YD-dco-lfo 0)
pico_enable_stdio_uart(YD-dco-lfo 1)

target_include_directories(YD-dco-lfo PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(YD-dco-lfo PRIVATE pico_stdlib hardware_pio hardware_adc hardware_pwm tinyusb_device tinyusb_board)

target_compile_definitions(YD-dco-lfo PRIVATE
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
 )

pico_add_extra_outputs(YD-dco-lfo)
