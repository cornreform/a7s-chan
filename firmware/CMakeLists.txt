cmake_minimum_required(VERSION 3.20)

# Set components dir for LovyanGFX and other custom components
set(EXTRA_COMPONENT_DIRS components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stackchan_firmware)
