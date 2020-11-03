/*
 * Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <uapi/linux/nvgpu.h>

#include <nvgpu/kmem.h>
#include <nvgpu/log.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/nvgpu_init.h>
#include <nvgpu/profiler.h>
#include <nvgpu/regops.h>
#include <nvgpu/perfbuf.h>
#include <nvgpu/pm_reservation.h>
#include <nvgpu/tsg.h>
#include <nvgpu/fb.h>

#include "platform_gk20a.h"
#include "os_linux.h"
#include "ioctl_prof.h"
#include "ioctl_dbg.h"
#include "ioctl_tsg.h"
#include "ioctl.h"

/** @cond DOXYGEN_SHOULD_SKIP_THIS */
#if defined(CONFIG_NVGPU_NON_FUSA) && defined(CONFIG_NVGPU_NEXT)
#include "os/linux/nvgpu_next_ioctl_prof.h"
#endif
/** @endcond DOXYGEN_SHOULD_SKIP_THIS */

#define NVGPU_PROF_UMD_COPY_WINDOW_SIZE		SZ_4K

struct nvgpu_profiler_object_priv {
	struct nvgpu_profiler_object *prof;
	struct gk20a *g;

	/*
	 * Staging buffer to hold regops copied from userspace.
	 * Regops are stored in struct nvgpu_profiler_reg_op format. This
	 * struct is added for new profiler design and is trimmed down
	 * version of legacy regop struct nvgpu_dbg_reg_op.
	 *
	 * Struct nvgpu_profiler_reg_op is OS specific struct and cannot
	 * be used in common nvgpu code.
	 */
	struct nvgpu_profiler_reg_op *regops_umd_copy_buf;

	/*
	 * Staging buffer to execute regops in common code.
	 * Regops are stored in struct nvgpu_dbg_reg_op which is defined
	 * in common code.
	 *
	 * Regops in struct nvgpu_profiler_reg_op should be first converted
	 * to this format and this handle should be passed for regops
	 * execution.
	 */
	struct nvgpu_dbg_reg_op *regops_staging_buf;

	/*
	 * dmabuf handle of the buffer that would store available bytes in PMA buffer
	 * (if PMA stream resource is reserved successfully).
	 */
	struct dma_buf *pma_bytes_available_buffer_dmabuf;
};

static void nvgpu_prof_free_pma_stream_priv_data(struct nvgpu_profiler_object_priv *priv);

static int nvgpu_prof_fops_open(struct gk20a *g, struct file *filp,
		enum nvgpu_profiler_pm_reservation_scope scope)
{
	struct nvgpu_profiler_object_priv *prof_priv;
	struct nvgpu_profiler_object *prof;
	u32 num_regops;
	int err;

	nvgpu_log(g, gpu_dbg_prof, "Request to open profiler session with scope %u",
		scope);

	prof_priv = nvgpu_kzalloc(g, sizeof(*prof_priv));
	if (prof_priv == NULL) {
		return -ENOMEM;
	}

	err = nvgpu_profiler_alloc(g, &prof, scope);
	if (err != 0) {
		goto free_priv;
	}

	prof_priv->g = g;
	prof_priv->prof = prof;
	filp->private_data = prof_priv;

	prof_priv->regops_umd_copy_buf = nvgpu_kzalloc(g,
			NVGPU_PROF_UMD_COPY_WINDOW_SIZE);
	if (prof_priv->regops_umd_copy_buf == NULL) {
		goto free_prof;
	}

	num_regops = NVGPU_PROF_UMD_COPY_WINDOW_SIZE /
		     sizeof(prof_priv->regops_umd_copy_buf[0]);
	prof_priv->regops_staging_buf = nvgpu_kzalloc(g,
		num_regops * sizeof(prof_priv->regops_staging_buf[0]));
	if (prof_priv->regops_staging_buf == NULL) {
		goto free_umd_buf;
	}

