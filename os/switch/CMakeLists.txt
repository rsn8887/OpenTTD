cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_SYSTEM_NAME "Generic")
    set(DEVKITPRO $ENV{DEVKITPRO})
    set(CMAKE_SYSTEM_PROCESSOR "armv8-a")
    set(CMAKE_C_COMPILER "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc")
    set(CMAKE_CXX_COMPILER "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-g++")
    set(CMAKE_ASM_COMPILER "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-as")
    set(CMAKE_AR "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc-ar" CACHE STRING "")
    set(CMAKE_RANLIB "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc-ranlib" CACHE STRING "")
    set(CMAKE_FIND_ROOT_PATH ${DEVKITPRO} ${DEVKITPRO}/devkitA64 ${DEVKITPRO}/libnx ${DEVKITPRO}/portlibs/switch)
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "Shared libs not available")
    link_directories(${DEVKITPRO}/libnx/lib ${DEVKITPRO}/portlibs/switch/lib)
endif()

set(SHORT_NAME openttd)
project(${SHORT_NAME})

set(SWITCH_VERSION "1.04")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    configure_file(../../objs/debug/${SHORT_NAME} ./${SHORT_NAME} COPYONLY)
else()
    configure_file(../../objs/release/${SHORT_NAME} ./${SHORT_NAME} COPYONLY)
endif()

add_custom_target(${SHORT_NAME}.nro
  DEPENDS ${SHORT_NAME}
  COMMAND ${DEVKITPRO}/tools/bin/nacptool --create "${SHORT_NAME}" "rsn8887" "${SWITCH_VERSION}" ${SHORT_NAME}.nacp
  COMMAND ${DEVKITPRO}/tools/bin/elf2nro ${SHORT_NAME} ${SHORT_NAME}.nro --icon=${CMAKE_BINARY_DIR}/switchdata/icon0.jpg --nacp=${SHORT_NAME}.nacp
)
add_custom_target(${SHORT_NAME}_switch.zip
  DEPENDS ${SHORT_NAME}.nro
  COMMAND rm -rf ${CMAKE_BINARY_DIR}/release/${SHORT_NAME}
  COMMAND mkdir -p ${CMAKE_BINARY_DIR}/release/${SHORT_NAME}
  COMMAND cp -f ${CMAKE_BINARY_DIR}/${SHORT_NAME}.nro ${CMAKE_BINARY_DIR}/release/${SHORT_NAME}/${SHORT_NAME}.nro
  COMMAND unzip ${CMAKE_BINARY_DIR}/switchdata/data.zip -d${CMAKE_BINARY_DIR}/release/
  COMMAND cd ${CMAKE_BINARY_DIR}/release && zip -r ../${SHORT_NAME}_${SWITCH_VERSION}_switch.zip ${SHORT_NAME} && cd ${CMAKE_BINARY_DIR}
  COMMAND zip -d ${SHORT_NAME}_${SWITCH_VERSION}_switch.zip *.DS_Store
  COMMAND zip -d ${SHORT_NAME}_${SWITCH_VERSION}_switch.zip *__MAC*
)



