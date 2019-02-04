/*
 * Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef NVGPU_PMU_H
#define NVGPU_PMU_H

#include <nvgpu/kmem.h>
#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/allocator.h>
#include <nvgpu/lock.h>
#include <nvgpu/cond.h>
#include <nvgpu/thread.h>
#include <nvgpu/nvgpu_common.h>
#include <nvgpu/flcnif_cmn.h>
#include <nvgpu/pmuif/nvgpu_gpmu_cmdif.h>
#include <nvgpu/pmuif/gpmu_super_surf_if.h>
#include <nvgpu/falcon.h>
#include <nvgpu/engine_mem_queue.h>

#define nvgpu_pmu_dbg(g, fmt, args...) \
	nvgpu_log(g, gpu_dbg_pmu, fmt, ##args)

/* defined by pmu hw spec */
#define GK20A_PMU_VA_SIZE		(512U * 1024U * 1024U)
#define GK20A_PMU_UCODE_SIZE_MAX	(256U * 1024U)
#define GK20A_PMU_SEQ_BUF_SIZE		4096U

#define GK20A_PMU_TRACE_BUFSIZE		0x4000U    /* 4K */
#define GK20A_PMU_DMEM_BLKSIZE2		8U

#define PMU_MODE_MISMATCH_STATUS_MAILBOX_R  6U
#define PMU_MODE_MISMATCH_STATUS_VAL        0xDEADDEADU

/* Falcon Register index */
#define PMU_FALCON_REG_R0		(0U)
#define PMU_FALCON_REG_R1		(1U)
#define PMU_FALCON_REG_R2		(2U)
#define PMU_FALCON_REG_R3		(3U)
#define PMU_FALCON_REG_R4		(4U)
#define PMU_FALCON_REG_R5		(5U)
#define PMU_FALCON_REG_R6		(6U)
#define PMU_FALCON_REG_R7		(7U)
#define PMU_FALCON_REG_R8		(8U)
#define PMU_FALCON_REG_R9		(9U)
#define PMU_FALCON_REG_R10		(10U)
#define PMU_FALCON_REG_R11		(11U)
#define PMU_FALCON_REG_R12		(12U)
#define PMU_FALCON_REG_R13		(13U)
#define PMU_FALCON_REG_R14		(14U)
#define PMU_FALCON_REG_R15		(15U)
#define PMU_FALCON_REG_IV0		(16U)
#define PMU_FALCON_REG_IV1		(17U)
#define PMU_FALCON_REG_UNDEFINED	(18U)
#define PMU_FALCON_REG_EV		(19U)
#define PMU_FALCON_REG_SP		(20U)
#define PMU_FALCON_REG_PC		(21U)
#define PMU_FALCON_REG_IMB		(22U)
#define PMU_FALCON_REG_DMB		(23U)
#define PMU_FALCON_REG_CSW		(24U)
#define PMU_FALCON_REG_CCR		(25U)
#define PMU_FALCON_REG_SEC		(26U)
#define PMU_FALCON_REG_CTX		(27U)
#define PMU_FALCON_REG_EXCI		(28U)
#define PMU_FALCON_REG_RSVD0		(29U)
#define PMU_FALCON_REG_RSVD1		(30U)
#define PMU_FALCON_REG_RSVD2		(31U)
#define PMU_FALCON_REG_SIZE		(32U)

/* Choices for pmu_state */
#define PMU_STATE_OFF			0U /* PMU is off */
#define PMU_STATE_STARTING		1U /* PMU is on, but not booted */
#define PMU_STATE_INIT_RECEIVED		2U /* PMU init message received */
#define PMU_STATE_ELPG_BOOTING		3U /* PMU is booting */
#define PMU_STATE_ELPG_BOOTED		4U /* ELPG is initialized */
#define PMU_STATE_LOADING_PG_BUF	5U /* Loading PG buf */
#define PMU_STATE_LOADING_ZBC		6U /* Loading ZBC buf */
#define PMU_STATE_STARTED		7U /* Fully unitialized */
#define PMU_STATE_EXIT			8U /* Exit PMU state machine */

