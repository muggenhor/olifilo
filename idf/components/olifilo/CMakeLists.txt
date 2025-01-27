# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register(
  PRIV_REQUIRES
    esp_common
    esp_netif
    esp_wifi
    nvs_flash
  INCLUDE_DIRS
    include
  SRCS
    src/errors.cpp
)

add_subdirectory(olifilo)
target_link_libraries(${COMPONENT_LIB} INTERFACE olifilo::olifilo)
