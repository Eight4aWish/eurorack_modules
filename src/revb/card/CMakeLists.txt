# ComputerCard for Rev B: header-only, plus its two PIO programs
add_library(revb_card INTERFACE)
target_include_directories(revb_card INTERFACE ${CMAKE_CURRENT_LIST_DIR})
pico_generate_pio_header(revb_card ${CMAKE_CURRENT_LIST_DIR}/revb_i2s.pio)
pico_generate_pio_header(revb_card ${CMAKE_CURRENT_LIST_DIR}/revb_conv.pio)
target_link_libraries(revb_card INTERFACE
  pico_stdlib pico_unique_id
  hardware_pio hardware_dma hardware_i2c hardware_spi hardware_pwm hardware_clocks hardware_vreg hardware_irq hardware_sync)