	nvgpu_log(g, gpu_dbg_prof,
		"Profiler session with scope %u created successfully with profiler handle %u",
		scope, prof->prof_handle);

	return 0;

free_umd_buf:
	nvgpu_kfree(g, prof_priv->regops_umd_copy_buf);
free_prof:
	nvgpu_profiler_free(prof);
free_priv:
	nvgpu_kfree(g, prof_priv);
	return err;
}

int nvgpu_prof_dev_fops_open(struct inode *inode, struct file *filp)
{
	struct gk20a *g;
	int err;
	struct nvgpu_cdev *cdev;

	cdev = container_of(inode->i_cdev, struct nvgpu_cdev, cdev);
	g = nvgpu_get_gk20a_from_cdev(cdev);

	g = nvgpu_get(g);
	if (!g) {
		return -ENODEV;
	}

	if (!nvgpu_is_enabled(g, NVGPU_SUPPORT_PROFILER_V2_DEVICE)) {
		nvgpu_err(g, "Profiler V2 not supported");
		nvgpu_put(g);
		return -EINVAL;
	}

	err = nvgpu_prof_fops_open(g, filp,
			NVGPU_PROFILER_PM_RESERVATION_SCOPE_DEVICE);
	if (err != 0) {
		nvgpu_put(g);
	}

	return err;
}

int nvgpu_prof_ctx_fops_open(struct inode *inode, struct file *filp)
{
	struct gk20a *g;
	int err;
	struct nvgpu_cdev *cdev;

	cdev = container_of(inode->i_cdev, struct nvgpu_cdev, cdev);
	g = nvgpu_get_gk20a_from_cdev(cdev);

	g = nvgpu_get(g);
	if (!g) {
		return -ENODEV;
	}

	if (!nvgpu_is_enabled(g, NVGPU_SUPPORT_PROFILER_V2_CONTEXT)) {
		nvgpu_put(g);
		return -EINVAL;
	}

	err = nvgpu_prof_fops_open(g, filp,
			NVGPU_PROFILER_PM_RESERVATION_SCOPE_CONTEXT);
	if (err != 0) {
		nvgpu_put(g);
	}

	return err;
}

int nvgpu_prof_fops_release(struct inode *inode, struct file *filp)
{
	struct nvgpu_profiler_object_priv *prof_priv = filp->private_data;
	struct nvgpu_profiler_object *prof = prof_priv->prof;
	struct gk20a *g = prof_priv->g;

	nvgpu_log(g, gpu_dbg_prof,
		"Request to close profiler session with scope %u and profiler handle %u",
		prof->scope, prof->prof_handle);

	nvgpu_prof_free_pma_stream_priv_data(prof_priv);

	nvgpu_profiler_free(prof);

	nvgpu_kfree(g, prof_priv->regops_umd_copy_buf);
	nvgpu_kfree(g, prof_priv->regops_staging_buf);

	nvgpu_kfree(g, prof_priv);
	nvgpu_put(g);

	nvgpu_log(g, gpu_dbg_prof, "Profiler session closed successfully");

	return 0;
}

static int nvgpu_prof_ioctl_bind_context(struct nvgpu_profiler_object *prof,
		struct nvgpu_profiler_bind_context_args *args)
{
	int tsg_fd = args->tsg_fd;
	struct nvgpu_tsg *tsg;
	struct gk20a *g = prof->g;

	if (prof->context_init) {
		nvgpu_err(g, "Context info is already initialized");
		return -EINVAL;
	}

	if (tsg_fd < 0) {
		if (prof->scope == NVGPU_PROFILER_PM_RESERVATION_SCOPE_DEVICE) {
			prof->context_init = true;
			return 0;
		}
		return -EINVAL;
	}

	tsg = nvgpu_tsg_get_from_file(tsg_fd);
	if (tsg == NULL) {
		nvgpu_err(g, "invalid TSG fd %d", tsg_fd);
		return -EINVAL;
	}

