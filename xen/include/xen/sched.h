/*
 * uXen changes:
 *
 * Copyright 2011-2019, Bromium, Inc.
 * Author: Christian Limpach <Christian.Limpach@gmail.com>
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

#ifndef __SCHED_H__
#define __SCHED_H__

#include <xen/config.h>
#include <xen/types.h>
#include <xen/spinlock.h>
#include <xen/shared.h>
#include <xen/timer.h>
#include <xen/rangeset.h>
#include <xen/domain.h>
#include <xen/rcupdate.h>
#include <xen/cpumask.h>
#include <xen/nodemask.h>
#include <xen/radix-tree.h>
#include <xen/multicall.h>
#include <xen/v4v.h>
#include <public/xen.h>
#include <public/domctl.h>
#include <public/sysctl.h>
#include <public/vcpu.h>

#ifdef CONFIG_COMPAT
#include <compat/vcpu.h>
DEFINE_XEN_GUEST_HANDLE(vcpu_runstate_info_compat_t);
#endif

/* A global pointer to the initial domain (DOM0). */
extern struct domain *dom0;

#ifndef CONFIG_COMPAT
#define BITS_PER_EVTCHN_WORD(d) BITS_PER_LONG
#else
#define BITS_PER_EVTCHN_WORD(d) (has_32bit_shinfo(d) ? 32 : BITS_PER_LONG)
#endif
#define MAX_EVTCHNS(d) (BITS_PER_EVTCHN_WORD(d) * BITS_PER_EVTCHN_WORD(d))
#define EVTCHNS_PER_BUCKET 256
#define NR_EVTCHN_BUCKETS  (NR_EVENT_CHANNELS / EVTCHNS_PER_BUCKET)

struct evtchn
{
#define ECS_FREE         0 /* Channel is available for use.                  */
#define ECS_RESERVED     1 /* Channel is reserved.                           */
#define ECS_UNBOUND      2 /* Channel is waiting to bind to a remote domain. */
#define ECS_HOST         7 /* Channel is bound to the host.                  */
    u8  state;             /* ECS_* */
    u8  consumer_is_xen;   /* Consumed by Xen or by guest? */
    u16 notify_vcpu_id;    /* VCPU for local delivery notification */
    union {
        struct {
            domid_t remote_domid;
        } unbound;     /* state == ECS_UNBOUND */
        struct {
            void *host_opaque;
        } host;     /* state == ECS_HOST */
    } u;
#ifdef FLASK_ENABLE
    void *ssid;
#endif
};

int  evtchn_init(struct domain *d); /* from domain_create */
void evtchn_destroy(struct domain *d); /* from domain_kill */
void evtchn_destroy_final(struct domain *d); /* from complete_domain_destroy */

struct waitqueue_vcpu;

struct vcpu 
{
    int              vcpu_id;

    int              processor;

    vcpu_info_t     *vcpu_info;

    struct domain   *domain;

    struct vcpu     *next_in_list;

    uint64_t         vcpu_throttle_last_time;
    int64_t          vcpu_throttle_credit;
    struct timer     vcpu_throttle_timer;

    union {
        void            *sched_priv;    /* scheduler-specific data */
        struct vm_vcpu_info_shared *vm_vcpu_info_shared;
    };

    struct vcpu_runstate_info runstate;
#ifndef CONFIG_COMPAT
# define runstate_guest(v) ((v)->runstate_guest)
    XEN_GUEST_HANDLE(vcpu_runstate_info_t) runstate_guest; /* guest address */
#else
# define runstate_guest(v) ((v)->runstate_guest.native)
    union {
        XEN_GUEST_HANDLE(vcpu_runstate_info_t) native;
        XEN_GUEST_HANDLE(vcpu_runstate_info_compat_t) compat;
    } runstate_guest; /* guest address */
#endif

    /* last time when vCPU is scheduled out */
    uint64_t last_run_time;

