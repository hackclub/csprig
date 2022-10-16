cmake_minimum_required(VERSION 3.24)

include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

project(csprig C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(csprig
  main.c
)

# ha this doesn't exist on arm. im stupid
# target_compile_options (csprig PRIVATE -fsanitize=undefined)
# target_link_options    (csprig PRIVATE -fsanitize=undefined)

target_link_libraries(csprig PRIVATE
  pico_stdlib
  pico_multicore
  hardware_spi
)

pico_enable_stdio_usb(csprig 1)
pico_enable_stdio_uart(csprig 0)

# target_compile_definitions(csprig PRIVATE
#         # compile time configuration of I2S
#         PICO_AUDIO_I2S_MONO_INPUT=1
#         #define for our example code
#         USE_AUDIO_I2S=1
#         PICO_AUDIO_I2S_DATA_PIN=9
#         PICO_AUDIO_I2S_CLOCK_PIN_BASE=10
# #            PICO_DEFAULT_UART=0
# #            PICO_DEFAULT_UART_TX_PIN=28
# #            PICO_DEFAULT_UART_RX_PIN=29
#         )

# create map/bin/hex file etc.
pico_add_extra_outputs(csprig)
