# IDF main component registration. Used by every env that combines
# framework = arduino, espidf (currently esp32_clklink and
# esp32_clklinkrec). Other envs use framework = arduino alone and
# ignore this file.
#
# Selection: $ENV{PIOENV} is a PIO interpolation token, NOT a system
# env var, so CMake can't read it. IDF_TARGET is set by the IDF build
# system from the active chip and IS visible here, so we use it to
# pick the active module's source directory. Add a new branch when a
# new arduino+espidf env is introduced.

if(IDF_TARGET STREQUAL "esp32c5")
    set(module_dir "esp32_clklinkrec")
elseif(IDF_TARGET STREQUAL "esp32")
    set(module_dir "esp32_clklink")
else()
    message(FATAL_ERROR
            "[src/CMakeLists.txt] No module mapped for IDF_TARGET='${IDF_TARGET}'. "
            "Add a branch above when introducing a new arduino+espidf env.")
endif()

file(GLOB_RECURSE app_sources
     "${CMAKE_SOURCE_DIR}/src/${module_dir}/*.cpp"
     "${CMAKE_SOURCE_DIR}/src/${module_dir}/*.c")

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS ".")