    /* Softirqs for this vcpu */
    unsigned long    softirq_pending;
    atomic_t         event_check;

    /* Timers for this vcpu */
    struct timers    timers;
    s_time_t         timer_deadline;

    /* Has the FPU been initialised? */
    bool_t           fpu_initialised;
    /* Has the FPU been used since it was last saved? */
    bool_t           fpu_dirtied;
    /* Initialization completed for this VCPU? */
    bool_t           is_initialised;
    /* Currently running on a CPU? */
    bool_t           is_running;
    /* VCPU should wake fast (do not deep sleep the CPU). */
    bool_t           is_urgent;
    /*  */
    bool_t           context_loaded;

    bool_t           need_hvm_resume;

    bool_t           force_preempt;

    bool_t           always_access_ok;

    /* Is executing context privileged within the caller (aka
     * system/kernel) */
    bool_t           is_privileged;

    /* Is executing context privileged (aka dom0)? */
    bool_t           is_sys_privileged;

    bool_t           target_vmis_owner;

    uint8_t          has_preemption_masked;

    uint8_t          cr3_load_count;

#ifdef VCPU_TRAP_LAST
#define VCPU_TRAP_NONE    0
    struct {
        bool_t           pending;
        uint8_t          old_mask;
    }                async_exception_state[VCPU_TRAP_LAST];
#define async_exception_state(t) async_exception_state[(t)-1]
    uint8_t          async_exception_mask;
#endif

    /* Require shutdown to be deferred for some asynchronous operation? */
    bool_t           defer_shutdown;
    /* VCPU is paused following shutdown request (d->is_shutting_down)? */
    bool_t           paused_for_shutdown;

    /*
     * > 0: a single port is being polled;
     * = 0: nothing is being polled (vcpu should be clear in d->poll_mask);
     * < 0: multiple ports may be being polled.
     */
    int              poll_evtchn;

    /* (over-)protected by ->domain->event_lock */
    int              pirq_evtchn_head;

    unsigned long    pause_flags;
    atomic_t         pause_count;

    /* IRQ-safe virq_lock protects against delivering VIRQ to stale evtchn. */
    u16              virq_to_evtchn[NR_VIRQS];
    spinlock_t       virq_lock;

    /* Bitmask of CPUs on which this VCPU may run. */
    cpumask_var_t    cpu_affinity;
    /* Used to change affinity temporarily. */
    cpumask_var_t    cpu_affinity_tmp;

    /* Bitmask of CPUs which are holding onto this VCPU's state. */
    cpumask_var_t    vcpu_dirty_cpumask;

    struct arch_vcpu arch;

    struct vm_info_shared *target_vmis;

    void *user_access_opaque;

    int vmexit_reason_count[55];
};

/* Per-domain lock can be recursively acquired in fault handlers. */
#define domain_lock(d) spin_lock_recursive(&(d)->domain_lock)
#define domain_unlock(d) spin_unlock_recursive(&(d)->domain_lock)
#define domain_is_locked(d) spin_is_locked(&(d)->domain_lock)

struct domain
{
    domid_t          domain_id;

    shared_info_t   *shared_info;     /* shared data area */
    unsigned long    shared_info_gpfn;

    spinlock_t       domain_lock;

    spinlock_t       page_alloc_lock; /* protects all the following fields  */
    unsigned int     tot_pages;       /* number of pages currently possesed */
    unsigned int     max_pages;       /* maximum value for tot_pages        */
    atomic_t         hidden_pages;    /* number of hidden pages             */
    atomic_t         pod_pages;       /* # pages populated on demand */
    atomic_t         zero_shared_pages; /* # pages zero shared      */
    atomic_t         tmpl_shared_pages; /* # pages template shared  */
    unsigned int     xenheap_pages;   /* # pages allocated from Xen heap    */
    unsigned int     host_pages;      /* # host pages mapped    */
    unsigned int     vframes;         /* # vframes */

