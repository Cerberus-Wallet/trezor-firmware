from __future__ import annotations


def stm32u5_common_files(env, defines, sources, paths):
    defines += [
        ("STM32_HAL_H", '"<stm32u5xx.h>"'),
        ("FLASH_BLOCK_WORDS", "4"),
    ]

    paths += [
        "embed/trezorhal/stm32u5",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Inc",
        "vendor/stm32cube-u5/Drivers/CMSIS/Device/ST/STM32U5xx/Include",
        "vendor/stm32cube-u5/Drivers/CMSIS/Core/Include",
    ]

    sources += [
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_cortex.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_cryp.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_dma.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_dma2d.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_dsi.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_flash.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_flash_ex.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_gfxmmu.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_gpio.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_gtzc.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_i2c.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_i2c_ex.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_icache.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_ltdc.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_ltdc_ex.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pcd.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pcd_ex.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pwr.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pwr_ex.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_rcc.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_rcc_ex.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_rtc.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_sd.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_spi.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_sram.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_tim.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_tim_ex.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_ll_fmc.c",
        "vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_ll_sdmmc.c",
    ]

    sources += [
        "embed/trezorhal/stm32u5/board_capabilities.c",
        "embed/trezorhal/stm32u5/common.c",
        "embed/trezorhal/stm32u5/fault_handlers.c",
        "embed/trezorhal/stm32u5/flash.c",
        "embed/trezorhal/stm32u5/lowlevel.c",
        "embed/trezorhal/stm32u5/mpu.c",
        "embed/trezorhal/stm32u5/platform.c",
        "embed/trezorhal/stm32u5/secret.c",
        "embed/trezorhal/stm32u5/secure_aes.c",
        "embed/trezorhal/stm32u5/systick.c",
        "embed/trezorhal/stm32f4/supervise.c",
        "embed/trezorhal/stm32u5/random_delays.c",
        "embed/trezorhal/stm32u5/rng.c",
        "embed/trezorhal/stm32u5/tamper.c",
        "embed/trezorhal/stm32u5/trustzone.c",
        "embed/trezorhal/stm32u5/vectortable.s",
    ]

    # boardloader needs separate assembler for some function unencumbered by various FW+bootloader hacks
    # this helps to prevent making a bug in boardloader which may be hard to fix since it's locked with write-protect
    env_constraints = env.get("CONSTRAINTS")
    if env_constraints and "limited_util_s" in env_constraints:
        sources += [
            "embed/trezorhal/stm32u5/limited_util.s",
        ]
    else:
        sources += [
            "embed/trezorhal/stm32u5/util.s",
        ]

    env.get("ENV")["RUST_INCLUDES"] = (
        "-I../trezorhal/stm32u5;"
        "-I../../vendor/stm32cube-u5/Drivers/STM32U5xx_HAL_Driver/Inc;"
        "-I../../vendor/stm32cube-u5/Drivers/CMSIS/Device/ST/STM32U5xx/Include;"
        "-I../../vendor/stm32cube-u5/Drivers/CMSIS/Core/Include;"
        "-DSTM32_HAL_H=<stm32u5xx.h>;"
        "-DSTM32U5;"
        "-DFLASH_BLOCK_WORDS=4"
    )

    env.get("ENV")["SUFFIX"] = "stm32u5"
