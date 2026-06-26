# LovyanGFX component wrapper for ESP-IDF
# This component downloads and builds LovyanGFX from GitHub

cmake_minimum_required(VERSION 3.20)

# Check if LovyanGFX is already cloned
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/LovyanGFX/CMakeLists.txt")
    message(STATUS "LovyanGFX not found locally. Attempting to download...")
    
    # Try git clone
    find_program(GIT_EXECUTABLE git)
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} clone --depth 1 
                https://github.com/lovyan03/LovyanGFX.git 
                ${CMAKE_CURRENT_LIST_DIR}/LovyanGFX
            RESULT_VARIABLE GIT_RESULT
            ERROR_VARIABLE GIT_ERROR
        )
        if(GIT_RESULT EQUAL 0)
            message(STATUS "LovyanGFX downloaded successfully")
        else()
            message(WARNING "Failed to download LovyanGFX: ${GIT_ERROR}")
        endif()
    else()
        message(WARNING "Git not found. LovyanGFX must be manually cloned.")
    endif()
endif()

# If LovyanGFX exists, add it as a component
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/LovyanGFx/CMakeLists.txt")
    # LovyanGFX has its own ESP-IDF support via a CMakeLists.txt in its root
    # But if not, we need to build it manually
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/LovyanGFX/CMakeLists.txt")
        # Create a minimal CMakeLists for LovyanGFX
        file(WRITE "${CMAKE_CURRENT_LIST_DIR}/LovyanGFX/CMakeLists.txt"
"idf_component_register(
    SRCS
        src/LovyanGFx.cpp
        src/lgfx/v1/platforms/esp32s3/Panel_M5StackCoreS3.cpp
    INCLUDE_DIRS
        src
        src/lgfx/v1
    REQUIRES
        driver
        spi_flash
)
")
    endif()
endif()

# Register our component (it will find LovyanGFX as a subdirectory)
idf_component_register()