    union {
        struct {
            atomic_t compressed_pages; /* number of compressed pages */
            atomic_t compressed_pdata; /* number of pages w/ compressed data */
            atomic_t decompressed_shared; /* # of decompressed pages shared */
            atomic_t decompressed_permanent; /* # of perm decompressed pages */
        } template;
        struct {
            atomic_t l1_pod_pages;    /* number of l1 pages pop on demand */
        } clone;
    };
    unsigned int     max_vcpus;

    /* Scheduling. */
    union {
        void            *sched_priv;    /* scheduler-specific data */
        struct vm_info_shared *vm_info_shared;
    };
    struct cpupool  *cpupool;

    struct domain   *next_in_list;

    struct list_head rangesets;
    spinlock_t       rangesets_lock;

    /* Event channel information. */
    struct evtchn   *evtchn[NR_EVTCHN_BUCKETS];
    spinlock_t       event_lock;

    struct domain_extra_1 *extra_1;
    struct domain_extra_2 *extra_2;

    /* Is this an HVM guest? */
    bool_t           is_hvm;
    /* Is this attovm guest? */
    bool_t           is_attovm;
    /* Is this ax-based attovm guest? */
    bool_t           is_attovm_ax;
    /* Does this guest need iommu mappings? */
    bool_t           need_iommu;
    /* Which guest this guest has privileges on */
    struct domain   *target;
    /* Is this guest being debugged by dom0? */
    bool_t           debugger_attached;
    /* Is this guest dying (i.e., a zombie)? */
    enum { DOMDYING_alive, DOMDYING_dying, DOMDYING_dead } is_dying;
    /* Domain is paused by controller software? */
    bool_t           is_paused_by_controller;
    /* Domain is paused for suspend */
    bool_t           is_paused_for_suspend;
    /* Domain is a memory template for clones. */
    bool_t           is_template;

    bool_t           hvm_domain_initialised;

#ifdef __i386__
    bool_t           use_hidden_mem;
#endif

    /* Domain is a clone of. */
    struct domain   *clone_of;

    /* Are any VCPUs polling event channels (SCHEDOP_poll)? */
#if MAX_VIRT_CPUS <= BITS_PER_LONG
    DECLARE_BITMAP(poll_mask, MAX_VIRT_CPUS);
#else
    unsigned long   *poll_mask;
#endif

    /* Guest has shut down (inc. reason code)? */
    spinlock_t       shutdown_lock;
    bool_t           is_crashing;
    bool_t           is_shutting_down; /* in process of shutting down? */
    bool_t           is_shut_down;     /* fully shut down? */
    int              shutdown_code;
    bool_t           silent_fake_emulation;

    /* If this is not 0, send suspend notification here instead of
     * raising DOM_EXC */
    int              suspend_evtchn;

    atomic_t         pause_count;

    spinlock_t       time_pause_lock;
    int              time_pause_count;
    uint64_t         time_pause_begin;
    uint64_t         pause_time;

    uint64_t         pause_tsc;

    unsigned long    vm_assist;

    atomic_t         refcnt;

    struct vcpu    **vcpu;

    /* Bitmask of CPUs which are holding onto this domain's state. */
    cpumask_var_t    domain_dirty_cpumask;

    struct arch_domain arch;

    void *ssid; /* sHype security subject identifier */

    /* Control-plane tools handle for this domain. */
    union {
        xen_domain_handle_t _handle;
        atomic_domain_handle_t handle_atomic;
    };

    int32_t time_offset_seconds;

    struct rcu_head rcu;

    struct list_head vcpu_idle_tasklet_list;
    spinlock_t vcpu_idle_tasklet_lock;

    /*
     * Hypercall deadlock avoidance lock. Used if a hypercall might
     * cause a deadlock. Acquirers don't spin waiting; they preempt.
     */
    spinlock_t hypercall_deadlock_mutex;

    uint64_t introspection_features;

    long printk_ratelimit_toks;
    unsigned long printk_ratelimit_last_msg;
    int printk_ratelimit_missed;

