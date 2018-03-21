/*
 * Copyright 2012-2018, Bromium, Inc.
 * Author: Christian Limpach <Christian.Limpach@gmail.com>
 * SPDX-License-Identifier: ISC
 */

#ifndef _DM_H_
#define _DM_H_

#include <stdint.h>
#include <stdio.h>

#define UXENDM_VERSION "devel"

#include "dict.h"
#include "hw.h"

extern char *dm_path;

extern const char *boot_order;
extern const char *monitor_device;
extern const char *lava_options;

extern uint32_t vm_id;
extern uint64_t vm_mem_mb;
extern uint64_t balloon_min_mb;
extern uint64_t balloon_max_mb;
extern const char *vm_name;
extern long vm_time_offset;
extern uint8_t vm_uuid[16];
extern uint8_t vm_template_uuid[16];
extern int vm_has_template_uuid;
extern char *vm_template_file;
extern int vm_restore_mode;
extern uint64_t vm_lazy_load;
extern int vm_status_sent;
extern window_handle vm_window;
extern window_handle vm_window_parent;
extern uint64_t vm_vcpus;
extern uint8_t vm_vcpu_avail[];
extern uint64_t vm_timer_mode;
extern uint64_t vm_tsc_mode;
extern uint64_t vm_vga_mb_mapped;
extern uint64_t vm_pae;
extern uint64_t vm_viridian;
extern uint64_t vm_virt_mode_change;
extern uint64_t vm_hpet;
extern uint64_t vm_zero_page;
extern uint64_t vm_zero_page_setup;
extern uint64_t vm_apic;
extern uint64_t vm_hidden_mem;
extern uint64_t vm_use_v4v_net;
extern uint64_t vm_use_v4v_disk;
extern uint64_t vm_v4v_storage;
extern uint64_t vm_v4v_disable_ahci_clones;
extern uint64_t vm_vram_dirty_tracking;
extern uint64_t use_simple_mapping;
extern uint64_t simple_map_size;
extern uint8_t v4v_idtoken[16];
extern uint8_t v4v_idtoken_is_vm_uuid;
extern const char *vmsavefile_on_crash;
extern const char *vmsavefile_on_restricted_pci;
extern const char *vmsavefile_on_kbd_reboot;
extern uint64_t guest_drivers_logmask;
extern uint64_t debugkey_level;
extern uint64_t malloc_limit_bytes;
extern uint64_t restore_framebuffer_pattern;
extern dict vm_hvm_params;
extern int *disabled_keys;
extern size_t disabled_keys_len;
extern uint64_t ps2_fallback;
extern const char *app_dump_command;
extern const char *clipboard_formats_blacklist_host2vm;
extern const char *clipboard_formats_whitelist_host2vm;
extern const char *clipboard_formats_blacklist_vm2host;
extern const char *clipboard_formats_whitelist_vm2host;
extern uint64_t deferred_clipboard;
extern uint64_t event_service_mouse_moves;
extern uint64_t hid_touch_enabled;
extern char *save_file_prefix;
extern uint64_t disp_fps_counter;
extern uint64_t disp_pv_vblank;
struct xc_interface_core;
extern struct xc_interface_core *xc_handle;
extern int xen_logdirty_enabled;
extern uint64_t cuckoo_debug_on;
extern uint64_t h264_offload;

extern FILE *logfile;
extern int loglevel;
extern uint64_t hide_log_sensitive_data;
extern uint64_t log_swap_fills;

extern uint64_t log_ratelimit_guest_burst;
extern uint64_t log_ratelimit_guest_ms;

extern const char *serial_devices[MAX_SERIAL_PORTS];

#endif	/* _DM_H_ */
