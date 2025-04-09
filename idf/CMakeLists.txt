# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.28)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(olifilo CXX)

include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}.elf)
install(PROGRAMS
  ${CMAKE_BINARY_DIR}/bootloader/bootloader.elf
  TYPE BIN
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
  ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin
  ${CMAKE_BINARY_DIR}/bootloader/bootloader.map
  ${CMAKE_BINARY_DIR}/partition_table/partition-table.bin
  ${CMAKE_BINARY_DIR}/flasher_args.json
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/${PROJECT_NAME}
)
