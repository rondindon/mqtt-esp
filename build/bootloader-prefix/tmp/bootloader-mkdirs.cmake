# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/ronni/esp/esp-idf/components/bootloader/subproject"
  "C:/FF15/tcp/build/bootloader"
  "C:/FF15/tcp/build/bootloader-prefix"
  "C:/FF15/tcp/build/bootloader-prefix/tmp"
  "C:/FF15/tcp/build/bootloader-prefix/src/bootloader-stamp"
  "C:/FF15/tcp/build/bootloader-prefix/src"
  "C:/FF15/tcp/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/FF15/tcp/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/FF15/tcp/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
