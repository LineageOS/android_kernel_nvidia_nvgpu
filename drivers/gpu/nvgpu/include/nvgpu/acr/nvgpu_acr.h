/*
 * Copyright (c) 2016-2019, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef NVGPU_ACR_H
#define NVGPU_ACR_H

#include <nvgpu/falcon.h>

#include "gk20a/mm_gk20a.h"

#include "acr_lsfm.h"
#include "acr_flcnbl.h"
#include "acr_objlsfm.h"
#include "acr_objflcn.h"

struct nvgpu_firmware;
struct gk20a;
struct hs_acr_ops;
struct hs_acr;
struct nvgpu_acr;

#define HSBIN_ACR_BL_UCODE_IMAGE "pmu_bl.bin"
#define HSBIN_ACR_UCODE_IMAGE "acr_ucode.bin"
#define HSBIN_ACR_AHESASC_PROD_UCODE "acr_ahesasc_prod_ucode.bin"
#define HSBIN_ACR_ASB_PROD_UCODE "acr_asb_prod_ucode.bin"
#define HSBIN_ACR_AHESASC_DBG_UCODE "acr_ahesasc_dbg_ucode.bin"
#define HSBIN_ACR_ASB_DBG_UCODE "acr_asb_dbg_ucode.bin"

#define GM20B_FECS_UCODE_SIG "fecs_sig.bin"
#define T18x_GPCCS_UCODE_SIG "gpccs_sig.bin"

#define LSF_SEC2_UCODE_IMAGE_BIN "sec2_ucode_image.bin"
#define LSF_SEC2_UCODE_DESC_BIN "sec2_ucode_desc.bin"
#define LSF_SEC2_UCODE_SIG_BIN "sec2_sig.bin"

#define ACR_COMPLETION_TIMEOUT_MS 10000U /*in msec */