    struct debug_port_state *debug_port;

    rwlock_t v4v_lock;
    struct v4v_domain *v4v;
    /* v4v access control token */
    union {
        xen_domain_handle_t _v4v_token;
        atomic_domain_handle_t v4v_token_atomic;
    };

    u64 start_time;

#ifndef NDEBUG
    spinlock_t p2m_stat_lock;
    u64 p2m_stat_last;
    int p2m_stat_ops;
#endif  /* NDEBUG */
};

struct domain_setup_info
{
    /* Initialised by caller. */
    unsigned long image_addr;
    unsigned long image_len;
    /* Initialised by loader: Public. */
    unsigned long v_start;
    unsigned long v_end;
    unsigned long v_kernstart;
    unsigned long v_kernend;
    unsigned long v_kernentry;
#define PAEKERN_no           0
#define PAEKERN_yes          1
#define PAEKERN_extended_cr3 2
#define PAEKERN_bimodal      3
    unsigned int  pae_kernel;
    /* Initialised by loader: Private. */
    unsigned long elf_paddr_offset;
    unsigned int  load_symtab;
    unsigned long symtab_addr;
    unsigned long symtab_len;
};

/* Protect updates/reads (resp.) of domain_list and domain_hash. */
extern spinlock_t domlist_update_lock;
extern rcu_read_lock_t domlist_read_lock;

extern struct domain **domain_array;

extern struct vcpu *idle_vcpu[NR_CPUS];
#define is_idle_domain(d) ((d)->domain_id == DOMID_IDLE)
#define is_idle_vcpu(v)   (is_idle_domain((v)->domain))

#define DOMAIN_DESTROYED (1<<31) /* assumes atomic_t is >= 32 bits */
#define put_domain(_d) \
  if ( atomic_dec_and_test(&(_d)->refcnt) ) domain_destroy(_d)

/*
 * Use this when you don't have an existing reference to @d. It returns
 * FALSE if @d is being destroyed.
 */
static always_inline int get_domain(struct domain *d)
{
    atomic_t old, new, seen = d->refcnt;
    do
    {
        old = seen;
        if ( unlikely(_atomic_read(old) & DOMAIN_DESTROYED) )
            return 0;
        _atomic_set(new, _atomic_read(old) + 1);
        seen = atomic_compareandswap(old, new, &d->refcnt);
    }
    while ( unlikely(_atomic_read(seen) != _atomic_read(old)) );
    return 1;
}

/*
 * Use this when you already have, or are borrowing, a reference to @d.
 * In this case we know that @d cannot be destroyed under our feet.
 */
static inline void get_knownalive_domain(struct domain *d)
{
    atomic_inc(&d->refcnt);
    ASSERT(!(atomic_read(&d->refcnt) & DOMAIN_DESTROYED));
}

void domain_update_node_affinity(struct domain *d);

int
domain_create(domid_t dom, unsigned int flags, uint32_t ssidref,
              xen_domain_handle_t uuid, xen_domain_handle_t v4v_token,
              struct vm_info_shared *vmi, struct domain **_d);
struct domain *domain_create_internal(
    domid_t domid, unsigned int domcr_flags, uint32_t ssidref,
    struct vm_info_shared *vmi);
 /* DOMCRF_hvm: Create an HVM domain, as opposed to a PV domain. */
#define _DOMCRF_hvm           0
#define DOMCRF_hvm            (1U<<_DOMCRF_hvm)
 /* DOMCRF_hap: Create a domain with hardware-assisted paging. */
#define _DOMCRF_hap           1
#define DOMCRF_hap            (1U<<_DOMCRF_hap)
 /* DOMCRF_s3_integrity: Create a domain with tboot memory integrity protection
                        by tboot */
#define _DOMCRF_s3_integrity  2
#define DOMCRF_s3_integrity   (1U<<_DOMCRF_s3_integrity)
 /* DOMCRF_dummy: Create a dummy domain (not scheduled; not on domain list) */
