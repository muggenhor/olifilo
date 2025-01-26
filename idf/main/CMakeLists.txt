idf_component_register(
  SRCS "app_main.cpp"
  PRIV_REQUIRES
    esp_eth
    esp_netif
    esp_wifi
    log
    nvs_flash
    olifilo
    vfs
)
idf_build_set_property(COMPILE_OPTIONS "-Wno-missing-field-initializers" APPEND)
