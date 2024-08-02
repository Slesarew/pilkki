
add_executable(
    pilkki-firmware

    adc.c
    crc.c
    dma.c
    gpio.c
    main.c
    rng.c
    spi.c
    stm32g4xx_hal_msp.c
    stm32g4xx_it.c
    syscalls.c
    sysmem.c
    system_stm32g4xx.c
    usart.c

    dap.c
    # debug_lock.c
    errors.c
    # flash_write.c
    # unlock_gecko.c
    # unlock_zero.c
    use_flashloader.c
    utils.c

    payload_bindings.c

    startup_stm32g431kbux.S
    # target-efm32pg23-obj
)

# target_include_directories(pilkki-firmware PRIVATE programmer)

target_link_libraries(
    pilkki-firmware
    PRIVATE
    pilkki-firmware-common
    stm32g4xx_cmsis
    stm32g4xx_hal
    stm32g4xx_usb

    target-efm32pg23-objlib
)

target_link_options(pilkki-firmware PRIVATE -T ${CMAKE_CURRENT_SOURCE_DIR}/STM32G431KBUX_FLASH.ld)


add_custom_target(
  pilkki-firmware-bin
  ALL
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/pilkki-firmware.bin
  DEPENDS pilkki-firmware
  COMMENT "generate .bin file"
  COMMAND ${OBJCOPY_BIN} -O binary ${CMAKE_CURRENT_BINARY_DIR}/pilkki-firmware
    ${CMAKE_CURRENT_BINARY_DIR}/pilkki-firmware.bin
)

add_custom_target(
    pilkki-flash-dfu
    DEPENDS pilkki-firmware-bin
    COMMENT "Flash pilkki via DFU"
    COMMAND dfu-util -a 0 -i 0 -s 0x08000000 -D ${CMAKE_CURRENT_BINARY_DIR}/pilkki-firmware.bin
)