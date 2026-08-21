idf_component_register(
    SRCS "led-check.c"
         "components/led/led-pulsation.c"
         "components/eyes/eyes.c"
         "states.c"
         "components/tail-light/tail-light.c"
    INCLUDE_DIRS "."
                 "components/led"
                 "components/eyes"
                 "components/tail-light"
    PRIV_REQUIRES led_strip esp_timer)