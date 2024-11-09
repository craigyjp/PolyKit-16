cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(YD-dco-dac C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(YD-dco-dac
  YD-dco-dac.c
  usb_descriptors.c
)

pico_generate_pio_header(YD-dco-dac ${CMAKE_CURRENT_LIST_DIR}/pico-dco-dac.pio)

pico_enable_stdio_usb(YD-dco-dac 0)
pico_enable_stdio_uart(YD-dco-dac 1)

target_include_directories(YD-dco-dac PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(YD-dco-dac PRIVATE hardware_spi pico_stdlib hardware_pio hardware_adc hardware_pwm tinyusb_device tinyusb_board)

target_compile_definitions(YD-dco-dac PRIVATE
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
 )

pico_add_extra_outputs(YD-dco-dac)