#define GK20A_PMU_UCODE_NB_MAX_OVERLAY	    32U
#define GK20A_PMU_UCODE_NB_MAX_DATE_LENGTH  64U

#define PMU_MAX_NUM_SEQUENCES		(256U)
#define PMU_SEQ_BIT_SHIFT		(5U)
#define PMU_SEQ_TBL_SIZE	\
		(PMU_MAX_NUM_SEQUENCES >> PMU_SEQ_BIT_SHIFT)

#define PMU_INVALID_SEQ_DESC		(~0U)

#define	GK20A_PMU_DMAIDX_UCODE		U32(0)
#define	GK20A_PMU_DMAIDX_VIRT		U32(1)
#define	GK20A_PMU_DMAIDX_PHYS_VID	U32(2)
#define	GK20A_PMU_DMAIDX_PHYS_SYS_COH	U32(3)
#define	GK20A_PMU_DMAIDX_PHYS_SYS_NCOH	U32(4)
#define	GK20A_PMU_DMAIDX_RSVD		U32(5)
#define	GK20A_PMU_DMAIDX_PELPG		U32(6)
#define	GK20A_PMU_DMAIDX_END		U32(7)

enum pmu_seq_state {
	PMU_SEQ_STATE_FREE = 0,
	PMU_SEQ_STATE_PENDING,
	PMU_SEQ_STATE_USED,
	PMU_SEQ_STATE_CANCELLED
};

/*PG defines used by nvpgu-pmu*/
#define PMU_PG_IDLE_THRESHOLD_SIM		1000U
#define PMU_PG_POST_POWERUP_IDLE_THRESHOLD_SIM	4000000U
/* TBD: QT or else ? */
#define PMU_PG_IDLE_THRESHOLD			15000U
#define PMU_PG_POST_POWERUP_IDLE_THRESHOLD	1000000U

#define PMU_PG_LPWR_FEATURE_RPPG 0x0U
#define PMU_PG_LPWR_FEATURE_MSCG 0x1U

#define PMU_MSCG_DISABLED 0U
#define PMU_MSCG_ENABLED 1U

/* Default Sampling Period of AELPG */
#define APCTRL_SAMPLING_PERIOD_PG_DEFAULT_US                    (1000000U)

/* Default values of APCTRL parameters */
#define APCTRL_MINIMUM_IDLE_FILTER_DEFAULT_US                   (100U)
#define APCTRL_MINIMUM_TARGET_SAVING_DEFAULT_US                 (10000U)
#define APCTRL_POWER_BREAKEVEN_DEFAULT_US                       (2000U)
#define APCTRL_CYCLES_PER_SAMPLE_MAX_DEFAULT                    (200U)

/* pmu load const defines */
#define PMU_BUSY_CYCLES_NORM_MAX		(1000U)

/* RPC */
#define PMU_RPC_EXECUTE(_stat, _pmu, _unit, _func, _prpc, _size)\
	do {                                                 \
		(void) memset(&((_prpc)->hdr), 0, sizeof((_prpc)->hdr));\
		\
		(_prpc)->hdr.unit_id   = PMU_UNIT_##_unit;       \
		(_prpc)->hdr.function = NV_PMU_RPC_ID_##_unit##_##_func;\
		(_prpc)->hdr.flags    = 0x0;    \
		\
		_stat = nvgpu_pmu_rpc_execute(_pmu, &((_prpc)->hdr),    \
			(u16)(sizeof(*(_prpc)) - sizeof((_prpc)->scratch)), \
			(_size), NULL, NULL, false);	\
	} while (false)

/* RPC blocking call to copy back data from PMU to  _prpc */
#define PMU_RPC_EXECUTE_CPB(_stat, _pmu, _unit, _func, _prpc, _size)\
	do {                                                 \
		(void) memset(&((_prpc)->hdr), 0, sizeof((_prpc)->hdr));\
		\
		(_prpc)->hdr.unit_id   = PMU_UNIT_##_unit;       \
		(_prpc)->hdr.function = NV_PMU_RPC_ID_##_unit##_##_func;\
		(_prpc)->hdr.flags    = 0x0;    \
		\
		_stat = nvgpu_pmu_rpc_execute(_pmu, &((_prpc)->hdr),    \
			(u16)(sizeof(*(_prpc)) - sizeof((_prpc)->scratch)),\
			(_size), NULL, NULL, true);	\
	} while (false)

