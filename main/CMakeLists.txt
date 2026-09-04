idf_component_register(
    SRCS "main.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "app_i18n.c"
         "app_prefs.c"
         "app_time.c"
         "app_dial.c"
         "app_shell.c"
         "app_web.c"
         "app_bg.c"
         "qrcode.c"
         "fonts/lv_font_cjk_12.c"
    INCLUDE_DIRS "."
    REQUIRES bsp nvs_flash esp_netif lwip
             spi_flash driver esp_event esp_http_server
    EMBED_FILES "dial.html"
                "bg_anime.bin" "bg_geek.bin" "bg_ink.bin" "bg_pop.bin"
)
