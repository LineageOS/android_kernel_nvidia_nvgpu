/*
 * GM20B GPC MMU
 *
 * Copyright (c) 2014-2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef NVGPU_GM20B_GR_GM20B_H
#define NVGPU_GM20B_GR_GM20B_H

#include <nvgpu/types.h>

struct gk20a;
struct nvgpu_warpstate;
enum nvgpu_event_id_type;

int gm20b_gr_tpc_disable_override(struct gk20a *g, u32 mask);
int gr_gm20b_commit_global_cb_manager(struct gk20a *g,
			struct nvgpu_gr_ctx *gr_ctx, bool patch);
void gr_gm20b_set_alpha_circular_buffer_size(struct gk20a *g, u32 data);
void gr_gm20b_set_circular_buffer_size(struct gk20a *g, u32 data);
void gr_gm20b_set_hww_esr_report_mask(struct gk20a *g);
void gr_gm20b_init_sm_dsm_reg_info(void);
void gr_gm20b_get_sm_dsm_perf_regs(struct gk20a *g,
					  u32 *num_sm_dsm_perf_regs,
					  u32 **sm_dsm_perf_regs,
					  u32 *perf_register_stride);
void gr_gm20b_get_sm_dsm_perf_ctrl_regs(struct gk20a *g,
					       u32 *num_sm_dsm_perf_ctrl_regs,
					       u32 **sm_dsm_perf_ctrl_regs,
					       u32 *ctrl_register_stride);
void gr_gm20b_set_gpc_tpc_mask(struct gk20a *g, u32 gpc_index);
bool gr_gm20b_is_tpc_addr(struct gk20a *g, u32 addr);
u32 gr_gm20b_get_tpc_num(struct gk20a *g, u32 addr);
int gr_gm20b_dump_gr_status_regs(struct gk20a *g,
			   struct gk20a_debug_output *o);
int gr_gm20b_update_pc_sampling(struct channel_gk20a *c,
				       bool enable);
void gr_gm20b_init_cyclestats(struct gk20a *g);
void gr_gm20b_bpt_reg_info(struct gk20a *g, struct nvgpu_warpstate *w_state);
int gm20b_gr_record_sm_error_state(struct gk20a *g, u32 gpc,
		u32 tpc, u32 sm, struct channel_gk20a *fault_ch);
int gm20b_gr_clear_sm_error_state(struct gk20a *g,
		struct channel_gk20a *ch, u32 sm_id);
void gm20b_gr_clear_sm_hww(struct gk20a *g, u32 gpc, u32 tpc, u32 sm,
			u32 global_esr);
u32 gr_gm20b_get_pmm_per_chiplet_offset(void);
void gm20b_gr_set_debug_mode(struct gk20a *g, bool enable);
bool gm20b_gr_esr_bpt_pending_events(u32 global_esr,
				     enum nvgpu_event_id_type bpt_event);
#endif /* NVGPU_GM20B_GR_GM20B_H */