	return nvgpu_profiler_bind_context(prof, tsg);
}

static int nvgpu_prof_ioctl_unbind_context(struct nvgpu_profiler_object *prof)
{
	return nvgpu_profiler_unbind_context(prof);
}

static int nvgpu_prof_ioctl_get_pm_resource_type(u32 resource,
		enum nvgpu_profiler_pm_resource_type *pm_resource)
{
	switch (resource) {
	case NVGPU_PROFILER_PM_RESOURCE_ARG_HWPM_LEGACY:
		*pm_resource = NVGPU_PROFILER_PM_RESOURCE_TYPE_HWPM_LEGACY;
		return 0;
	case NVGPU_PROFILER_PM_RESOURCE_ARG_SMPC:
		*pm_resource = NVGPU_PROFILER_PM_RESOURCE_TYPE_SMPC;
		return 0;
	default:
		break;
	}

	return -EINVAL;
}

static int nvgpu_prof_ioctl_reserve_pm_resource(struct nvgpu_profiler_object *prof,
		struct nvgpu_profiler_reserve_pm_resource_args *args)
{
	enum nvgpu_profiler_pm_resource_type pm_resource;
	struct gk20a *g = prof->g;
	bool flag_ctxsw;
	int err;

	if (!prof->context_init) {
		nvgpu_err(g, "Context info not initialized");
		return -EINVAL;
	}

	err = nvgpu_prof_ioctl_get_pm_resource_type(args->resource,
			&pm_resource);
	if (err) {
		nvgpu_err(prof->g, "invalid resource %u", args->resource);
		return err;
	}

	flag_ctxsw = ((args->flags & NVGPU_PROFILER_RESERVE_PM_RESOURCE_ARG_FLAG_CTXSW) != 0);

	switch (prof->scope) {
	case NVGPU_PROFILER_PM_RESERVATION_SCOPE_DEVICE:
		if (flag_ctxsw && (prof->tsg == NULL)) {
			nvgpu_err(g, "Context must be bound to enable context switch");
			return -EINVAL;
		}
		if (!flag_ctxsw	&& (pm_resource == NVGPU_PROFILER_PM_RESOURCE_TYPE_SMPC)
				&& !nvgpu_is_enabled(g, NVGPU_SUPPORT_SMPC_GLOBAL_MODE)) {
			nvgpu_err(g, "SMPC global mode not supported");
			return -EINVAL;
		}
		if (flag_ctxsw) {
			prof->ctxsw[pm_resource] = true;
		} else {
			prof->ctxsw[pm_resource] = false;
		}
		break;

	case NVGPU_PROFILER_PM_RESERVATION_SCOPE_CONTEXT:
		if (prof->tsg == NULL) {
			nvgpu_err(g, "Context must be bound for context session");
			return -EINVAL;
		}
		prof->ctxsw[pm_resource] = true;
		break;

	default:
		return -EINVAL;
	}

	err = nvgpu_profiler_pm_resource_reserve(prof, pm_resource);
	if (err) {
		return err;
	}

	return 0;
}

static int nvgpu_prof_ioctl_release_pm_resource(struct nvgpu_profiler_object *prof,
		struct nvgpu_profiler_release_pm_resource_args *args)
{
	enum nvgpu_profiler_pm_resource_type pm_resource;
	int err;

	err = nvgpu_prof_ioctl_get_pm_resource_type(args->resource,
			&pm_resource);
	if (err) {
		return err;
	}

	err = nvgpu_profiler_pm_resource_release(prof, pm_resource);
	if (err) {
		return err;
	}

	prof->ctxsw[pm_resource] = false;

	return 0;
}

