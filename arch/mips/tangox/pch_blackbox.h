#ifndef _PCH_BLACKBOX_H_
#define _PCH_BLACKBOX_H_

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#endif

#define PCH_BLACKBOX_MAGIC    0x50434842UL  /* "PCHB" */
#define PCH_BLACKBOX_VERSION  1
#define PCH_BLACKBOX_ADDR     0xABF00000UL
#define PCH_BLACKBOX_SIZE     65536UL
#define PCH_BLACKBOX_HDR_SIZE 128UL
#define PCH_BLACKBOX_LOG_SIZE (PCH_BLACKBOX_SIZE - PCH_BLACKBOX_HDR_SIZE)

/* MMIO hardware registers (uncached KSEG1) */
#define PCH_REG_XTAL_CNT      0xA0010048UL  /* 27MHz free-running timer */
#define PCH_REG_WDT_CNT       0xA001FD00UL  /* 27MHz watchdog down-counter */
#define PCH_REG_WDT_CFG       0xA001FD04UL  /* Watchdog config register */

/* Milestones */
#define MS_KJUMP_INIT         1   /* kjump_init(): module loaded, probe done */
#define MS_KJUMP_PAGES_READY  2   /* kjump pages allocated & staged */
#define MS_KJUMP_LOADED       3   /* payload verified & ready to jump */
#define MS_TRAMPOLINE_ENTER   4   /* trampoline entered, IRQs off */
#define MS_CACHE_FLUSHED      5   /* L1 cache flushed */
#define MS_COPY_DONE          6   /* staging copy complete */
#define MS_JUMP_TAKEN         7   /* WDT armed, jumping to target */
#define MS_DECOMPRESS_START   8   /* head.S / decompress.c entered */
#define MS_DECOMPRESS_DONE    9   /* decompress complete, jumping to kernel */
#define MS_PROM_INIT          10  /* Linux 4.9 prom_init() entered */
#define MS_REMAP_CONFIGURED   11  /* Linux 4.9 tangox_remap_init() done */
#define MS_SETUP_ARCH         12  /* Linux 4.9 setup_arch() entered */
#define MS_PID1_LAUNCHED      13  /* Linux 4.9 PID 1 launched */

/* Flags */
#define PCH_BB_FLAG_WDT_ARMED (1 << 0)
#define PCH_BB_FLAG_CRASH     (1 << 1)

#pragma pack(push, 1)
struct pch_blackbox {
    uint32_t magic;               /* 0x00: 0x50434842 ("PCHB") */
    uint32_t version;             /* 0x04: 1 */
    uint32_t current_milestone;   /* 0x08: 1 to 20 */
    uint32_t flags;               /* 0x0C: Subsystem state bitmask */
    uint32_t wdt_ticks_remaining; /* 0x10: Last sampled 27MHz WDT ticks */
    uint32_t target_entry;        /* 0x14: Jump destination address */
    uint32_t log_head;            /* 0x18: Ring buffer write pointer offset (0 .. LOG_SIZE-1) */
    uint32_t log_count;           /* 0x1C: Total characters written */

    /* Milestone Timestamps (sampled from SYS_xtal_in_cnt @ 0xA0010048) */
    uint32_t t_kjump_init;        /* 0x20: Milestone 1 */
    uint32_t t_kjump_pages_ready; /* 0x24: Milestone 2 */
    uint32_t t_kjump_loaded;      /* 0x28: Milestone 3 */
    uint32_t t_trampoline_enter;  /* 0x2C: Milestone 4 */
    uint32_t t_cache_flushed;     /* 0x30: Milestone 5 */
    uint32_t t_copy_done;         /* 0x34: Milestone 6 */
    uint32_t t_jump_taken;        /* 0x38: Milestone 7 */
    uint32_t t_decompress_start;  /* 0x3C: Milestone 8 */
    uint32_t t_decompress_done;   /* 0x40: Milestone 9 */
    uint32_t t_prom_init;         /* 0x44: Milestone 10 */
    uint32_t t_early_boot;        /* 0x48: Milestone 11 */

