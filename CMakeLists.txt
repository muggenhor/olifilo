# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.28)

project(olifilo
  LANGUAGES CXX
  VERSION "0.0.1"
)

if(UNIX AND (
       (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.9"))
    AND CMAKE_GENERATOR STREQUAL "Ninja")
  # These compilers generate coloured output, but don't do so by default when writing to a pipe.
  # Ninja captures in a pipe *and* subsequently strips out the VT100 codes (used for color) when Ninja
  # itself isn't writing to a terminal. So this is both desirable and safe with Ninja.
  add_compile_options(-fdiagnostics-color=always)
  add_link_options(-fdiagnostics-color=always)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(
    -Werror
    -Wall
    -Wextra
    -Wconversion
    -Wno-missing-field-initializers
    -Wno-mismatched-new-delete
)
endif()
#add_compile_options(
#  -fsanitize=undefined,address
#)
#add_link_options(
#  -fsanitize=undefined,address
#)

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
    src/coro/io_poll_context.cpp
    src/coro/wait.cpp
    src/errors.cpp
    src/io/file_descriptor.cpp
)

target_sources(${PROJECT_NAME}
  PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/olifilo/coro/future.hpp
      include/olifilo/coro/detail/forward.hpp
      include/olifilo/coro/detail/io_poll_context.hpp
      include/olifilo/coro/detail/promise.hpp
      include/olifilo/coro/io/file_descriptor.hpp
      include/olifilo/coro/io/types.hpp
      include/olifilo/coro/wait.hpp
      include/olifilo/coro/when_all.hpp
      include/olifilo/coro/when_any.hpp
      include/olifilo/detail/small_vector.hpp
      include/olifilo/detail/variant_ptr.hpp
      include/olifilo/dynarray.hpp
      include/olifilo/errors.hpp
      include/olifilo/io/connect.hpp
      include/olifilo/io/fcntl.hpp
      include/olifilo/io/poll.hpp
      include/olifilo/io/read.hpp
      include/olifilo/io/select.hpp
      include/olifilo/io/socket.hpp
      include/olifilo/io/sockopt.hpp
      include/olifilo/io/sockopts/base.hpp
      include/olifilo/io/sockopts/socket.hpp
      include/olifilo/io/sockopts/tcp.hpp
      include/olifilo/io/shutdown.hpp
      include/olifilo/io/types.hpp
      include/olifilo/io/write.hpp
      include/olifilo/expected.hpp
)

include(GNUInstallDirs)
install(
  TARGETS
    ${PROJECT_NAME}
  EXPORT
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
if(NOT DEFINED ESP_PLATFORM)
  install(EXPORT
    ${PROJECT_NAME}
    FILE ${PROJECT_NAME}.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
  )

  if(BUILD_TESTING)
    add_executable(hmm)
    target_sources(hmm PRIVATE
      src/coro.cpp
      src/logging-stuff.hpp
    )
    target_link_libraries(hmm PRIVATE ${PROJECT_NAME})

    target_compile_features(hmm PRIVATE cxx_std_23)

    add_executable(test-variant-ptr)
    target_sources(test-variant-ptr PRIVATE
      tests/variant_ptr.cpp
    )
    target_link_libraries(test-variant-ptr PRIVATE ${PROJECT_NAME})
    add_test(NAME test-variant-ptr COMMAND test-variant-ptr)
  endif()
endif()