static int nvgpu_prof_ioctl_alloc_pma_stream(struct nvgpu_profiler_object_priv *priv,
		struct nvgpu_profiler_alloc_pma_stream_args *args)
{
	struct nvgpu_profiler_object *prof = priv->prof;
	struct gk20a *g = prof->g;
	struct mm_gk20a *mm = &g->mm;
	u64 pma_bytes_available_buffer_offset;
	struct dma_buf *dmabuf;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	struct dma_buf_map map;
#endif
	void *cpuva;
	u32 pma_buffer_size;
	int err;

	nvgpu_log(g, gpu_dbg_prof, "Request to setup PMA stream for handle %u",
		prof->prof_handle);

	if (prof->pma_buffer_va != 0U) {
		nvgpu_err(g, "PMA stream already initialized");
		return -EINVAL;
	}

	err = nvgpu_profiler_alloc_pma_stream(prof);
	if (err != 0) {
		nvgpu_err(g, "failed to init PMA stream");
		return err;
	}

	/*
	 * PMA available byte buffer GPU_VA needs to fit in 32 bit
	 * register, hence use a fixed GPU_VA to map it.
	 */
	pma_bytes_available_buffer_offset = mm->perfbuf.pma_bytes_available_buffer_gpu_va;

	err = nvgpu_vm_map_buffer(mm->perfbuf.vm, args->pma_bytes_available_buffer_fd,
			&pma_bytes_available_buffer_offset,
			NVGPU_AS_MAP_BUFFER_FLAGS_FIXED_OFFSET, SZ_4K, 0, 0,
			0, 0, NULL);
	if (err != 0) {
		nvgpu_err(g, "failed to map available bytes buffer");
		goto err_put_vm;
	}

	/*
	 * Size register is 32-bit in HW, ensure requested size does
	 * not violate that.
	 */
	pma_buffer_size = nvgpu_safe_cast_u64_to_u32(args->pma_buffer_map_size);

	err = nvgpu_vm_map_buffer(mm->perfbuf.vm, args->pma_buffer_fd,
			&args->pma_buffer_offset, 0, SZ_4K, 0, 0,
			0, 0, NULL);
	if (err != 0) {
		nvgpu_err(g, "failed to map PMA buffer");
		goto err_unmap_bytes_available;
	}

