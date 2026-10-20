idf_component_register(SRCS "main.c" "oled/oled_i2c.c" "oled/font6x8.c" "oled/font16.c" "wifi.c" "web.c"
                       INCLUDE_DIRS "oled"
                       EMBED_FILES "font16.bin")
