idf_component_register(
    SRCS "main.c"
         "flomo_ui.c"
         "flomo_ui_model.c"
         "flomo_config.c"
         "flomo_config_json.c"
         "flomo_json.c"
         "flomo_wav.c"
         "flomo_queue_policy.c"
         "flomo_store.c"
         "flomo_recorder.c"
         "flomo_wifi.c"
         "flomo_uploader.c"
         "flomo_blufi.c"
         "flomo_blufi_security.c"
         "flomo_websrv.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt nvs_flash esp_event esp_hw_support esp_netif esp_timer esp_wifi esp_http_server esp-tls mbedtls esp_littlefs esp_http_client
)

# 中文子集字库(assets/fonts/README 记录来源与再生成命令)。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/flomo_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/flomo_font_24.c"
)
