# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register(
  PRIV_REQUIRES
    esp_common
    log
    nvs_flash
    vfs
  REQUIRES
    esp_eth
    esp_event
    esp_netif
    esp_wifi
  INCLUDE_DIRS
    include
  SRCS
    src/errors.cpp
    src/event.cpp
)

add_subdirectory(olifilo)
target_link_libraries(${COMPONENT_LIB} PUBLIC olifilo::olifilo)
