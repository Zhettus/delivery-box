idf_component_register(
    SRCS "main.c"
         "states.c"
         "motor.c"
         "led.c"
         "face.c"
         "tail_light.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_timer led_strip pwm_util door ina228)