	dmabuf = dma_buf_get(args->pma_bytes_available_buffer_fd);
	if (IS_ERR(dmabuf)) {
		err = -EINVAL;
		nvgpu_err(g, "failed to get available bytes buffer FD");
		goto err_unmap_pma;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	err = dma_buf_vmap(dmabuf, &map);
	cpuva = err ? NULL : map.vaddr;
#else
	cpuva = dma_buf_vmap(dmabuf);
#endif
	if (cpuva == NULL) {
		err = -ENOMEM;
		nvgpu_err(g, "failed to vmap available bytes buffer FD");
		goto err_dma_buf_put;
	}

	prof->pma_buffer_va = args->pma_buffer_offset;
	prof->pma_buffer_size = pma_buffer_size;
	prof->pma_bytes_available_buffer_va = pma_bytes_available_buffer_offset;
	prof->pma_bytes_available_buffer_cpuva = cpuva;
	priv->pma_bytes_available_buffer_dmabuf = dmabuf;

	nvgpu_log(g, gpu_dbg_prof, "PMA stream initialized for profiler handle %u, 0x%llx 0x%x 0x%llx",
		prof->prof_handle, prof->pma_buffer_va, prof->pma_buffer_size,
		prof->pma_bytes_available_buffer_va);

	args->pma_buffer_va = args->pma_buffer_offset;

	return 0;

err_dma_buf_put:
	dma_buf_put(dmabuf);
err_unmap_pma:
	nvgpu_vm_unmap(mm->perfbuf.vm, args->pma_buffer_offset, NULL);
err_unmap_bytes_available:
	nvgpu_vm_unmap(mm->perfbuf.vm, pma_bytes_available_buffer_offset, NULL);
err_put_vm:
	nvgpu_perfbuf_deinit_vm(g);
	nvgpu_profiler_pm_resource_release(prof,
			NVGPU_PROFILER_PM_RESOURCE_TYPE_PMA_STREAM);
	return err;
}

static void nvgpu_prof_free_pma_stream_priv_data(struct nvgpu_profiler_object_priv *priv)
{
	struct nvgpu_profiler_object *prof = priv->prof;
	struct gk20a *g = prof->g;
	struct mm_gk20a *mm = &g->mm;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	struct dma_buf_map map;
#endif

	if (priv->pma_bytes_available_buffer_dmabuf == NULL) {
		return;
	}

	nvgpu_vm_unmap(mm->perfbuf.vm, prof->pma_bytes_available_buffer_va, NULL);
	prof->pma_bytes_available_buffer_va = 0U;

	nvgpu_vm_unmap(mm->perfbuf.vm, prof->pma_buffer_va, NULL);
	prof->pma_buffer_va = 0U;
	prof->pma_buffer_size = 0U;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	dma_buf_map_set_vaddr(&map, prof->pma_bytes_available_buffer_cpuva);
	dma_buf_vunmap(priv->pma_bytes_available_buffer_dmabuf, &map);
#else
	dma_buf_vunmap(priv->pma_bytes_available_buffer_dmabuf,
		prof->pma_bytes_available_buffer_cpuva);
#endif
	dma_buf_put(priv->pma_bytes_available_buffer_dmabuf);
	priv->pma_bytes_available_buffer_dmabuf = NULL;
	prof->pma_bytes_available_buffer_cpuva = NULL;
}

static int nvgpu_prof_ioctl_free_pma_stream(struct nvgpu_profiler_object_priv *priv)
{
	struct nvgpu_profiler_object *prof = priv->prof;
	struct gk20a *g = prof->g;

	nvgpu_log(g, gpu_dbg_prof, "Request to free PMA stream for handle %u",
		prof->prof_handle);

	if (prof->pma_buffer_va == 0U) {
		nvgpu_err(g, "PMA stream not initialized");
		return -EINVAL;
	}

	if (prof->bound) {
		nvgpu_err(g, "PM resources are bound, cannot free PMA");
		return -EINVAL;
	}

	nvgpu_prof_free_pma_stream_priv_data(priv);
	nvgpu_profiler_free_pma_stream(prof);

	nvgpu_log(g, gpu_dbg_prof, "Request to free PMA stream for handle %u completed",
		prof->prof_handle);

	return 0;
}

static int nvgpu_prof_ioctl_bind_pm_resources(struct nvgpu_profiler_object *prof)
{
	return nvgpu_profiler_bind_pm_resources(prof);
}

static int nvgpu_prof_ioctl_unbind_pm_resources(struct nvgpu_profiler_object *prof)
{
	return nvgpu_profiler_unbind_pm_resources(prof);
}

static void nvgpu_prof_get_regops_staging_data(struct nvgpu_profiler_reg_op *in,
		struct nvgpu_dbg_reg_op *out, u32 num_ops)
{
	u32 i;

	for (i = 0; i < num_ops; i++) {
		out[i].op = nvgpu_get_regops_op_values_common(in[i].op);
		out[i].type = 0U; /* Selected based on per-resource ctxsw flags */
		out[i].status = nvgpu_get_regops_status_values_common(in[i].status);
		out[i].quad = 0U;
		out[i].group_mask = 0U;
		out[i].sub_group_mask = 0U;
		out[i].offset = in[i].offset;
		out[i].value_lo = u64_lo32(in[i].value);
		out[i].value_hi = u64_hi32(in[i].value);
		out[i].and_n_mask_lo = u64_lo32(in[i].and_n_mask);
		out[i].and_n_mask_hi = u64_hi32(in[i].and_n_mask);
	}
}

static void nvgpu_prof_get_regops_linux_data(struct nvgpu_dbg_reg_op *in,
		struct nvgpu_profiler_reg_op *out, u32 num_ops)
{
	u32 i;

