# Which program to build (the choice is remembered in the build folder):
#
#   idf.py -DAUDIO_CHECK=0 build   -> the Delivery Box firmware (main.c)
#   idf.py -DAUDIO_CHECK=1 build   -> audio_check.c: plays gm.wav in a loop
#                                     to check the WM8960 Audio HAT
#   idf.py -DMIC_CHECK=1 build     -> mic_check.c: microphone level meter +
#                                     wiring diagnosis (+ record/play back
#                                     with the HAT)
# Set the one you used back to 0 to build the firmware again.
option(AUDIO_CHECK "Build main/audio_check.c instead of the firmware" OFF)
option(MIC_CHECK "Build main/mic_check.c instead of the firmware" OFF)

if(AUDIO_CHECK AND MIC_CHECK)
    message(FATAL_ERROR "AUDIO_CHECK and MIC_CHECK are both 1: pick one "
                        "(e.g. idf.py -DAUDIO_CHECK=0 -DMIC_CHECK=1 build)")
elseif(AUDIO_CHECK)
    message(STATUS "*** Building the AUDIO HAT CHECK (audio_check.c) ***")
    set(srcs "audio_check.c" "sound.c")
elseif(MIC_CHECK)
    message(STATUS "*** Building the MICROPHONE CHECK (mic_check.c) ***")
    set(srcs "mic_check.c" "sound.c")
else()
    set(srcs "main.c"
             "states.c"
             "motor.c"
             "led.c"
             "face.c"
             "tail_light.c"
             "sound.c")
endif()
set(requires driver esp_timer led_strip pwm_util door ina228 audio)

# The microphone stream needs micro-ROS. It is built when the micro-ROS
# component is in components/ (not in git: too big, see README.md).
if(NOT AUDIO_CHECK AND NOT MIC_CHECK AND
   EXISTS "${CMAKE_CURRENT_LIST_DIR}/../components/micro_ros_espidf_component/CMakeLists.txt")
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
