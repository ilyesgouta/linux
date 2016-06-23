/*
 * arch/arm/include/debug/stm32f7.S
 *
 * Debugging macro include header
 * Copyright (C) 2016 STMicroelectronics (R&D) Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define VIRT_ADDRESS(x) x

#define DEBUG_LL_UART_BASE  0x40011400 /* USART6 */

#define TX_DR_OFF	    0x28
#define SR_OFF		    0x1c

#define USART_SR_FLAG_TXE   (1 << 7)

		.macro	addruart, rp, rv, tmp
		ldr	\rp,      =DEBUG_LL_UART_BASE
		ldr	\rv,      =VIRT_ADDRESS(DEBUG_LL_UART_BASE)
		.endm

                .macro  senduart,rd,rx
                strb    \rd, [\rx, #TX_DR_OFF]
                .endm

                .macro  waituart,rd,rx
1001:           ldr     \rd, [\rx, #SR_OFF]
                tst     \rd, #USART_SR_FLAG_TXE
                beq     1001b
                .endm

                .macro  busyuart,rd,rx
1001:           ldr     \rd, [\rx, #SR_OFF]
                tst     \rd, #USART_SR_FLAG_TXE
                beq     1001b
                .endm