	for (i = 0; i < num_ops; i++) {
		out[i].op = nvgpu_get_regops_op_values_linux(in[i].op);
		out[i].status = nvgpu_get_regops_status_values_linux(in[i].status);
		out[i].offset = in[i].offset;
		out[i].value = hi32_lo32_to_u64(in[i].value_hi, in[i].value_lo);
		out[i].and_n_mask = hi32_lo32_to_u64(in[i].and_n_mask_hi, in[i].and_n_mask_lo);
	}
}

static int nvgpu_prof_ioctl_exec_reg_ops(struct nvgpu_profiler_object_priv *priv,
		struct nvgpu_profiler_exec_reg_ops_args *args)
{
	struct nvgpu_profiler_object *prof = priv->prof;
	struct gk20a *g = prof->g;
	struct nvgpu_tsg *tsg = prof->tsg;
	u32 num_regops_in_copy_buf = NVGPU_PROF_UMD_COPY_WINDOW_SIZE /
				     sizeof(priv->regops_umd_copy_buf[0]);
	u32 ops_offset = 0;
	u32 flags = 0U;
	bool all_passed = true;
	int err;

	nvgpu_log(g, gpu_dbg_prof,
		"REG_OPS for handle %u: count=%u mode=%u flags=0x%x",
		prof->prof_handle, args->count, args->mode, args->flags);

	if (args->count == 0) {
		return -EINVAL;
	}

	if (args->count > NVGPU_IOCTL_DBG_REG_OPS_LIMIT) {
		nvgpu_err(g, "regops limit exceeded");
		return -EINVAL;
	}

	if (!prof->bound) {
		nvgpu_err(g, "PM resources are not bound to profiler");
		return -EINVAL;
	}

	err = gk20a_busy(g);
	if (err != 0) {
		nvgpu_err(g, "failed to poweron");
		return -EINVAL;
	}

	if (args->mode == NVGPU_PROFILER_EXEC_REG_OPS_ARG_MODE_CONTINUE_ON_ERROR) {
		flags |= NVGPU_REG_OP_FLAG_MODE_CONTINUE_ON_ERROR;
	} else {
		flags |= NVGPU_REG_OP_FLAG_MODE_ALL_OR_NONE;
	}

	while (ops_offset < args->count) {
		const u32 num_ops =
			min(args->count - ops_offset, num_regops_in_copy_buf);
		const u64 fragment_size =
			num_ops * sizeof(priv->regops_umd_copy_buf[0]);
		void __user *const user_fragment =
			(void __user *)(uintptr_t)
			(args->ops +
			 ops_offset * sizeof(priv->regops_umd_copy_buf[0]));

		nvgpu_log(g, gpu_dbg_prof, "Regops fragment: start_op=%u ops=%u",
			     ops_offset, num_ops);

		if (copy_from_user(priv->regops_umd_copy_buf,
				   user_fragment, fragment_size)) {
			nvgpu_err(g, "copy_from_user failed!");
			err = -EFAULT;
			break;
		}

		nvgpu_prof_get_regops_staging_data(
			priv->regops_umd_copy_buf,
			priv->regops_staging_buf, num_ops);

		if (args->mode == NVGPU_PROFILER_EXEC_REG_OPS_ARG_MODE_CONTINUE_ON_ERROR) {
			flags &= ~NVGPU_REG_OP_FLAG_ALL_PASSED;
		}

		err = g->ops.regops.exec_regops(g, tsg, prof,
			priv->regops_staging_buf, num_ops,
			&flags);
		if (err) {
			nvgpu_err(g, "regop execution failed");
			break;
		}

		if (ops_offset == 0) {
			if (flags & NVGPU_REG_OP_FLAG_DIRECT_OPS) {
				args->flags |=
					NVGPU_PROFILER_EXEC_REG_OPS_ARG_FLAG_DIRECT_OPS;
			}
		}

		if (args->mode == NVGPU_PROFILER_EXEC_REG_OPS_ARG_MODE_CONTINUE_ON_ERROR) {
			if ((flags & NVGPU_REG_OP_FLAG_ALL_PASSED) == 0) {
				all_passed = false;
			}
		}

		 nvgpu_prof_get_regops_linux_data(
			priv->regops_staging_buf,
			priv->regops_umd_copy_buf, num_ops);

		if (copy_to_user(user_fragment,
				 priv->regops_umd_copy_buf,
				 fragment_size)) {
			nvgpu_err(g, "copy_to_user failed!");
			err = -EFAULT;
			break;
		}

		ops_offset += num_ops;
	}

