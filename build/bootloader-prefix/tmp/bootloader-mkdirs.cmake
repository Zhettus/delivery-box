# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/zhettus/.espressif/v5.2.7/esp-idf/components/bootloader/subproject"
  "/home/zhettus/delivery-box/build/bootloader"
  "/home/zhettus/delivery-box/build/bootloader-prefix"
  "/home/zhettus/delivery-box/build/bootloader-prefix/tmp"
  "/home/zhettus/delivery-box/build/bootloader-prefix/src/bootloader-stamp"
  "/home/zhettus/delivery-box/build/bootloader-prefix/src"
  "/home/zhettus/delivery-box/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/zhettus/delivery-box/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/zhettus/delivery-box/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
