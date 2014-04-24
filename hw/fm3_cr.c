/*
 * Fujitsu FM3 Clock/Reset
 *
 * Copyright (c) 2012 Pylone, Inc.
 * Written by Masashi YOKOTA <yokota@pylone.jp>
 *
 * This code is licensed under the GNU GPL v2.
 */
#include "sysbus.h"
#include "devices.h"
#include "arm-misc.h"

#define FM3_CR_SCM_CTL_OFFSET       (0x0000)
#define FM3_CR_SCM_STR_OFFSET       (0x0004)
#define FM3_CR_STB_CTL_OFFSET       (0x0008)
#define FM3_CR_RST_STR_OFFSET       (0x000c)
#define FM3_CR_BSC_PSR_OFFSET       (0x0010)
#define FM3_CR_APBC0_PSR_OFFSET     (0x0014)
#define FM3_CR_APBC1_PSR_OFFSET     (0x0018)
#define FM3_CR_APBC2_PSR_OFFSET     (0x001c)
#define FM3_CR_SWC_PSR_OFFSET       (0x0020)
#define FM3_CR_TTC_PSR_OFFSET       (0x0028)
#define FM3_CR_CSW_TMR_OFFSET       (0x0030)
#define FM3_CR_PSW_TMR_OFFSET       (0x0034)
#define FM3_CR_PLL_CTL1_OFFSET      (0x0038)
#define FM3_CR_PLL_CTL2_OFFSET      (0x003c)
#define FM3_CR_CSV_CTL_OFFSET       (0x0040)
#define FM3_CR_CSV_STR_OFFSET       (0x0044)
#define FM3_CR_FCSWH_CTL_OFFSET     (0x0048)
#define FM3_CR_FCSWL_CTL_OFFSET     (0x004c)
#define FM3_CR_FCSWD_CTL_OFFSET     (0x0050)
#define FM3_CR_DBWDT_CTL_OFFSET     (0x0054)
#define FM3_CR_INT_ENR_OFFSET       (0x0060)
#define FM3_CR_INT_STR_OFFSET       (0x0064)
#define FM3_CR_INT_CLR_OFFSET       (0x0068)

#define FM3_CR_HI_OSC_HZ            (4000000)
#define FM3_CR_LOW_OSC_HZ           (100000)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion mmio;
    uint32_t scm;
    uint32_t bsc;
    uint32_t pll1;
    uint32_t pll2;
    uint32_t main_clk_hz;
    uint32_t sub_clk_hz;
    uint32_t master_clk_hz;
} Fm3CrState;

static uint32_t fm3_cr_get_pll(Fm3CrState *s)
{
    uint32_t k = ((s->pll1 >> 4) & 0xf) + 1;
    uint32_t n = (s->pll2 & 0x3f) + 1;
    return s->main_clk_hz / k * n;
}

static void fm3_cr_update_system_clock(Fm3CrState *s)
{
    int tmp = system_clock_scale;
    switch ((s->scm >> 5) & 3) {
    case 0:
        s->master_clk_hz = FM3_CR_HI_OSC_HZ;
        break;
    case 1:
        if (s->scm & (1 << 3))
            s->master_clk_hz = s->main_clk_hz;
        else
            s->master_clk_hz = 0;
        break;
    case 2:
        if (s->scm & (1 << 4))
            s->master_clk_hz = fm3_cr_get_pll(s);
        else
            s->master_clk_hz = 0;
        break;
    case 4:
        s->master_clk_hz = FM3_CR_LOW_OSC_HZ;
        break;
    case 5:
        if (s->scm & (1 << 3))
            s->master_clk_hz = s->main_clk_hz;
        else
            s->master_clk_hz = 0;
        break;
    default:
        printf("FM3_CR: Invalid selection for the master clock: SCM_CTL=0x%x\n", s->scm);
        return;

    }
    switch (s->bsc) {
    case 0:
        system_clock_scale = s->master_clk_hz;
        break;
    case 1:
        system_clock_scale = s->master_clk_hz >> 1;
        break;
    case 2:
        system_clock_scale = s->master_clk_hz / 3;
        break;
    case 3:
        system_clock_scale = s->master_clk_hz >> 2;
        break;
    case 4:
        system_clock_scale = s->master_clk_hz / 6;
        break;
    case 5:
        system_clock_scale = s->master_clk_hz >> 3;
        break;
    case 6:
        system_clock_scale = s->master_clk_hz >> 4;
        break;
    default:
        printf("FM3_CR: Invalid divisor setting for the base clock: BSC_PSR=0x%x\n", s->bsc);
        return;
    }

    if (tmp != system_clock_scale)
        printf("FM3_CR: Base clock at %d Hz\n", system_clock_scale);
}

static uint64_t fm3_cr_read(void *opaque, target_phys_addr_t offset,
                            unsigned size)
{
    Fm3CrState *s = (Fm3CrState *)opaque;
    uint64_t retval = 0;

    switch (offset) {
    case FM3_CR_SCM_STR_OFFSET:
    case FM3_CR_SCM_CTL_OFFSET:
        retval = s->scm;
        break;
    case FM3_CR_BSC_PSR_OFFSET:
        retval = s->bsc;
        break;
    case FM3_CR_PLL_CTL1_OFFSET:
        retval = s->pll1;
        break;
    case FM3_CR_PLL_CTL2_OFFSET:
        retval = s->pll2;
        break;
    default:
        break;
    }
    return retval;
}

static void fm3_cr_write(void *opaque, target_phys_addr_t offset,
                         uint64_t value, unsigned size)
{
    Fm3CrState *s = (Fm3CrState *)opaque;
    switch (offset) {
    case FM3_CR_SCM_CTL_OFFSET:
        s->scm = value & 0xff;
        break;
    case FM3_CR_BSC_PSR_OFFSET:
        s->bsc = value & 3;
        break;
    case FM3_CR_PLL_CTL1_OFFSET:
        s->pll1 = value;
        break;
    case FM3_CR_PLL_CTL2_OFFSET:
        s->pll2 = value & 0x3f;
        if (49 < s->pll2) {
            printf("FM3_CR: Invalid pll feedback divisor: PLLN=%d\n", s->pll2);
            return;
        }
        break;
    default:
        return;
    }
    fm3_cr_update_system_clock(s);
}

static const MemoryRegionOps fm3_cr_mem_ops = {
    .read = fm3_cr_read,
    .write = fm3_cr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void fm3_cr_reset(DeviceState *d)
{
    Fm3CrState *s = container_of(d, Fm3CrState, busdev.qdev);
    s->master_clk_hz = FM3_CR_HI_OSC_HZ;
}

static int fm3_cr_init(SysBusDevice *dev)
{
    Fm3CrState *s = FROM_SYSBUS(Fm3CrState, dev);

    memory_region_init_io(&s->mmio, &fm3_cr_mem_ops, s, 
                          "fm3.cr", 0x1000);
    sysbus_init_mmio_region(dev, &s->mmio);

    system_clock_scale = s->main_clk_hz;
    return 0;
}

static SysBusDeviceInfo fm3_cr_info = {
    .init = fm3_cr_init,
    .qdev.name  = "fm3.cr",
    .qdev.size  = sizeof(Fm3CrState),
    .qdev.reset  = fm3_cr_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("main_clk_hz", Fm3CrState, main_clk_hz, 4000000),
        DEFINE_PROP_UINT32("sub_clk_hz", Fm3CrState, sub_clk_hz, 32768),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void fm3_register_devices(void)
{
    sysbus_register_withprop(&fm3_cr_info);
}

device_init(fm3_register_devices)
