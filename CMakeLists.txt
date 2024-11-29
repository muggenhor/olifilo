# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.28)

project(olifilo
  LANGUAGES CXX
  VERSION "0.0.1"
)

include(CTest)

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
    VERSION "${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}"
    SOVERSION "${CMAKE_PROJECT_VERSION_MAJOR}"
)

target_sources(${PROJECT_NAME}
  INTERFACE
    FILE_SET HEADERS
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/olifilo/expected.hpp
)

include(GNUInstallDirs)
install(
  TARGETS
    ${PROJECT_NAME}
  RUNTIME
    COMPONENT Runtime
  LIBRARY
    COMPONENT Runtime
    NAMELINK_COMPONENT Development
  ARCHIVE
    COMPONENT Development
  FILE_SET HEADERS
    COMPONENT Development
)

if(BUILD_TESTING)
  add_executable(hmm)
  target_sources(hmm PRIVATE
    coro.cpp
  )
  target_link_libraries(hmm PRIVATE ${PROJECT_NAME})

  target_compile_features(hmm PRIVATE cxx_std_23)
  target_compile_options(hmm
    PRIVATE
      -fdiagnostics-color=always
      #-fsanitize=undefined,address
  )
  target_link_options(hmm
    PRIVATE
      -fdiagnostics-color=always
      #-fsanitize=undefined,address
  )
endif()