/* RPC non-blocking with call_back handler option */
#define PMU_RPC_EXECUTE_CB(_stat, _pmu, _unit, _func, _prpc, _size, _cb, _cbp)\
	do {                                                 \
		(void) memset(&((_prpc)->hdr), 0, sizeof((_prpc)->hdr));\
		\
		(_prpc)->hdr.unit_id   = PMU_UNIT_##_unit;       \
		(_prpc)->hdr.function = NV_PMU_RPC_ID_##_unit##_##_func;\
		(_prpc)->hdr.flags    = 0x0;    \
		\
		_stat = nvgpu_pmu_rpc_execute(_pmu, &((_prpc)->hdr),    \
			(sizeof(*(_prpc)) - sizeof((_prpc)->scratch)),\
			(_size), _cb, _cbp, false);	\
	} while (false)

typedef void (*pmu_callback)(struct gk20a *g, struct pmu_msg *msg, void *param,
		u32 handle, u32 status);

struct rpc_handler_payload {
	void *rpc_buff;
	bool is_mem_free_set;
	bool complete;
};

struct pmu_rpc_desc {
	void   *prpc;
	u16 size_rpc;
	u16 size_scratch;
};

struct pmu_payload {
	struct {
		void *buf;
		u32 offset;
		u32 size;
		u32 fb_size;
	} in, out;
	struct pmu_rpc_desc rpc;
};

struct pmu_ucode_desc {
	u32 descriptor_size;
	u32 image_size;
	u32 tools_version;
	u32 app_version;
	char date[GK20A_PMU_UCODE_NB_MAX_DATE_LENGTH];
	u32 bootloader_start_offset;
	u32 bootloader_size;
	u32 bootloader_imem_offset;
	u32 bootloader_entry_point;
	u32 app_start_offset;
	u32 app_size;
	u32 app_imem_offset;
	u32 app_imem_entry;
	u32 app_dmem_offset;
	/* Offset from appStartOffset */
	u32 app_resident_code_offset;
	/* Exact size of the resident code
	 * ( potentially contains CRC inside at the end )
	 */
	u32 app_resident_code_size;
	/* Offset from appStartOffset */
	u32 app_resident_data_offset;
	/* Exact size of the resident code
	 * ( potentially contains CRC inside at the end )
	 */
	u32 app_resident_data_size;
	u32 nb_overlays;
	struct {u32 start; u32 size; } load_ovl[GK20A_PMU_UCODE_NB_MAX_OVERLAY];
	u32 compressed;
};

struct pmu_ucode_desc_v1 {
	u32 descriptor_size;
	u32 image_size;
	u32 tools_version;
	u32 app_version;
	char date[GK20A_PMU_UCODE_NB_MAX_DATE_LENGTH];
	u32 bootloader_start_offset;
	u32 bootloader_size;
	u32 bootloader_imem_offset;
	u32 bootloader_entry_point;
	u32 app_start_offset;
	u32 app_size;
	u32 app_imem_offset;
	u32 app_imem_entry;
	u32 app_dmem_offset;
	u32 app_resident_code_offset;
	u32 app_resident_code_size;
	u32 app_resident_data_offset;
	u32 app_resident_data_size;
	u32 nb_imem_overlays;
	u32 nb_dmem_overlays;
	struct {u32 start; u32 size; } load_ovl[64];
	u32 compressed;
};

struct pmu_mutex {
	u32 id;
	u32 index;
	u32 ref_cnt;
};

