set(srcs "main.c"
         "states.c"
         "motor.c"
         "led.c"
         "face.c"
         "tail_light.c"
         "sound.c")
set(requires driver esp_timer led_strip pwm_util door ina228 audio)

# The microphone stream needs micro-ROS. It is built when the micro-ROS
# component is in components/ (not in git: too big, see README.md).
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../components/micro_ros_espidf_component/CMakeLists.txt")
    set(mic_ros 1)
    list(APPEND srcs "mic_ros.c" "usb_jtag_transport.c")
    list(APPEND requires micro_ros_espidf_component)
else()
    set(mic_ros 0)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    PRIV_REQUIRES ${requires}
    EMBED_FILES "gm.wav")

target_compile_definitions(${COMPONENT_LIB} PRIVATE MIC_ROS=${mic_ros})
