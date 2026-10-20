# delivery-box-final
# Delivery Box firmware — ESP32-S3-WROOM-1 N16R8
# Build:  idf.py build      Flash + monitor:  idf.py -p <port> flash monitor
cmake_minimum_required(VERSION 3.16)

# With micro-ROS (microphone stream, components/micro_ros_espidf_component
# present) the native USB port carries ROS, so the console moves to UART0:
# sdkconfig.microros is applied on top of sdkconfig.defaults. It only takes
# effect when sdkconfig is (re)created: delete sdkconfig after adding or
# removing the micro-ROS component.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/components/micro_ros_espidf_component/CMakeLists.txt")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.microros")
    message(STATUS "micro-ROS found: microphone stream ON, console on UART0")
else()
    message(STATUS "no micro-ROS component: microphone stream OFF")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(delivery-box-final)