struct pmu_sequence {
	u8 id;
	enum pmu_seq_state state;
	u32 desc;
	struct pmu_msg *msg;
	union {
		struct pmu_allocation_v1 in_v1;
		struct pmu_allocation_v2 in_v2;
		struct pmu_allocation_v3 in_v3;
	};
	struct nvgpu_mem *in_mem;
	union {
		struct pmu_allocation_v1 out_v1;
		struct pmu_allocation_v2 out_v2;
		struct pmu_allocation_v3 out_v3;
	};
	struct nvgpu_mem *out_mem;
	u8 *out_payload;
	pmu_callback callback;
	void *cb_params;

	/* fb queue that is associated with this seq */
	struct nvgpu_engine_fb_queue *cmd_queue;
	/* fbq element that is associated with this seq */
	u8 *fbq_work_buffer;
	u32 fbq_element_index;
	/* flags if queue element has an in payload */
	bool in_payload_fb_queue;
	/* flags if queue element has an out payload */
	bool out_payload_fb_queue;
	/* Heap location this cmd will use in the nvgpu managed heap */
	u16 fbq_heap_offset;
	/*
	 * Track the amount of the "work buffer" (queue_buffer) that
	 * has been used so far, as the outbound frame is assembled
	 * (first FB Queue hdr, then CMD, then payloads).
	 */
	 u32 buffer_size_used;
	/* offset to out data in the queue element */
	u16 fbq_out_offset_in_queue_element;
};

struct nvgpu_pg_init {
	bool state_change;
	struct nvgpu_cond wq;
	struct nvgpu_thread state_task;
};

struct nvgpu_pmu {
	struct gk20a *g;
	struct nvgpu_falcon *flcn;

	struct nvgpu_firmware *fw_desc;
	struct nvgpu_firmware *fw_image;
	struct nvgpu_firmware *fw_sig;

	struct nvgpu_mem ucode;

	struct nvgpu_mem pg_buf;

	/* TBD: remove this if ZBC seq is fixed */
	struct nvgpu_mem seq_buf;
	struct nvgpu_mem trace_buf;

	/* super surface members */
	struct nvgpu_mem super_surface_buf;
	struct nv_pmu_super_surface_member_descriptor
		ssmd_set[NV_PMU_SUPER_SURFACE_MEMBER_DESCRIPTOR_COUNT];
	struct nv_pmu_super_surface_member_descriptor
		ssmd_get_status[NV_PMU_SUPER_SURFACE_MEMBER_DESCRIPTOR_COUNT];

	bool buf_loaded;

	struct pmu_sha1_gid gid_info;

	struct nvgpu_engine_mem_queue *queue[PMU_QUEUE_COUNT];
	u32 queue_type;

	struct nvgpu_engine_fb_queue *fb_queue[PMU_QUEUE_COUNT];

	struct pmu_sequence *seq;
	unsigned long pmu_seq_tbl[PMU_SEQ_TBL_SIZE];
	u32 next_seq_desc;

	struct pmu_mutex *mutex;
	u32 mutex_cnt;

	struct nvgpu_mutex pmu_copy_lock;
	struct nvgpu_mutex pmu_seq_lock;

	struct nvgpu_allocator dmem;

	bool pmu_ready;

	u32 perfmon_query;

	bool zbc_save_done;

	u32 stat_dmem_offset[PMU_PG_ELPG_ENGINE_ID_INVALID_ENGINE];

	u32 elpg_stat;

	u32 mscg_stat;
	u32 mscg_transition_state;

	u32 pmu_state;

#define PMU_ELPG_ENABLE_ALLOW_DELAY_MSEC	1U /* msec */
	struct nvgpu_pg_init pg_init;
	struct nvgpu_mutex pg_mutex; /* protect pg-RPPG/MSCG enable/disable */
	struct nvgpu_mutex elpg_mutex; /* protect elpg enable/disable */
	/* disable -1, enable +1, <=0 elpg disabled, > 0 elpg enabled */
	int elpg_refcnt;

	union {
		struct pmu_perfmon_counter_v2 perfmon_counter_v2;
	};
	u32 perfmon_state_id[PMU_DOMAIN_GROUP_NUM];

	bool initialized;

	void (*remove_support)(struct nvgpu_pmu *pmu);
	bool sw_ready;
	bool perfmon_ready;

	u32 sample_buffer;
	u32 load_shadow;
	u32 load_avg;
	u32 load;

