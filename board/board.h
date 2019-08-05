/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-5      SummerGift   first version
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include <rtthread.h>
#include <stm32l4xx.h>
#include "drv_common.h"
#include "drv_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __ICCARM__
// Use *.icf ram symbal, to avoid hardcode.
extern char __ICFEDIT_region_IRAM1_end__;
#define STM32_SRAM_END          &__ICFEDIT_region_IRAM1_end__
#else
#define STM32_SRAM_SIZE         96
#define STM32_SRAM_END          (0x20000000 + STM32_SRAM_SIZE * 1024)
#endif

#ifdef __CC_ARM
extern int Image$$RW_IRAM1$$ZI$$Limit;
#define HEAP_BEGIN    (&Image$$RW_IRAM1$$ZI$$Limit)
#elif __ICCARM__
#pragma section="HEAP"
#define HEAP_BEGIN    (__segment_end("HEAP"))
#else
extern int __bss_end;
#define HEAP_BEGIN    (&__bss_end)
#endif

#define HEAP_END                STM32_SRAM_END
#define STM32_SRAM2_SIZE        32
#define STM32_SRAM2_BEGIN       (0x10000000u)
#define STM32_SRAM2_END         (0x10000000 + STM32_SRAM2_SIZE * 1024)
#define STM32_SRAM2_HEAP_SIZE   ((uint32_t)STM32_SRAM2_END - (uint32_t)STM32_SRAM2_BEGIN)

#define STM32_FLASH_START_ADRESS       ((uint32_t)0x08000000)
#define STM32_FLASH_SIZE               (512 * 1024)
#define STM32_FLASH_END_ADDRESS        ((uint32_t)(STM32_FLASH_START_ADRESS + STM32_FLASH_SIZE))

void SystemClock_Config(void);
void SystemClock_MSI_ON(void);
void SystemClock_MSI_OFF(void);
void SystemClock_80M(void);
void SystemClock_24M(void);
void SystemClock_2M(void);
void SystemClock_ReConfig(uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif

