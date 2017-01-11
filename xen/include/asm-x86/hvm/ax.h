/*
 * Copyright 2017, Bromium, Inc.
 * SPDX-License-Identifier: ISC
 */

#ifndef __ASM_X86_HVM_AX_H__
#define __ASM_X86_HVM_AX_H__

#include <asm/msr-index.h>
#include <asm/hvm/hvm.h>
#include <asm/hvm/support.h>
#include <asm/hvm/vmx/vmx.h>
#include <asm/hvm/vmx/vmcs.h>

#include <attoxen-api/ax_constants.h>
#include <attoxen-api/ax_structures.h>


#define AX_VMCS_EXTENSIONS_V1(v) ((ax_vmcs_extensions_v1_t *) (((uint8_t *) v) + 0x1000 - sizeof(ax_vmcs_extensions_v1_t)))

extern int ax_present;
extern void ax_flush_ept (struct domain *d);
extern int ax_setup (void);

static inline
void ax_vmcs_x_flags_set (struct vcpu *v, uint64_t val)
{
  ax_vmcs_extensions_v1_t *x = AX_VMCS_EXTENSIONS_V1 (v->arch.hvm_vmx.vmcs);

  x->flags = val;
}

static inline
void ax_vmcs_x_wrmsrl (struct vcpu *v, uint32_t msr, uint64_t value)
{
  ax_vmcs_extensions_v1_t *x = AX_VMCS_EXTENSIONS_V1 (v->arch.hvm_vmx.vmcs);

  switch (msr) {
  case MSR_SHADOW_GS_BASE:
    x->msr_gs_shadow = value;
    return;

  case MSR_STAR:
    x->msr_star = value;
    return;

  case MSR_CSTAR:
    x->msr_cstar = value;
    return;

  case MSR_LSTAR:
    x->msr_lstar = value;
    return;

  case MSR_SYSCALL_MASK:
    x->msr_syscall_mask = value;
    return;
  }
}


static inline
void ax_vmcs_x_rdmsrl (struct vcpu *v, uint32_t msr, uint64_t *value)
{
  ax_vmcs_extensions_v1_t *x = AX_VMCS_EXTENSIONS_V1 (v->arch.hvm_vmx.vmcs);

  switch (msr) {
  case MSR_SHADOW_GS_BASE:
    *value = x->msr_gs_shadow;
    return;

  case MSR_STAR:
    *value = x->msr_star;
    return;

  case MSR_CSTAR:
    *value = x->msr_cstar;
    return;

  case MSR_LSTAR:
    *value = x->msr_lstar;
    return;

  case MSR_SYSCALL_MASK:
    *value = x->msr_syscall_mask;
    return;
  }
}

#endif /* __ASM_X86_HVM_AX_H__ */