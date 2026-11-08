# Thư viện dùng chung PDS cho dự án CMake (vd. CubeMX "CMake" project).
#
#   set(PDS_MCU STM32G474xx)
#   add_subdirectory(<repo>/common pds_common)
#   target_link_libraries(app PRIVATE pds_common)
#
# Target app phải dùng cùng cờ CPU (-mcpu=cortex-m4 -mthumb ...).
cmake_minimum_required(VERSION 3.16)
project(pds_common C)

if(NOT PDS_MCU)
    message(FATAL_ERROR "Đặt PDS_MCU, ví dụ -DPDS_MCU=STM32G474xx")
endif()

set(PDS_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

add_library(pds_common STATIC
    src/pds_layout.c
    src/pds_flash_stm32g4.c
    src/pds_crc.c
    src/pds_config.c
    src/pds_secver.c
    src/pds_boot.c
    src/pds_identity.c
    src/pds_app_header.c
)
target_compile_definitions(pds_common PUBLIC ${PDS_MCU})
target_include_directories(pds_common PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${PDS_ROOT}/third_party/cmsis_core/Include
    ${PDS_ROOT}/third_party/cmsis_device_g4/Include
    ${PDS_ROOT}/third_party/stm32g4xx_hal_driver/Inc
)
