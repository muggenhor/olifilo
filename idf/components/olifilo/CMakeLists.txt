# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register(
  PRIV_REQUIRES
    esp_common
    esp_netif
    esp_wifi
    nvs_flash
  REQUIRES
    esp_event
  INCLUDE_DIRS
    include
  SRCS
    src/errors.cpp
    src/event.cpp
)

add_subdirectory(olifilo)
target_link_libraries(${COMPONENT_LIB} PUBLIC olifilo::olifilo)
