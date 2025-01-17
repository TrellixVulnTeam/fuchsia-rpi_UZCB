// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>
#include <lib/cbuf.h>
#include <lib/debuglog.h>
#include <lib/zx/status.h>
#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>

#include <arch/arm64/periphmap.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <kernel/thread.h>
#include <pdev/driver.h>
#include <pdev/uart.h>
#include <platform/debug.h>

// PL011 implementation

// clang-format off
#define UART_DR    (0x00)
#define UART_RSR   (0x04)
#define UART_FR    (0x18)
#define UART_ILPR  (0x20)
#define UART_IBRD  (0x24)
#define UART_FBRD  (0x28)
#define UART_LCRH  (0x2c)
#define UART_CR    (0x30)
#define UART_IFLS  (0x34)
#define UART_IMSC  (0x38)
#define UART_TRIS  (0x3c)
#define UART_TMIS  (0x40)
#define UART_ICR   (0x44)
#define UART_DMACR (0x48)
// clang-format on

#define UARTREG(base, reg) (*REG32((base) + (reg)))

#define RXBUF_SIZE 16

// values read from zbi
static vaddr_t uart_base = 0;
static uint32_t uart_irq = 0;

static Cbuf uart_rx_buf;

/*
 * Tx driven irq:
 * NOTE: For the pl011, txim is the "ready to transmit" interrupt. So we must
 * mask it when we no longer care about it and unmask it when we start
 * xmitting.
 */
static bool uart_tx_irq_enabled = false;
static AutounsignalEvent uart_dputc_event{true};

static SpinLock uart_spinlock;

static inline void pl011_mask_tx() { UARTREG(uart_base, UART_IMSC) &= ~(1 << 5); }

static inline void pl011_unmask_tx() { UARTREG(uart_base, UART_IMSC) |= (1 << 5); }

static interrupt_eoi pl011_uart_irq(void* arg) {
  /* read interrupt status and mask */
  uint32_t isr = UARTREG(uart_base, UART_TMIS);

  if (isr & ((1 << 4) | (1 << 6))) {  // rxmis
    /* while fifo is not empty, read chars out of it */
    while ((UARTREG(uart_base, UART_FR) & (1 << 4)) == 0) {
      /* if we're out of rx buffer, mask the irq instead of handling it */
      if (uart_rx_buf.Full()) {
        UARTREG(uart_base, UART_IMSC) &= ~((1 << 4) | (1 << 6));  // !rxim
        break;
      }

      char c = static_cast<char>(UARTREG(uart_base, UART_DR));
      uart_rx_buf.WriteChar(c);
    }
  }
  uart_spinlock.Acquire();
  if (isr & (1 << 5)) {
    /*
     * Signal any waiting Tx and mask Tx interrupts once we
     * wakeup any blocked threads
     */
    uart_dputc_event.Signal();
    pl011_mask_tx();
  }
  uart_spinlock.Release();

  return IRQ_EOI_DEACTIVATE;
}

static void pl011_uart_init(const void* driver_data, uint32_t length) {
  // Initialize circular buffer to hold received data.
  uart_rx_buf.Initialize(RXBUF_SIZE, malloc(RXBUF_SIZE));

  // assumes interrupts are contiguous
  zx_status_t status = register_int_handler(uart_irq, &pl011_uart_irq, NULL);
  DEBUG_ASSERT(status == ZX_OK);

  // clear all irqs
  UARTREG(uart_base, UART_ICR) = 0x3ff;

  // set fifo trigger level
  UARTREG(uart_base, UART_IFLS) = 0;  // 1/8 rxfifo, 1/8 txfifo

  // enable rx interrupt
  UARTREG(uart_base, UART_IMSC) = (1 << 4) |  //  rxim
                                  (1 << 6);   //  rtim

  // enable receive
  UARTREG(uart_base, UART_CR) |= (1 << 9);  // rxen

  // enable interrupt
  unmask_interrupt(uart_irq);

  if (dlog_bypass() == true) {
    uart_tx_irq_enabled = false;
  } else {
    /* start up tx driven output */
    printf("UART: started IRQ driven TX\n");
    uart_tx_irq_enabled = true;
  }
}

static int pl011_uart_getc(bool wait) {
  zx::status<char> result = uart_rx_buf.ReadChar(wait);
  if (result.is_ok()) {
    UARTREG(uart_base, UART_IMSC) |= ((1 << 4) | (1 << 6));  // rxim
    return result.value();
  }

  return result.error_value();
}

/* panic-time getc/putc */
static void pl011_uart_pputc(char c) {
  /* spin while fifo is full */
  while (UARTREG(uart_base, UART_FR) & (1 << 5))
    ;
  UARTREG(uart_base, UART_DR) = c;
}

static int pl011_uart_pgetc() {
  if ((UARTREG(uart_base, UART_FR) & (1 << 4)) == 0) {
    return UARTREG(uart_base, UART_DR);
  } else {
    return -1;
  }
}

static void pl011_dputs(const char* str, size_t len, bool block, bool map_NL) {
  interrupt_saved_state_t state;
  bool copied_CR = false;

  if (!uart_tx_irq_enabled) {
    block = false;
  }
  uart_spinlock.AcquireIrqSave(state);
  while (len > 0) {
    // Is FIFO Full ?
    while (UARTREG(uart_base, UART_FR) & (1 << 5)) {
      if (block) {
        /* Unmask Tx interrupts before we block on the event */
        pl011_unmask_tx();
        uart_spinlock.ReleaseIrqRestore(state);
        uart_dputc_event.Wait();
      } else {
        uart_spinlock.ReleaseIrqRestore(state);
        arch::Yield();
      }
      uart_spinlock.AcquireIrqSave(state);
    }
    if (!copied_CR && map_NL && *str == '\n') {
      copied_CR = true;
      UARTREG(uart_base, UART_DR) = '\r';
    } else {
      copied_CR = false;
      UARTREG(uart_base, UART_DR) = *str++;
      len--;
    }
  }
  uart_spinlock.ReleaseIrqRestore(state);
}

static void pl011_start_panic() { uart_tx_irq_enabled = false; }

static const struct pdev_uart_ops uart_ops = {
    .getc = pl011_uart_getc,
    .pputc = pl011_uart_pputc,
    .pgetc = pl011_uart_pgetc,
    .start_panic = pl011_start_panic,
    .dputs = pl011_dputs,
};

static void pl011_uart_init_early(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_simple_t));
  auto driver = static_cast<const dcfg_simple_t*>(driver_data);
  ASSERT(driver->mmio_phys && driver->irq);

  uart_base = periph_paddr_to_vaddr(driver->mmio_phys);
  ASSERT(uart_base);
  uart_irq = driver->irq;

  UARTREG(uart_base, UART_CR) = (1 << 8) | (1 << 0);  // tx_enable, uarten

  pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(pl011_uart_init_early, KDRV_PL011_UART, pl011_uart_init_early,
             LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(pl011_uart_init, KDRV_PL011_UART, pl011_uart_init, LK_INIT_LEVEL_PLATFORM)
