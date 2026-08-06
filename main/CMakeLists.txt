idf_component_register(
    SRCS "tail-light-check.c"
         "components/led/led.c"
         "components/eyes/eyes.c"
         "components/tail-light/mosfet.c"
    INCLUDE_DIRS "."
                 "components/led"
                 "components/eyes"
                 "components/tail-light"
    PRIV_REQUIRES led_strip esp_timer)