#define _DOMCRF_dummy         3
#define DOMCRF_dummy          (1U<<_DOMCRF_dummy)
 /* DOMCRF_oos_off: dont use out-of-sync optimization for shadow page tables */
#define _DOMCRF_oos_off         4
#define DOMCRF_oos_off          (1U<<_DOMCRF_oos_off)
 /* DOMCRF_template: Create a memory template domain */
#define _DOMCRF_template      5
#define DOMCRF_template       (1U<<_DOMCRF_template)
#ifdef __i386__
 /* DOMCRF_hidden_mem: Use hidden memory for domain pages */
#define _DOMCRF_hidden_mem    6
#define DOMCRF_hidden_mem     (1U<<_DOMCRF_hidden_mem)
#endif
/* DOMCRF_attovm_ax: Is this attoxen attovm guest? */
#define _DOMCRF_attovm_ax     7
#define DOMCRF_attovm_ax      (1U<<_DOMCRF_attovm_ax)

long domain_set_max_vcpus(struct domain *d, unsigned int max);

/*
 * rcu_lock_domain_by_id() is more efficient than get_domain_by_id().
 * This is the preferred function if the returned domain reference
 * is short lived,  but it cannot be used if the domain reference needs 
 * to be kept beyond the current scope (e.g., across a softirq).
 * The returned domain reference must be discarded using rcu_unlock_domain().
 */
struct domain *rcu_lock_domain_by_id(domid_t dom);

/*
 * As above function, but accounts for current domain context:
 *  - Translates target DOMID_SELF into caller's domain id; and
 *  - Checks that caller has permission to act on the target domain.
 */
int rcu_lock_target_domain_by_id(domid_t dom, struct domain **d);

/*
 * As rcu_lock_target_domain_by_id(), but will fail EPERM rather than resolve
 * to local domain. Successful return always resolves to a remote domain that
 * the local domain is privileged to control.
 */
int rcu_lock_remote_target_domain_by_id(domid_t dom, struct domain **d);

enum uuid_type {
    UUID_HANDLE,
    UUID_V4V_TOKEN
};

struct domain *rcu_lock_domain_by_uuid(xen_domain_handle_t uuid,
                                       enum uuid_type);
int rcu_lock_target_domain_by_uuid(xen_domain_handle_t uuid, struct domain **d);

/* Finish a RCU critical region started by rcu_lock_domain_by_id(). */
static inline void rcu_unlock_domain(struct domain *d)
{
    if ( d != current->domain )
        rcu_read_unlock(&domlist_read_lock);
}

static inline struct domain *rcu_lock_domain(struct domain *d)
{
    if ( d != current->domain )
        rcu_read_lock(d);
    return d;
}

static inline struct domain *rcu_lock_current_domain(void)
{
    return /*rcu_lock_domain*/(current->domain);
}

struct domain *get_domain_by_id(domid_t dom);
void domain_destroy(struct domain *d);
int domain_kill(struct domain *d);
void domain_shutdown(struct domain *d, u8 reason);
void domain_resume(struct domain *d);
void domain_pause_for_debugger(void);

int vcpu_start_shutdown_deferral(struct vcpu *v);
void vcpu_end_shutdown_deferral(struct vcpu *v);

/*
 * Mark specified domain as crashed. This function always returns, even if the
 * caller is the specified domain. The domain is not synchronously descheduled
 * from any processor.
 */
void __domain_crash(struct domain *d);
#define domain_crash(d) do {                                              \
    printk("domain_crash called from %s:%d\n", __FILE__, __LINE__);       \
    __domain_crash(d);                                                    \
} while (0)

/*
 * Mark current domain as crashed and synchronously deschedule from the local
 * processor. This function never returns.
 */
void __domain_crash_synchronous(void) __attribute__((noreturn));
#define domain_crash_synchronous() do {                                   \
    printk("domain_crash_sync called from %s:%d\n", __FILE__, __LINE__);  \
    __domain_crash_synchronous();                                         \
} while (0)

