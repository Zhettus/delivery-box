# Door-switch test: prints 1 while the switch is pressed (door closed), else 0.
# Build + run:  idf.py build flash monitor     (from this folder)
cmake_minimum_required(VERSION 3.16)

# Only this test's own code, none of the box's components.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(switch_test)
