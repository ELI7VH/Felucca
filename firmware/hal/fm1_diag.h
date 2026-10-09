/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only linker allocation sizes. No memory writes or chip register access. */
#pragma once
#include <stdint.h>
extern uint32_t _bss_end[], _pool_start[], _pool_end[], _data_load[], _data_start[], _data_end[], _rt_start[], _rt_end[];
static uint32_t fm1_diag_main_ram(void) { return (uint32_t)((uintptr_t)_bss_end - 0x01C08000u); }
static uint32_t fm1_diag_pool(void) { return (uint32_t)((uintptr_t)_pool_end - (uintptr_t)_pool_start); }
static uint32_t fm1_diag_app(void) { return (uint32_t)((uintptr_t)_data_load + (uintptr_t)_data_end - (uintptr_t)_data_start - 0x02000120u); }
static uint32_t fm1_diag_ram_code(void) { return (uint32_t)((uintptr_t)_rt_end - (uintptr_t)_rt_start); }
