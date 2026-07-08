# CMakeLists.txt for DeBreathalyzer VST3 Plugin

# Minimum required CMake version
cmake_minimum_required(VERSION 3.15)

# Project name
project(DeBreathalyzerVST3 VERSION 1.0.0 LANGUAGES CXX)

# Find the VST3 SDK.
# You need to set VST3_SDK_PATH environment variable or CMake variable
# to point to the root directory of your Steinberg VST3 SDK.
if (NOT DEFINED VST3_SDK_PATH)
    message(FATAL_ERROR "VST3_SDK_PATH is not defined. Please set it to the root of your VST3 SDK.")
endif()

# Include the VST3 SDK CMake modules
set(CMAKE_MODULE_PATH "${VST3_SDK_PATH}/cmake" ${CMAKE_MODULE_PATH})
find_package(VST3SDK REQUIRED)

# Add our plugin as a VST3 module
add_vst3_plugin(
    DeBreathalyzer
    SOURCES
        debreathalyzerprocessor.cpp
        debreathalyzerprocessor.h
        debreathalyzercontroller.cpp
        debreathalyzercontroller.h
        parameters.h
        pluginfactory.cpp
    BUNDLE_NAME "DeBreathalyzer.vst3"
    SDK_PATH ${VST3_SDK_PATH}
    VENDOR "YourCompany"
    URL "www.yourcompany.com"
    EMAIL "info@yourcompany.com"
    VERSION "1.0.0"
    COPYRIGHT "YourCompany © 2023"
)

# Optional: Add common build settings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # For GCC/Clang
    target_compile_options(DeBreathalyzer PRIVATE -Wall -Wextra -pedantic)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    # For MSVC
    target_compile_options(DeBreathalyzer PRIVATE /W4)
endif()

# Optional: Set output directory for the VST3 bundle
# By default, it goes into build/VST3
# set_target_properties(DeBreathalyzer PROPERTIES
#     ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VST3"
#     LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VST3"
#     RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VST3"
# )

# Optional: Install the plugin
# install(TARGETS DeBreathalyzer DESTINATION "${CMAKE_INSTALL_PREFIX}/VST3")