	struct nvgpu_mutex isr_mutex;
	bool isr_enabled;

	bool zbc_ready;
	union {
		struct pmu_cmdline_args_v3 args_v3;
		struct pmu_cmdline_args_v4 args_v4;
		struct pmu_cmdline_args_v5 args_v5;
		struct pmu_cmdline_args_v6 args_v6;
	};
	unsigned long perfmon_events_cnt;
	bool perfmon_sampling_enabled;
	u32 aelpg_param[5];
	u32 override_done;
};

struct pmu_surface {
	struct nvgpu_mem vidmem_desc;
	struct nvgpu_mem sysmem_desc;
	struct flcn_mem_desc_v0 params;
};

/*PG defines used by nvpgu-pmu*/
struct pmu_pg_stats_data {
	u32 gating_cnt;
	u32 ingating_time;
	u32 ungating_time;
	u32 avg_entry_latency_us;
	u32 avg_exit_latency_us;
};

/*!
 * Structure/object which single register write need to be done during PG init
 * sequence to set PROD values.
 */
struct pg_init_sequence_list {
	u32 regaddr;
	u32 writeval;
};

/* PMU IPC Methods */
void nvgpu_pmu_seq_init(struct nvgpu_pmu *pmu);

int nvgpu_pmu_mutex_acquire(struct nvgpu_pmu *pmu, u32 id, u32 *token);
int nvgpu_pmu_mutex_release(struct nvgpu_pmu *pmu, u32 id, u32 *token);

int nvgpu_pmu_queue_init(struct nvgpu_pmu *pmu, u32 id,
	union pmu_init_msg_pmu *init);
void nvgpu_pmu_queue_free(struct nvgpu_pmu *pmu, u32 id);

int nvgpu_pmu_queue_init_fb(struct nvgpu_pmu *pmu,
	u32 id, union pmu_init_msg_pmu *init);

/* send a cmd to pmu */
int nvgpu_pmu_cmd_post(struct gk20a *g, struct pmu_cmd *cmd,
		struct pmu_msg *msg, struct pmu_payload *payload,
		u32 queue_id, pmu_callback callback, void *cb_param,
		u32 *seq_desc);

bool nvgpu_pmu_queue_is_empty(struct nvgpu_pmu *pmu, u32 queue_id);
int nvgpu_pmu_process_message(struct nvgpu_pmu *pmu);

/* perfmon */
int nvgpu_pmu_init_perfmon(struct nvgpu_pmu *pmu);
int nvgpu_pmu_perfmon_start_sampling(struct nvgpu_pmu *pmu);
int nvgpu_pmu_perfmon_stop_sampling(struct nvgpu_pmu *pmu);
int nvgpu_pmu_perfmon_start_sampling_rpc(struct nvgpu_pmu *pmu);
int nvgpu_pmu_perfmon_stop_sampling_rpc(struct nvgpu_pmu *pmu);
int nvgpu_pmu_perfmon_get_samples_rpc(struct nvgpu_pmu *pmu);
int nvgpu_pmu_handle_perfmon_event(struct nvgpu_pmu *pmu,
	struct pmu_perfmon_msg *msg);
int nvgpu_pmu_init_perfmon_rpc(struct nvgpu_pmu *pmu);
int nvgpu_pmu_load_norm(struct gk20a *g, u32 *load);
int nvgpu_pmu_load_update(struct gk20a *g);
int nvgpu_pmu_busy_cycles_norm(struct gk20a *g, u32 *norm);
void nvgpu_pmu_reset_load_counters(struct gk20a *g);
void nvgpu_pmu_get_load_counters(struct gk20a *g, u32 *busy_cycles,
		u32 *total_cycles);

int nvgpu_pmu_handle_therm_event(struct nvgpu_pmu *pmu,
			struct nv_pmu_therm_msg *msg);

/* PMU init */
int nvgpu_init_pmu_support(struct gk20a *g);
int nvgpu_pmu_destroy(struct gk20a *g);
int nvgpu_pmu_process_init_msg(struct nvgpu_pmu *pmu,
	struct pmu_msg *msg);
