/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * uXen changes:
 *
 * Copyright 2011-2019, Bromium, Inc.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __XEN_PUBLIC_HVM_PARAMS_H__
#define __XEN_PUBLIC_HVM_PARAMS_H__

#include "hvm_op.h"

/*
 * Parameter space for HVMOP_{set,get}_param.
 */

/*
 * How should CPU0 event-channel notifications be delivered?
 * val[63:56] == 0: val[55:0] is a delivery GSI (Global System Interrupt).
 * val[63:56] == 1: val[55:0] is a delivery PCI INTx line, as follows:
 *                  Domain = val[47:32], Bus  = val[31:16],
 *                  DevFn  = val[15: 8], IntX = val[ 1: 0]
 * val[63:56] == 2: val[7:0] is a vector number, check for
 *                  XENFEAT_hvm_callback_vector to know if this delivery
 *                  method is available.
 * If val == 0 then CPU0 event-channel notifications are not delivered.
 */
#define HVM_PARAM_CALLBACK_IRQ 0

/*
 * These are not used by Xen. They are here for convenience of HVM-guest
 * xenbus implementations.
 */
#define HVM_PARAM_STORE_PFN    1
#define HVM_PARAM_STORE_EVTCHN 2

#define HVM_PARAM_PAE_ENABLED  4

/* Param for ioreq servers */
#define HVM_PARAM_IO_PFN_FIRST 5
#define HVM_PARAM_IO_PFN_LAST  6

#if !defined(__UXEN__) && !defined(__UXEN_TOOLS__)
#define NR_IO_PAGES_PER_SERVER 2
#else  /* __UXEN__ */
#define NR_IO_PAGES_PER_SERVER 1
#endif  /* __UXEN__ */

#ifdef __ia64__

#define HVM_PARAM_NVRAM_FD     7
#define HVM_PARAM_VHPT_SIZE    8
#define HVM_PARAM_BUFPIOREQ_PFN	9

#elif defined(__i386__) || defined(__x86_64__)

/* Expose Viridian interfaces to this HVM guest? */
#define HVM_PARAM_VIRIDIAN     9

#endif

/* Set mode for virtual timers (currently x86 only) */
#define HVM_PARAM_TIMER_MODE   10

/* continue running or stop timer when vcpu is descheduled ? */
#define HVMPTF_DESCHED_TIMER_MASK    0x00000001
#define HVMPTF_DESCHED_TIMER_cont    0x00000000
#define HVMPTF_DESCHED_TIMER_stop    0x00000001

/* continue or freeze guest time when vcpu is descheduled ? */
#define HVMPTF_DESCHED_GTIME_MASK    0x00000100
#define HVMPTF_DESCHED_GTIME_cont    0x00000000
#define HVMPTF_DESCHED_GTIME_stop    0x00000100

/* missed timer ticks: replay or collapse into single tick ? */
#define HVMPTF_DESCHED_MTICKS_MASK   0x00030000
#define HVMPTF_DESCHED_MTICKS_single 0x00000000
#define HVMPTF_DESCHED_MTICKS_replay 0x00010000

/* missed timer ticks after unpause: collapse into one or replay ? */
#define HVMPTF_UNPAUSE_MTICKS_MASK   0x01000000
#define HVMPTF_UNPAUSE_MTICKS_single 0x00000000
#define HVMPTF_UNPAUSE_MTICKS_replay 0x01000000

/* Boolean: Enable virtual HPET (high-precision event timer)? (x86-only) */
#define HVM_PARAM_HPET_ENABLED 11

/* Identity-map page directory used by Intel EPT when CR0.PG=0. */
#define HVM_PARAM_IDENT_PT     12

/* Device Model domain, defaults to 0. */
#define HVM_PARAM_DM_DOMAIN    13

/* ACPI S state: currently support S0 and S3 on x86. */
#define HVM_PARAM_ACPI_S_STATE 14

/* TSS used on Intel when CR0.PE=0. */
#define HVM_PARAM_VM86_TSS     15

/* Boolean: Enable aligning all periodic vpts to reduce interrupts */
#define HVM_PARAM_VPT_ALIGN    16

