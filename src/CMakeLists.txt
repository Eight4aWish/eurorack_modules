# IDF main component registration for the esp32-clklink env (the only env
# in this repo that uses framework = arduino, espidf). Other envs use
# framework = arduino alone and ignore this file.
#
# Scoped to esp32-clklink/ explicitly because PIO's default global GLOB
# would compile every other env's sources too.

file(GLOB_RECURSE app_sources
     "${CMAKE_SOURCE_DIR}/src/esp32-clklink/*.cpp"
     "${CMAKE_SOURCE_DIR}/src/esp32-clklink/*.c")

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS ".")
