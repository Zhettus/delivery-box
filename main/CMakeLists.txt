idf_component_register(
    SRCS "final.c"
         "components/led/led.c"
         "components/eyes/eyes.c"
         "states.c"
         "components/tail-light/tail-light.c"
    INCLUDE_DIRS "."
                 "components/led"
                 "components/eyes"
                 "components/tail-light"
    PRIV_REQUIRES led_strip esp_timer)