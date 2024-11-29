# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.28)

project(olifilo
  LANGUAGES CXX
  VERSION "0.0.1"
)

add_compile_options(
  -fdiagnostics-color=always
  #-fsanitize=undefined,address
)
add_link_options(
  -fdiagnostics-color=always
  #-fsanitize=undefined,address
)

include(CTest)

add_library(${PROJECT_NAME})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
    VERSION "${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}"
    SOVERSION "${CMAKE_PROJECT_VERSION_MAJOR}"
)

target_sources(${PROJECT_NAME}
  PRIVATE
    src/io/errors.cpp
)

target_sources(${PROJECT_NAME}
  PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/olifilo/io/errors.hpp
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
    src/coro.cpp
    src/logging-stuff.hpp
  )
  target_link_libraries(hmm PRIVATE ${PROJECT_NAME})

  target_compile_features(hmm PRIVATE cxx_std_23)
endif()
