# Delivery Box firmware — ESP32-S3-WROOM-1 N16R8
# Build:  idf.py build      Flash + monitor:  idf.py -p /dev/ttyACM0 flash monitor
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(delivery-box-final)