/* Console debug shared memory ring and event channel */
#define HVM_PARAM_CONSOLE_PFN    17
#define HVM_PARAM_CONSOLE_EVTCHN 18

/*
 * Select location of ACPI PM1a and TMR control blocks. Currently two locations
 * are supported, specified by version 0 or 1 in this parameter:
 *   - 0: default, use the old addresses
 *        PM1A_EVT == 0x1f40; PM1A_CNT == 0x1f44; PM_TMR == 0x1f48
 *   - 1: use the new default qemu addresses
 *        PM1A_EVT == 0xb000; PM1A_CNT == 0xb004; PM_TMR == 0xb008
 * You can find these address definitions in <hvm/ioreq.h>
 */
#define HVM_PARAM_ACPI_IOPORTS_LOCATION 19

/* Enable blocking memory events, async or sync (pause vcpu until response) 
 * onchangeonly indicates messages only on a change of value */
#define HVM_PARAM_MEMORY_EVENT_CR0          20
#define HVM_PARAM_MEMORY_EVENT_CR3          21
#define HVM_PARAM_MEMORY_EVENT_CR4          22
#define HVM_PARAM_MEMORY_EVENT_INT3         23
#define HVM_PARAM_MEMORY_EVENT_SINGLE_STEP  25

#define HVMPME_MODE_MASK       (3 << 0)
#define HVMPME_mode_disabled   0
#define HVMPME_mode_async      1
#define HVMPME_mode_sync       2
#define HVMPME_onchangeonly    (1 << 2)

/* Boolean: Enable nestedhvm (hvm only) */
#define HVM_PARAM_NESTEDHVM    24

#define HVM_PARAM_RESTRICTED_X86_EMUL 27

#define HVM_PARAM_SHARED_INFO_PFN 28

#define HVM_PARAM_RAND_SEED_LO 29

#define HVM_PARAM_RAND_SEED_HI 30

#define HVM_PARAM_VPT_COALESCE_NS 31

/* control how clone l1 pages are populated */
#define HVM_PARAM_CLONE_L1     32
/* lazy clone l1 pages */
#define HVM_PARAM_CLONE_L1_lazy          0x1
/* lazy populate cloned l1 pages */
#define HVM_PARAM_CLONE_L1_lazy_populate 0x2
/* dynamically populate l1 pages */
#define HVM_PARAM_CLONE_L1_dynamic       0x4

/* control decompressed pages from read accesses */
#define HVM_PARAM_CLONE_DECOMPRESSED 34
/* keep pages mapped read-only */
#define HVM_PARAM_CLONE_DECOMPRESSED_read_only 0x1
/* share decompressed pages */
#define HVM_PARAM_CLONE_DECOMPRESSED_shared    0x2

/* control garbage collection of compressed pages */
#define HVM_PARAM_COMPRESSED_GC 35
/* remove not-referenced decompressed pages */
#define HVM_PARAM_COMPRESSED_GC_decompressed 0x1

/* only compress a page on write, if the page is not shared */
#define HVM_PARAM_CLONE_PAGE_WRITE_COMPRESS_UNSHARED_ONLY 36

#define HVM_PARAM_LOG_RATELIMIT_GUEST_BURST 37

#define HVM_PARAM_LOG_RATELIMIT_GUEST_MS 38

#define HVM_PARAM_DMREQ_PFN    39
#define HVM_PARAM_DMREQ_VCPU_PFN 40

#define HVM_PARAM_ZERO_PAGE 41
#define HVM_PARAM_ZERO_PAGE_enable_setup 0x1
#define HVM_PARAM_ZERO_PAGE_enable_load 0x2

#define HVM_PARAM_TEMPLATE_LAZY_LOAD 42

#define HVM_PARAM_THROTTLE_PERIOD 43
#define HVM_PARAM_THROTTLE_RATE 44

#define HVM_PARAM_RESTRICTED_HYPERCALLS 45

#define HVM_NR_PARAMS          46

#endif /* __XEN_PUBLIC_HVM_PARAMS_H__ */