#define set_current_state(_s) do { current->state = (_s); } while (0)
void scheduler_init(void);
int  sched_init_vcpu(struct vcpu *v, unsigned int processor);
void sched_destroy_vcpu(struct vcpu *v);
int  sched_init_domain(struct domain *d);
void sched_destroy_domain(struct domain *d);
int sched_move_domain(struct domain *d, struct cpupool *c);
long sched_adjust(struct domain *, struct xen_domctl_scheduler_op *);
long sched_adjust_global(struct xen_sysctl_scheduler_op *);
int  sched_id(void);
void sched_tick_suspend(void);
void sched_tick_resume(void);
void vcpu_wake(struct vcpu *d);
void vcpu_sleep_nosync(struct vcpu *d);
void vcpu_sleep_sync(struct vcpu *d);

/* NOTE: schedule_vcpu return with scheduler lock held in failure case */
int schedule_vcpu(struct vcpu *next);

/*
 * Force synchronisation of given VCPU's state. If it is currently descheduled,
 * this call will ensure that all its state is committed to memory and that
 * no CPU is using critical state (e.g., page tables) belonging to the VCPU.
 */
void sync_vcpu_execstate(struct vcpu *v);

/* As above, for any lazy state being held on the local CPU. */
void sync_local_execstate(void);

void vcpu_switch_host_cpu(struct vcpu *v);

/*
 * Called by the scheduler to switch to another VCPU. This function must
 * call context_saved(@prev) when the local CPU is no longer running in
 * @prev's context, and that context is saved to memory. Alternatively, if
 * implementing lazy context switching, it suffices to ensure that invoking
 * sync_vcpu_execstate() will switch and commit @prev's state.
 */
void context_switch(
    struct vcpu *prev, 
    struct vcpu *next);

/*
 * As described above, context_switch() must call this function when the
 * local CPU is no longer running in @prev's context, and @prev's context is
 * saved to memory. Alternatively, if implementing lazy context switching,
 * ensure that invoking sync_vcpu_execstate() will switch and commit @prev.
 */
void context_saved(struct vcpu *prev);

/* Called by the scheduler to continue running the current VCPU. */
void continue_running(
    struct vcpu *same);

void startup_cpu_idle_loop(void);
extern void (*pm_idle) (void);
extern void (*dead_idle) (void);


/*
 * Creates a continuation to resume the current hypercall. The caller should
 * return immediately, propagating the value returned from this invocation.
 * The format string specifies the types and number of hypercall arguments.
 * It contains one character per argument as follows:
 *  'i' [unsigned] {char, int}
 *  'l' [unsigned] long
 *  'h' guest handle (XEN_GUEST_HANDLE(foo))
 */
unsigned long hypercall_create_continuation(
    unsigned int op, const char *format, ...);
unsigned long hypercall_create_retry_continuation(void);
void hypercall_cancel_continuation(void);

#ifdef __UXEN_todo__
#define hypercall_preempt_check() (unlikely(    \
        softirq_pending(smp_processor_id()) |   \
        local_events_need_delivery()            \
    ))
#else  /* __UXEN_todo__ */
#define hypercall_preempt_check() 0
#endif  /* __UXEN_todo__ */

#define check_free_pages_needed(n)                                  \
    (_uxen_info.ui_free_pages[smp_processor_id()].count < 20 + (n))
#define check_pagemap_needed()                 \
    (_uxen_info.ui_pagemap_needs_check)
#define check_vframes_needed()                                          \
    (_uxen_info.ui_vframes.count < 20 && !_uxen_info.ui_out_of_vframes)

#define hypercall_needs_checks()                                \
    (check_free_pages_needed(0) || check_pagemap_needed() ||    \
     check_vframes_needed())

extern struct domain *domain_list;

