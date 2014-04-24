/*
 * Fujitsu FM3 Interrupts
 *
 * Copyright (c) 2012 Pylone, Inc.
 * Written by Masashi YOKOTA <yokota@pylone.jp>
 *
 * This code is licensed under the GNU GPL v2.
 */

#include "sysbus.h"
#include "sysemu.h"
#include "qerror.h"
#include "fm3.h"

//#define FM3_DEBUG_INT

#ifdef FM3_DEBUG_INT
#define DPRINTF(fmt, ...)                                       \
    do { printf(fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

typedef struct {
    SysBusDevice busdev;
    MemoryRegion mmio;
    qemu_irq parent[FM3_IRQ_NUM];
    uint32_t extint_0_7;
    uint32_t extint_8_31;
    uint32_t mfs_rx[8];
    uint32_t mfs_tx_status[8];
} Fm3IntState;

#define FM3_INT_EXC02MON            (0x10) 
#define FM3_INT_IRQ00MON            (0x14) 
#define FM3_INT_IRQ01MON            (0x18) 
#define FM3_INT_IRQ02MON            (0x1C) 
#define FM3_INT_IRQ03MON            (0x20) 
#define FM3_INT_IRQ04MON            (0x24) 
#define FM3_INT_IRQ05MON            (0x28) 
#define FM3_INT_IRQ06MON            (0x2C) 
#define FM3_INT_IRQ07MON            (0x30) 
#define FM3_INT_IRQ08MON            (0x34) 
#define FM3_INT_IRQ09MON            (0x38) 
#define FM3_INT_IRQ10MON            (0x3C) 
#define FM3_INT_IRQ11MON            (0x40) 
#define FM3_INT_IRQ12MON            (0x44) 
#define FM3_INT_IRQ13MON            (0x48) 
#define FM3_INT_IRQ14MON            (0x4C) 
#define FM3_INT_IRQ15MON            (0x50) 
#define FM3_INT_IRQ16MON            (0x54) 
#define FM3_INT_IRQ17MON            (0x58) 
#define FM3_INT_IRQ18MON            (0x5C) 
#define FM3_INT_IRQ19MON            (0x60) 
#define FM3_INT_IRQ20MON            (0x64) 
#define FM3_INT_IRQ21MON            (0x68) 
#define FM3_INT_IRQ22MON            (0x6C) 
#define FM3_INT_IRQ23MON            (0x70) 
#define FM3_INT_IRQ24MON            (0x74) 
#define FM3_INT_IRQ25MON            (0x78) 
#define FM3_INT_IRQ26MON            (0x7C) 
#define FM3_INT_IRQ27MON            (0x80) 
#define FM3_INT_IRQ28MON            (0x84) 
#define FM3_INT_IRQ29MON            (0x88) 
#define FM3_INT_IRQ30MON            (0x8C) 
#define FM3_INT_IRQ31MON            (0x90) 

static Fm3IntState *fm3_int_state;

static void fm3_int_set_irq(void *opaque, int irq, int level)
{
    Fm3IntState *s = (Fm3IntState *)opaque;
    DPRINTF("%s : IRQ#%02d = %d\n", __func__, irq, level);
    qemu_set_irq(s->parent[irq], level);
}

static uint64_t fm3_int_read(void *opaque, target_phys_addr_t offset,
                             unsigned size)
{
    //Fm3IntState *s = (Fm3IntState *)opaque;
    uint64_t retval = 0;
    int i;

    switch (offset & 0xff) {

    /* extint ch0-7 */
    case FM3_INT_IRQ04MON:
        for (i = 0; i < 8; i++) {
            retval |= fm3_exti_get_irq_stat(i) << i;
        }
        break;

    /* extint ch8-31 */
    case FM3_INT_IRQ05MON: 
        for (i = 8; i < 32; i++) {
            retval |= fm3_exti_get_irq_stat(i) << (i - 8);
        }
        break;

    /* MFS rx int. */
    case FM3_INT_IRQ07MON:
    case FM3_INT_IRQ09MON:
    case FM3_INT_IRQ11MON:
    case FM3_INT_IRQ13MON:
    case FM3_INT_IRQ15MON:
    case FM3_INT_IRQ17MON:
    case FM3_INT_IRQ19MON:
    case FM3_INT_IRQ21MON:
        i = ((offset - FM3_INT_IRQ07MON) >> 3);
        retval = fm3_uart_get_rx_irq_stat(i);
        break;

    /* MFS tx/status int. */
    case FM3_INT_IRQ08MON:
    case FM3_INT_IRQ10MON:
    case FM3_INT_IRQ12MON:
    case FM3_INT_IRQ14MON:
    case FM3_INT_IRQ16MON:
    case FM3_INT_IRQ18MON:
    case FM3_INT_IRQ20MON:
    case FM3_INT_IRQ22MON:
        i = ((offset - FM3_INT_IRQ08MON) >> 3);
        retval = fm3_uart_get_tx_irq_stat(i) | 
                (fm3_uart_get_stat_irq_stat(i) << 1);
        break;
    }

    DPRINTF("%s : 0x%08x ---> 0x%08x\n", __func__, offset, retval);
    return retval;
}

static void fm3_int_write(void *opaque, target_phys_addr_t offset,
                          uint64_t value, unsigned size)
{
    DPRINTF("%s: Interrupt registers are read-only \n", __func__);
}

static const MemoryRegionOps fm3_int_mem_ops = {
    .read = fm3_int_read,
    .write = fm3_int_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int fm3_int_init(SysBusDevice *dev)
{
    Fm3IntState *s = FROM_SYSBUS(Fm3IntState, dev);
    int i;

    qdev_init_gpio_in(&dev->qdev, fm3_int_set_irq, FM3_IRQ_NUM);
    for (i = 0; i < FM3_IRQ_NUM; i++) {
        sysbus_init_irq(dev, &s->parent[i]);
    }

    memory_region_init_io(&s->mmio, &fm3_int_mem_ops, s, 
                          "fm3.int", 0x1000);

    sysbus_init_mmio_region(dev, &s->mmio);
    fm3_int_state = s;
    return 0;
}

static SysBusDeviceInfo fm3_int_info = {
    .init = fm3_int_init,
    .qdev.name  = "fm3.int",
    .qdev.size  = sizeof(Fm3IntState),
    .qdev.props = (Property[]) {
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void fm3_register_devices(void)
{
    sysbus_register_withprop(&fm3_int_info);
}

device_init(fm3_register_devices)
