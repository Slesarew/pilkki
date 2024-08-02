
add_executable(
    target-efm32pg23
    flashloader.c
    startup_efm32pg23.c
    AT25.c
)

target_link_libraries(
    target-efm32pg23
    PRIVATE
    gecko-platform-emlib
)

target_link_libraries(gecko-platform-emlib PUBLIC gecko-device-efm32pg23-nostart )

target_link_options(target-efm32pg23 PRIVATE -T ${CMAKE_CURRENT_SOURCE_DIR}/EFM32PG23B200F512IM40_sram.lds)



add_custom_target(
  target-efm32pg23-bin
  ALL
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/target-efm32pg23.bin
  DEPENDS target-efm32pg23
  COMMENT "generate .bin file"
  COMMAND ${OBJCOPY_BIN} -O binary ${CMAKE_CURRENT_BINARY_DIR}/target-efm32pg23
    ${CMAKE_CURRENT_BINARY_DIR}/target-efm32pg23.bin
)
