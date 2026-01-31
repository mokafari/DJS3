idf_component_register(
    SRCS "main.c"
         "../../../../main/audio_output.cpp"
         "../../../../main/audio_player.cpp"
         "../../../../main/storage.c"
         "../../../../main/sd_card.c"
         "usb_host_mock.c"
         "../../../../main/id3_parser.c"
    INCLUDE_DIRS "."
                 "../../../../main/include"
    REQUIRES unity nvs_flash esp8266audio libhelix-mp3 fatfs driver spiffs sdmmc usb esp_driver_i2s
    PRIV_REQUIRES esp_timer
)

