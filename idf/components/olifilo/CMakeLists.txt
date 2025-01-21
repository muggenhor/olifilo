# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register()

add_subdirectory(olifilo)
target_link_libraries(${COMPONENT_LIB} INTERFACE olifilo::olifilo)