#define nvgpu_acr_dbg(g, fmt, args...) \
	nvgpu_log(g, gpu_dbg_pmu, fmt, ##args)

struct bin_hdr {
	/* 0x10de */
	u32 bin_magic;
	/* versioning of bin format */
	u32 bin_ver;
	/* Entire image size including this header */
	u32 bin_size;
	/*
	 * Header offset of executable binary metadata,
	 * start @ offset- 0x100 *
	 */
	u32 header_offset;
	/*
	 * Start of executable binary data, start @
	 * offset- 0x200
	 */
	u32 data_offset;
	/* Size of executable binary */
	u32 data_size;
};

struct acr_fw_header {
	u32 sig_dbg_offset;
	u32 sig_dbg_size;
	u32 sig_prod_offset;
	u32 sig_prod_size;
	u32 patch_loc;
	u32 patch_sig;
	u32 hdr_offset; /* This header points to acr_ucode_header_t210_load */
	u32 hdr_size; /* Size of above header */
};

struct wpr_carveout_info {
	u64 wpr_base;
	u64 nonwpr_base;
	u64 size;
};

/* ACR interfaces */

struct acr_lsf_config {
	u32 falcon_id;
	u32 falcon_dma_idx;
	bool is_lazy_bootstrap;
	bool is_priv_load;

	int (*get_lsf_ucode_details)(struct gk20a *g, void *lsf_ucode_img);
	void (*get_cmd_line_args_offset)(struct gk20a *g, u32 *args_offset);
};

struct hs_flcn_bl {
	const char *bl_fw_name;
	struct nvgpu_firmware *hs_bl_fw;
	struct hsflcn_bl_desc *hs_bl_desc;
	struct bin_hdr *hs_bl_bin_hdr;
	struct nvgpu_mem hs_bl_ucode;
};

struct hs_acr {
	u32 acr_type;

	/* HS bootloader to validate & load ACR ucode */
	struct hs_flcn_bl acr_hs_bl;

	/* ACR ucode */
	const char *acr_fw_name;
	struct nvgpu_firmware *acr_fw;
	struct nvgpu_mem acr_ucode;

	union {
		struct flcn_bl_dmem_desc bl_dmem_desc;
		struct flcn_bl_dmem_desc_v1 bl_dmem_desc_v1;
	};

	void *ptr_bl_dmem_desc;
	u32 bl_dmem_desc_size;

	union{
		struct flcn_acr_desc *acr_dmem_desc;
		struct flcn_acr_desc_v1 *acr_dmem_desc_v1;
	};

	/* Falcon used to execute ACR ucode */
	struct nvgpu_falcon *acr_flcn;

	int (*acr_flcn_setup_hw_and_bl_bootstrap)(struct gk20a *g,
		struct nvgpu_falcon_bl_info *bl_info);
	void (*report_acr_engine_bus_err_status)(struct gk20a *g,
		u32 bar0_status, u32 error_type);
	int (*acr_engine_bus_err_status)(struct gk20a *g, u32 *bar0_status,
		u32 *error_type);
};

#define ACR_DEFAULT	0U
#define ACR_AHESASC	1U
#define ACR_ASB		2U

struct nvgpu_acr {
	struct gk20a *g;

	u32 bootstrap_owner;

	u32 lsf_enable_mask;
	struct acr_lsf_config lsf[FALCON_ID_END];

	/*
	 * non-wpr space to hold LSF ucodes,
	 * ACR does copy ucode from non-wpr to wpr
	 */
	struct nvgpu_mem ucode_blob;
	/*
	 * Even though this mem_desc wouldn't be used,
	 * the wpr region needs to be reserved in the
	 * allocator in dGPU case.
	 */
	struct nvgpu_mem wpr_dummy;

	/* ACR member for different types of ucode */
	/* For older dgpu/tegra ACR cuode */
	struct hs_acr acr;
	/* ACR load split feature support */
	struct hs_acr acr_ahesasc;
	struct hs_acr acr_asb;

	int (*prepare_ucode_blob)(struct gk20a *g);
	void (*get_wpr_info)(struct gk20a *g, struct wpr_carveout_info *inf);
	int (*alloc_blob_space)(struct gk20a *g, size_t size,
		struct nvgpu_mem *mem);
	int (*patch_wpr_info_to_ucode)(struct gk20a *g, struct nvgpu_acr *acr,
		struct hs_acr *acr_desc, bool is_recovery);
	int (*acr_fill_bl_dmem_desc)(struct gk20a *g,
		struct nvgpu_acr *acr, struct hs_acr *acr_desc,
		u32 *acr_ucode_header);
	int (*bootstrap_hs_acr)(struct gk20a *g, struct nvgpu_acr *acr,
		struct hs_acr *acr_desc);

	void (*remove_support)(struct nvgpu_acr *acr);
};

int nvgpu_acr_bootstrap_hs_ucode(struct gk20a *g, struct nvgpu_acr *acr,
	struct hs_acr *acr_desc);
int nvgpu_acr_alloc_blob_space_sys(struct gk20a *g, size_t size,
	struct nvgpu_mem *mem);
int nvgpu_acr_alloc_blob_space_vid(struct gk20a *g, size_t size,
	struct nvgpu_mem *mem);
void nvgpu_acr_wpr_info_sys(struct gk20a *g, struct wpr_carveout_info *inf);
void nvgpu_acr_wpr_info_vid(struct gk20a *g, struct wpr_carveout_info *inf);

int nvgpu_acr_prepare_ucode_blob_v0(struct gk20a *g);
int nvgpu_acr_prepare_ucode_blob_v1(struct gk20a *g);

int nvgpu_acr_lsf_pmu_ucode_details_v0(struct gk20a *g, void *lsf_ucode_img);
int nvgpu_acr_lsf_fecs_ucode_details_v0(struct gk20a *g, void *lsf_ucode_img);
int nvgpu_acr_lsf_gpccs_ucode_details_v0(struct gk20a *g, void *lsf_ucode_img);

int nvgpu_acr_lsf_pmu_ucode_details_v1(struct gk20a *g, void *lsf_ucode_img);
int nvgpu_acr_lsf_fecs_ucode_details_v1(struct gk20a *g, void *lsf_ucode_img);
int nvgpu_acr_lsf_gpccs_ucode_details_v1(struct gk20a *g, void *lsf_ucode_img);
int nvgpu_acr_lsf_sec2_ucode_details_v1(struct gk20a *g, void *lsf_ucode_img);

void nvgpu_acr_init(struct gk20a *g);
int nvgpu_acr_construct_execute(struct gk20a *g);

int nvgpu_acr_self_hs_load_bootstrap(struct gk20a *g, struct nvgpu_falcon *flcn,
	struct nvgpu_firmware *hs_fw, u32 timeout);

#endif /* NVGPU_ACR_H */