/* Caller must hold the domlist_read_lock or domlist_update_lock. */
static inline struct domain *first_domain_in_cpupool( struct cpupool *c)
{
    struct domain *d;
    for (d = rcu_dereference(domain_list); d && d->cpupool != c;
         d = rcu_dereference(d->next_in_list));
    return d;
}
static inline struct domain *next_domain_in_cpupool(
    struct domain *d, struct cpupool *c)
{
    for (d = rcu_dereference(d->next_in_list); d && d->cpupool != c;
         d = rcu_dereference(d->next_in_list));
    return d;
}

#define for_each_domain(_d)                     \
 for ( (_d) = rcu_dereference(domain_list);     \
       (_d) != NULL;                            \
       (_d) = rcu_dereference((_d)->next_in_list )) \

#define for_each_domain_in_cpupool(_d,_c)       \
 for ( (_d) = first_domain_in_cpupool(_c);      \
       (_d) != NULL;                            \
       (_d) = next_domain_in_cpupool((_d), (_c)))

#define for_each_vcpu(_d,_v)                    \
 for ( (_v) = (_d)->vcpu ? (_d)->vcpu[0] : NULL; \
       (_v) != NULL;                            \
       (_v) = (_v)->next_in_list )

/*
 * Per-VCPU pause flags.
 */
 /* Domain is blocked waiting for an event. */
#define _VPF_blocked         0
#define VPF_blocked          (1UL<<_VPF_blocked)
 /* VCPU is offline. */
#define _VPF_down            1
#define VPF_down             (1UL<<_VPF_down)
 /* VCPU is blocked awaiting an event to be consumed by Xen. */
#define _VPF_blocked_in_xen  2
#define VPF_blocked_in_xen   (1UL<<_VPF_blocked_in_xen)
 /* VCPU affinity has changed: migrating to a new CPU. */
#define _VPF_migrating       3
#define VPF_migrating        (1UL<<_VPF_migrating)
 /* VCPU yield. */
#define _VPF_yield           5
#define VPF_yield            (1UL<<_VPF_yield)

static inline int vcpu_runnable(struct vcpu *v)
{
    return !(v->pause_flags |
             atomic_read(&v->pause_count) |
             atomic_read(&v->domain->pause_count));
}

static inline void vcpu_raise_softirq(struct vcpu *vcpu, unsigned int nr)
{
    /* This will usually already run on the right cpu and in a
     * context where softirq's will be checked.
     * Possible contexts are:
     * vcpu halted: smp_send_event_check_vcpu executes wake
     * vcpu in emulation: softirq will be checked
     * vcpu in vmcs: in most cases, this will be running on the same
     *               processor and will have interrupted the thread
     *               and the return to vmcs will check for softirqs.
     *               If not, then smp_send_event_check_vcpu will IPI
     *               processor where the vcpu is running. 
     */
    if ( !test_and_set_bit(nr, &vcpu->softirq_pending) )
        smp_send_event_check_vcpu(vcpu);
}

void vcpu_unblock(struct vcpu *v);
void vcpu_pause(struct vcpu *v);
void vcpu_pause_nosync(struct vcpu *v);
void domain_pause(struct domain *d);
void vcpu_unpause(struct vcpu *v);
void domain_unpause(struct domain *d);
void domain_pause_by_systemcontroller(struct domain *d);
void domain_unpause_by_systemcontroller(struct domain *d);
void domain_pause_for_suspend(struct domain *d);
void domain_unpause_for_suspend(struct domain *d);
void domain_pause_time(struct domain *d);
void domain_unpause_time(struct domain *d);
void cpu_init(void);

struct scheduler;

struct scheduler *scheduler_get_default(void);
struct scheduler *scheduler_alloc(unsigned int sched_id, int *perr);
void scheduler_free(struct scheduler *sched);
int schedule_cpu_switch(unsigned int cpu, struct cpupool *c);
void vcpu_force_reschedule(struct vcpu *v);
int cpu_disable_scheduler(unsigned int cpu);
int vcpu_set_affinity(struct vcpu *v, const cpumask_t *affinity);

