# The MOD2 app: HAGIWO's MOD2 voices (CC0) behind a voice menu. docs/REVB_MOD2.md
add_executable(mod2 mod2.cpp)
target_link_libraries(mod2 revb_card hardware_flash)
target_compile_options(mod2 PRIVATE -Wall -Wextra -Wdouble-promotion)
pico_enable_stdio_uart(mod2 0)
pico_enable_stdio_usb(mod2 0)
pico_add_extra_outputs(mod2)