int nvgpu_pmu_super_surface_alloc(struct gk20a *g,
	struct nvgpu_mem *mem_surface, u32 size);

void nvgpu_pmu_state_change(struct gk20a *g, u32 pmu_state,
	bool post_change_event);
void nvgpu_kill_task_pg_init(struct gk20a *g);

/* NVGPU-PMU MEM alloc */
void nvgpu_pmu_surface_free(struct gk20a *g, struct nvgpu_mem *mem);
void nvgpu_pmu_surface_describe(struct gk20a *g, struct nvgpu_mem *mem,
		struct flcn_mem_desc_v0 *fb);
int nvgpu_pmu_vidmem_surface_alloc(struct gk20a *g, struct nvgpu_mem *mem,
		u32 size);
int nvgpu_pmu_sysmem_surface_alloc(struct gk20a *g, struct nvgpu_mem *mem,
		u32 size);

/* PMU F/W support */
int nvgpu_early_init_pmu_sw(struct gk20a *g, struct nvgpu_pmu *pmu);
int nvgpu_pmu_prepare_ns_ucode_blob(struct gk20a *g);

/* PG init*/
int nvgpu_pmu_init_powergating(struct gk20a *g);
int nvgpu_pmu_init_bind_fecs(struct gk20a *g);
void nvgpu_pmu_setup_hw_load_zbc(struct gk20a *g);

/* PMU reset */
int nvgpu_pmu_reset(struct gk20a *g);

/* PG enable/disable */
int nvgpu_pmu_enable_elpg(struct gk20a *g);
int nvgpu_pmu_disable_elpg(struct gk20a *g);
int nvgpu_pmu_pg_global_enable(struct gk20a *g, bool enable_pg);

int nvgpu_pmu_get_pg_stats(struct gk20a *g, u32 pg_engine_id,
	struct pmu_pg_stats_data *pg_stat_data);

/* AELPG */
int nvgpu_aelpg_init(struct gk20a *g);
int nvgpu_aelpg_init_and_enable(struct gk20a *g, u8 ctrl_id);
int nvgpu_pmu_ap_send_command(struct gk20a *g,
		union pmu_ap_cmd *p_ap_cmd, bool b_block);

/* PMU debug */
void nvgpu_pmu_dump_falcon_stats(struct nvgpu_pmu *pmu);
void nvgpu_pmu_dump_elpg_stats(struct nvgpu_pmu *pmu);
bool nvgpu_find_hex_in_string(char *strings, struct gk20a *g, u32 *hex_pos);

/* PMU RPC */
int nvgpu_pmu_rpc_execute(struct nvgpu_pmu *pmu, struct nv_pmu_rpc_header *rpc,
	u16 size_rpc, u16 size_scratch, pmu_callback caller_cb,
	void *caller_cb_param, bool is_copy_back);


/* PMU wait*/
int pmu_wait_message_cond_status(struct nvgpu_pmu *pmu, u32 timeout_ms,
				void *var, u8 val);
void pmu_wait_message_cond(struct nvgpu_pmu *pmu, u32 timeout_ms,
				void *var, u8 val);
int nvgpu_pmu_wait_ready(struct gk20a *g);

void nvgpu_pmu_get_cmd_line_args_offset(struct gk20a *g,
	u32 *args_offset);

struct gk20a *gk20a_from_pmu(struct nvgpu_pmu *pmu);

/* super surface */
void nvgpu_pmu_create_ssmd_lookup_table(struct nvgpu_pmu *pmu);
u32 nvgpu_pmu_get_ss_member_set_offset(struct nvgpu_pmu *pmu, u32 member_id);
u32 nvgpu_pmu_get_ss_member_get_status_offset(struct nvgpu_pmu *pmu,
	u32 member_id);
u32 nvgpu_pmu_get_ss_member_set_size(struct nvgpu_pmu *pmu, u32 member_id);
u32 nvgpu_pmu_get_ss_member_get_status_size(struct nvgpu_pmu *pmu,
	u32 member_id);

#endif /* NVGPU_PMU_H */

