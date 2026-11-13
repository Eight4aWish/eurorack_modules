# Bends (Vincent Maurer, MIT), unmodified from the Workshop Computer repository: its sources are
# in third_party/workshop_computer/bends. Rev B's ComputerCard.h stands in for the one it ships with.
set(BENDS_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/workshop_computer/bends)
add_executable(bends ${BENDS_DIR}/bends.cpp)
target_include_directories(bends PRIVATE ${BENDS_DIR})
target_compile_definitions(bends PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64 REVB_APP_NAME="BENDS")
target_link_libraries(bends revb_card pico_multicore hardware_flash hardware_interp hardware_adc hardware_timer)
pico_enable_stdio_uart(bends 0)
pico_enable_stdio_usb(bends 0)
pico_add_extra_outputs(bends)
