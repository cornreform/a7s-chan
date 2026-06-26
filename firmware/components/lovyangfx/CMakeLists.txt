# LovyanGFX component for ESP-IDF
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/LovyanGFX/CMakeLists.txt")
    message(STATUS "LovyanGFX not found. Cloning...")
    find_program(GIT git)
    if(GIT)
        execute_process(COMMAND ${GIT} clone --depth 1
            https://github.com/lovyan03/LovyanGFX.git
            ${CMAKE_CURRENT_LIST_DIR}/LovyanGFX
        )
    endif()
endif()

# LovyanGFX uses its own CMakeLists.txt for ESP-IDF
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/LovyanGFX/CMakeLists.txt")
    idf_component_register(
        REQUIRES driver spi_flash
    )
    add_subdirectory(LovyanGFX ${CMAKE_CURRENT_BINARY_DIR}/lovyangfx)
else()
    # Manual registration if LovyanGFX doesn't have ESP-IDF support yet
    file(GLOB LG_SRCS
        "${CMAKE_CURRENT_LIST_DIR}/LovyanGFX/src/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/LovyanGFX/src/lgfx/v1/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/LovyanGFX/src/lgfx/v1/platforms/esp32s3/*.cpp"
    )
    idf_component_register(
        SRCS ${LG_SRCS}
        INCLUDE_DIRS
            LovyanGFX/src
            LovyanGFX/src/lgfx/v1
        REQUIRES driver spi_flash
    )
endif()
