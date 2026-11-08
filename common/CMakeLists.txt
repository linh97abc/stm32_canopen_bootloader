# Thư viện dùng chung BLC (BootLoader Common) cho dự án CMake (vd. CubeMX "CMake" project).
#
#   set(BLC_MCU STM32G474xx)
#   add_subdirectory(<repo>/common blc_common)
#   target_link_libraries(app PRIVATE blc_common)
#
# Target app phải dùng cùng cờ CPU (-mcpu=cortex-m4 -mthumb ...).
cmake_minimum_required(VERSION 3.16)
project(blc_common C)

if(NOT BLC_MCU)
    message(FATAL_ERROR "Đặt BLC_MCU, ví dụ -DBLC_MCU=STM32G474xx")
endif()

set(BLC_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

add_library(blc_common STATIC
    src/blc_layout.c
    src/blc_flash_stm32g4.c
    src/blc_crc.c
    src/blc_config.c
    src/blc_secver.c
    src/blc_boot.c
    src/blc_identity.c
    src/blc_app_header.c
)
target_compile_definitions(blc_common PUBLIC ${BLC_MCU})
target_include_directories(blc_common PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${BLC_ROOT}/third_party/cmsis_core/Include
    ${BLC_ROOT}/third_party/cmsis_device_g4/Include
    ${BLC_ROOT}/third_party/stm32g4xx_hal_driver/Inc
)
