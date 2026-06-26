cmake_minimum_required(VERSION 5.0)

set(EXTRA_COMPONENT_DIRS
    components
)

# Force target to ESP32-S3
set(IDF_TARGET "esp32s3" CACHE STRING "")

# Include the ESP-IDF project build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(a7s-firmware)
