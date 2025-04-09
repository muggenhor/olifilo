idf_component_register(
  SRCS "app_main.cpp"
  PRIV_REQUIRES
    esp_eth
    esp_netif
    esp_wifi
    log
    nvs_flash
    olifilo
)
idf_build_set_property(COMPILE_OPTIONS "-Wno-missing-field-initializers" APPEND)

nvs_create_partition_image(nvs nvs-data.csv FLASH_IN_PROJECT)
install(FILES
  ${CMAKE_BINARY_DIR}/nvs.bin
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/${CMAKE_PROJECT_NAME}
)