	if (args->mode == NVGPU_PROFILER_EXEC_REG_OPS_ARG_MODE_CONTINUE_ON_ERROR
			&& all_passed && (err == 0)) {
		args->flags |= NVGPU_PROFILER_EXEC_REG_OPS_ARG_FLAG_ALL_PASSED;
	}

	nvgpu_log(g, gpu_dbg_prof,
		"REG_OPS for handle %u complete: count=%u mode=%u flags=0x%x err=%d",
		prof->prof_handle, args->count, args->mode, args->flags, err);

	gk20a_idle(g);

	return err;
}

static int nvgpu_prof_ioctl_pma_stream_update_get_put(struct nvgpu_profiler_object *prof,
		struct nvgpu_profiler_pma_stream_update_get_put_args *args)
{
	bool update_bytes_available = args->flags &
		NVGPU_PROFILER_PMA_STREAM_UPDATE_GET_PUT_ARG_FLAG_UPDATE_AVAILABLE_BYTES;
	bool wait = args->flags &
		NVGPU_PROFILER_PMA_STREAM_UPDATE_GET_PUT_ARG_FLAG_WAIT_FOR_UPDATE;
	bool update_put_ptr = args->flags &
		NVGPU_PROFILER_PMA_STREAM_UPDATE_GET_PUT_ARG_FLAG_RETURN_PUT_PTR;
	struct gk20a *g = prof->g;
	bool overflowed;
	int err;

	nvgpu_log(g, gpu_dbg_prof,
		"Update PMA stream request %u: flags = 0x%x bytes_consumed=%llu",
		prof->prof_handle, args->flags, args->bytes_consumed);

	if (!prof->reserved[NVGPU_PROFILER_PM_RESOURCE_TYPE_PMA_STREAM]) {
		nvgpu_err(g, "PMA stream resource not reserved");
		return -EINVAL;
	}

	err = nvgpu_perfbuf_update_get_put(prof->g, args->bytes_consumed,
			update_bytes_available ? &args->bytes_available : NULL,
			prof->pma_bytes_available_buffer_cpuva, wait,
			update_put_ptr ? &args->put_ptr : NULL,
			&overflowed);
	if (err != 0) {
		return err;
	}

	if (overflowed) {
		args->flags |=
			NVGPU_PROFILER_PMA_STREAM_UPDATE_GET_PUT_ARG_FLAG_OVERFLOW_TRIGGERED;
	}

	nvgpu_log(g, gpu_dbg_prof,
		"Update PMA stream request %u complete: flags = 0x%x"
		"bytes_available=%llu put_ptr=%llu",
		prof->prof_handle, args->flags, args->bytes_available, args->put_ptr);

	return 0;
}

long nvgpu_prof_fops_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct nvgpu_profiler_object_priv *prof_priv = filp->private_data;
	struct nvgpu_profiler_object *prof = prof_priv->prof;
	struct gk20a *g = prof_priv->g;
	u8 __maybe_unused buf[NVGPU_PROFILER_IOCTL_MAX_ARG_SIZE];
	int err = 0;

