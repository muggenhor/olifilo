idf_component_register(
  SRCS "app_main.cpp"
  PRIV_REQUIRES
    esp_eth
    esp_netif
    log
    olifilo
    vfs
)
