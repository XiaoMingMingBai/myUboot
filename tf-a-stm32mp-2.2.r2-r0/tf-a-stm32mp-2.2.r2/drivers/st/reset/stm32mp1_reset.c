/*
 * Copyright (c) 2018-2020, STMicroelectronics - All Rights Reserved
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <limits.h>

#include <lib/libc/errno.h>

#include <platform_def.h>

#include <common/bl_common.h>
#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <drivers/st/stm32mp_reset.h>
#include <lib/mmio.h>
#include <lib/utils_def.h>

static uint32_t id2reg_offset(unsigned int reset_id)
{
	return ((reset_id & GENMASK(31, 5)) >> 5) * sizeof(uint32_t);
}

static uint8_t id2reg_bit_pos(unsigned int reset_id)
{
	return (uint8_t)(reset_id & GENMASK(4, 0));
}

int stm32mp_reset_assert_to(uint32_t id, unsigned int to_us)
{
	uint32_t offset = id2reg_offset(id);
	uint32_t bitmsk = BIT(id2reg_bit_pos(id));
	uintptr_t rcc_base = stm32mp_rcc_base();

	mmio_write_32(rcc_base + offset, bitmsk);

	if (to_us != 0) {
		uint64_t timeout_ref = timeout_init_us(to_us);

		while ((mmio_read_32(rcc_base + offset) & bitmsk) == 0U) {
			if (timeout_elapsed(timeout_ref)) {
				break;
			}
		}

		if ((mmio_read_32(rcc_base + offset) & bitmsk) == 0U) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

int stm32mp_reset_deassert_to(uint32_t id, unsigned int to_us)
{
	uint32_t offset = id2reg_offset(id) + RCC_RSTCLRR_OFFSET;
	uint32_t bitmsk = BIT(id2reg_bit_pos(id));
	uintptr_t rcc_base = stm32mp_rcc_base();

	mmio_write_32(rcc_base + offset, bitmsk);

	if (to_us != 0) {
		uint64_t timeout_ref = timeout_init_us(to_us);

		while ((mmio_read_32(rcc_base + offset) & bitmsk) != 0U) {
			if (timeout_elapsed(timeout_ref)) {
				break;
			}
		}

		if ((mmio_read_32(rcc_base + offset) & bitmsk) != 0U) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

void stm32mp_reset_assert_deassert_to_mcu(bool assert_not_deassert)
{
	uintptr_t rcc_base = stm32mp_rcc_base();

	/*
	 * The RCC_MP_GCR is a read/write register.
	 * Assert the MCU HOLD_BOOT means clear the BOOT_MCU bit
	 * Deassert the MCU HOLD_BOOT means set the BOOT_MCU the bit
	 */
	if (assert_not_deassert) {
		mmio_clrbits_32(rcc_base + RCC_MP_GCR, RCC_MP_GCR_BOOT_MCU);
	} else {
		mmio_setbits_32(rcc_base + RCC_MP_GCR, RCC_MP_GCR_BOOT_MCU);
	}
}