	if ((_IOC_TYPE(cmd) != NVGPU_PROFILER_IOCTL_MAGIC) ||
	    (_IOC_NR(cmd) == 0) ||
	    (_IOC_NR(cmd) > NVGPU_PROFILER_IOCTL_LAST) ||
	    (_IOC_SIZE(cmd) > NVGPU_PROFILER_IOCTL_MAX_ARG_SIZE)) {
		return -EINVAL;
	}

	(void) memset(buf, 0, sizeof(buf));
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (copy_from_user(buf, (void __user *)arg, _IOC_SIZE(cmd))) {
			return -EFAULT;
		}
	}

	nvgpu_log(g, gpu_dbg_prof, "Profiler handle %u received IOCTL cmd %u",
		prof->prof_handle, cmd);

	nvgpu_mutex_acquire(&prof->ioctl_lock);

	nvgpu_speculation_barrier();

	switch (cmd) {
	case NVGPU_PROFILER_IOCTL_BIND_CONTEXT:
		err = nvgpu_prof_ioctl_bind_context(prof,
			(struct nvgpu_profiler_bind_context_args *)buf);
		break;

	case NVGPU_PROFILER_IOCTL_UNBIND_CONTEXT:
		err = nvgpu_prof_ioctl_unbind_context(prof);
		break;

	case NVGPU_PROFILER_IOCTL_RESERVE_PM_RESOURCE:
		err = nvgpu_prof_ioctl_reserve_pm_resource(prof,
			(struct nvgpu_profiler_reserve_pm_resource_args *)buf);
		break;

	case NVGPU_PROFILER_IOCTL_RELEASE_PM_RESOURCE:
		err = nvgpu_prof_ioctl_release_pm_resource(prof,
			(struct nvgpu_profiler_release_pm_resource_args *)buf);
		break;

	case NVGPU_PROFILER_IOCTL_BIND_PM_RESOURCES:
		err = nvgpu_prof_ioctl_bind_pm_resources(prof);
		break;

	case NVGPU_PROFILER_IOCTL_UNBIND_PM_RESOURCES:
		err = nvgpu_prof_ioctl_unbind_pm_resources(prof);
		break;

	case NVGPU_PROFILER_IOCTL_ALLOC_PMA_STREAM:
		err = nvgpu_prof_ioctl_alloc_pma_stream(prof_priv,
			(struct nvgpu_profiler_alloc_pma_stream_args *)buf);
		break;

	case NVGPU_PROFILER_IOCTL_FREE_PMA_STREAM:
		err = nvgpu_prof_ioctl_free_pma_stream(prof_priv);
		break;

	case NVGPU_PROFILER_IOCTL_EXEC_REG_OPS:
		err = nvgpu_prof_ioctl_exec_reg_ops(prof_priv,
			(struct nvgpu_profiler_exec_reg_ops_args *)buf);
		break;

	case NVGPU_PROFILER_IOCTL_PMA_STREAM_UPDATE_GET_PUT:
		err = nvgpu_prof_ioctl_pma_stream_update_get_put(prof,
			(struct nvgpu_profiler_pma_stream_update_get_put_args *)buf);
		break;

	default:
#if defined(CONFIG_NVGPU_NON_FUSA) && defined(CONFIG_NVGPU_NEXT)
		err = nvgpu_next_prof_fops_ioctl(prof, cmd, (void *)buf);
#else
		nvgpu_err(g, "unrecognized profiler ioctl cmd: 0x%x", cmd);
		err = -ENOTTY;
#endif
		break;
	}

	nvgpu_mutex_release(&prof->ioctl_lock);

	if ((err == 0) && (_IOC_DIR(cmd) & _IOC_READ))
		err = copy_to_user((void __user *)arg,
				   buf, _IOC_SIZE(cmd));

	nvgpu_log(g, gpu_dbg_prof, "Profiler handle %u IOCTL err =  %d",
		prof->prof_handle, err);

	return err;
}
