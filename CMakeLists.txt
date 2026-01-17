cmake_minimum_required(VERSION 3.25)

project(obs-svt-av1 VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable modern compiler warnings
if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
  # Removed strict sign conversion warnings for legacy compatibility
  # add_compile_options(-Wconversion -Wsign-conversion)
endif()

# Dependencies
find_package(libobs REQUIRED)

# SVT-AV1 (via PkgConfig)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SVT_AV1 REQUIRED IMPORTED_TARGET SvtAv1Enc)

# Configure plugin-support.c
configure_file(
  src/plugin-support.c.in
  ${CMAKE_BINARY_DIR}/src/plugin-support.c
  @ONLY
)

# Plugin target
add_library(${CMAKE_PROJECT_NAME} MODULE)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  src/plugin-main.cpp
  src/svt-av1-encoder.cpp
  src/svt-av1-encoder.hpp
  ${CMAKE_BINARY_DIR}/src/plugin-support.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
  OBS::libobs
  PkgConfig::SVT_AV1
)

# Set output name (standard OBS plugin naming convention)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
  OUTPUT_NAME "obs-svt-av1"
  PREFIX ""
)

# Install location (standard OBS plugin structure)
if(NOT DEFINED OBS_PLUGIN_DESTINATION)
    if(WIN32)
        set(OBS_PLUGIN_DESTINATION "obs-plugins/64bit")
    else()
        set(OBS_PLUGIN_DESTINATION "lib/obs-plugins")
    endif()
endif()

install(TARGETS ${CMAKE_PROJECT_NAME}
    LIBRARY DESTINATION ${OBS_PLUGIN_DESTINATION}
    RUNTIME DESTINATION ${OBS_PLUGIN_DESTINATION}
)