void vcpu_runstate_get(struct vcpu *v, struct vcpu_runstate_info *runstate);
uint64_t get_cpu_idle_time(unsigned int cpu);

/*
 * Used by idle loop to decide whether there is work to do:
 *  (1) Run softirqs; or (2) Play dead; or (3) Run tasklets.
 */
#define cpu_is_haltable(cpu)                    \
    (!softirq_pending(cpu) &&                   \
     cpu_online(cpu) &&                         \
     !per_cpu(tasklet_work_to_do, cpu))

void watchdog_domain_init(struct domain *d);
void watchdog_domain_destroy(struct domain *d);

#define IS_PRIV(_d) (({                                                 \
                if (current->domain != (_d))                            \
                    gdprintk(XENLOG_ERR, "%s: IS_PRIV not with current\n", \
                             __FUNCTION__);                             \
                if (!current->is_privileged)                            \
                    gdprintk(XENLOG_ERR,                                \
                             "%s: IS_PRIV vm%u target_vmis vm%d\n",     \
                             __FUNCTION__, (_d)->domain_id,             \
                             current->target_vmis ?                     \
                             current->target_vmis->vmi_domid : -1);     \
                current->is_privileged;                                 \
            }))
#define IS_PRIV_FOR(_d, _t) (({                                         \
                if (current->domain != (_d))                            \
                    gdprintk(XENLOG_ERR, "%s: IS_PRIV_FOR not with current\n", \
                             __FUNCTION__);                             \
                (IS_HOST(_d) ?                                          \
                 (current->is_privileged ||                             \
                  (current->domain == (_d) &&                           \
                   (current->target_vmis_owner && current->target_vmis && \
                    current->target_vmis->vmi_domid == (_t)->domain_id))) : \
                 (current->domain == (_d) &&                            \
                  (_d)->domain_id == (_t)->domain_id));                 \
            }))
#define IS_PRIV_SYS() (({                       \
                current->is_sys_privileged;     \
            }))
#define IS_HOST(_d) (({                         \
                !(_d)->domain_id;               \
            }))

#define VM_ASSIST(_d,_t) (test_bit((_t), &(_d)->vm_assist))

#define is_hvm_domain(d) ((d)->is_hvm)
#define is_hvm_vcpu(v)   (is_hvm_domain(v->domain))
#define is_pinned_vcpu(v) ((v)->domain->is_pinned || \
                           cpumask_weight((v)->cpu_affinity) == 1)
#define need_iommu(d)    ((d)->need_iommu)

#define is_template_domain(d) ((d)->is_template)

void set_vcpu_migration_delay(unsigned int delay);
unsigned int get_vcpu_migration_delay(void);

extern bool_t sched_smt_power_savings;

extern enum cpufreq_controller {
    FREQCTL_none, FREQCTL_dom0_kernel, FREQCTL_xen
} cpufreq_controller;

#define CPUPOOLID_NONE    -1

struct cpupool *cpupool_get_by_id(int poolid);
void cpupool_put(struct cpupool *pool);
int cpupool_add_domain(struct domain *d, int poolid);
void cpupool_rm_domain(struct domain *d);
int cpupool_do_sysctl(struct xen_sysctl_cpupool_op *op);
void schedule_dump(struct cpupool *c);
extern void dump_runq(unsigned char key);

#define num_cpupool_cpus(c) cpumask_weight((c)->cpu_valid)

int hostsched_setup_vm(struct domain *, struct vm_info_shared *);
struct vm_vcpu_info_shared *hostsched_setup_vcpu(struct vcpu *,
                                                 struct vm_vcpu_info_shared *);
void hostsched_notify_exception(struct domain *);
void hostsched_signal_event(struct vcpu *, void *);
void hostsched_kick_vcpu(struct vcpu *);
void hostsched_set_handle(struct domain *, xen_domain_handle_t);

#endif /* __SCHED_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