    /* CPU Register Snapshot */
    uint32_t reg_pc;              /* 0x4C */
    uint32_t reg_sp;              /* 0x50 */
    uint32_t reg_ra;              /* 0x54 */
    uint32_t reg_t9;              /* 0x58 */
    uint32_t reg_badva;           /* 0x5C */
    uint32_t reg_cause;           /* 0x60 */
    uint32_t reg_status;          /* 0x64 */

    uint32_t reserved[6];         /* 0x68 - 0x7F: 6 words = 24 bytes -> total 128 bytes */

    /* Ring buffer text log */
    char log[PCH_BLACKBOX_LOG_SIZE]; /* 0x80: 65,408 bytes */
};
#pragma pack(pop)

static inline struct pch_blackbox *pch_bb_get(void)
{
    return (struct pch_blackbox *)PCH_BLACKBOX_ADDR;
}

static inline uint32_t pch_bb_get_xtal(void)
{
    return *(volatile uint32_t *)PCH_REG_XTAL_CNT;
}

static inline uint32_t pch_bb_get_wdt(void)
{
    return *(volatile uint32_t *)PCH_REG_WDT_CNT;
}

static inline void pch_bb_init(uint32_t target_entry)
{
    struct pch_blackbox *bb = pch_bb_get();
    memset((void *)bb, 0, PCH_BLACKBOX_HDR_SIZE);
    bb->magic = PCH_BLACKBOX_MAGIC;
    bb->version = PCH_BLACKBOX_VERSION;
    bb->current_milestone = MS_KJUMP_INIT;
    bb->target_entry = target_entry;
    bb->t_kjump_init = pch_bb_get_xtal();
    bb->wdt_ticks_remaining = pch_bb_get_wdt();
    bb->log_head = 0;
    bb->log_count = 0;
}

static inline void pch_bb_set_milestone(uint32_t ms)
{
    struct pch_blackbox *bb = pch_bb_get();
    uint32_t now;
    if (bb->magic != PCH_BLACKBOX_MAGIC)
        return;
    bb->current_milestone = ms;
    bb->wdt_ticks_remaining = pch_bb_get_wdt();
    now = pch_bb_get_xtal();
    switch (ms) {
        case MS_KJUMP_INIT:         bb->t_kjump_init = now; break;
        case MS_KJUMP_PAGES_READY:  bb->t_kjump_pages_ready = now; break;
        case MS_KJUMP_LOADED:       bb->t_kjump_loaded = now; break;
        case MS_TRAMPOLINE_ENTER:   bb->t_trampoline_enter = now; break;
        case MS_CACHE_FLUSHED:      bb->t_cache_flushed = now; break;
        case MS_COPY_DONE:          bb->t_copy_done = now; break;
        case MS_JUMP_TAKEN:         bb->t_jump_taken = now; break;
        case MS_DECOMPRESS_START:   bb->t_decompress_start = now; break;
        case MS_DECOMPRESS_DONE:    bb->t_decompress_done = now; break;
        case MS_PROM_INIT:          bb->t_prom_init = now; break;
        case MS_REMAP_CONFIGURED:   bb->t_early_boot = now; break;
        default: break;
    }
}

static inline void pch_bb_log_char(char c)
{
    struct pch_blackbox *bb = pch_bb_get();
    uint32_t head;
    if (bb->magic != PCH_BLACKBOX_MAGIC)
        return;
    head = bb->log_head;
    if (head >= PCH_BLACKBOX_LOG_SIZE)
        head = 0;
    bb->log[head] = c;
    bb->log_head = (head + 1) % PCH_BLACKBOX_LOG_SIZE;
    bb->log_count++;
}

static inline void pch_bb_log_str(const char *s)
{
    if (!s) return;
    while (*s) {
        pch_bb_log_char(*s++);
    }
}

#endif /* _PCH_BLACKBOX_H_ */
