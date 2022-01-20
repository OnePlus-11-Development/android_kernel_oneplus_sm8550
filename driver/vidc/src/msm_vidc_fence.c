// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "msm_vidc_fence.h"
#include "msm_vidc_debug.h"

static const char *msm_vidc_dma_fence_get_driver_name(struct dma_fence *df)
{
	struct msm_vidc_fence *fence;

	if (df) {
		fence = container_of(df, struct msm_vidc_fence, dma_fence);
		return fence->name;
	}
	return "msm_vidc_dma_fence_get_driver_name: invalid fence";
}

static const char *msm_vidc_dma_fence_get_timeline_name(struct dma_fence *df)
{
	struct msm_vidc_fence *fence;

	if (df) {
		fence = container_of(df, struct msm_vidc_fence, dma_fence);
		return fence->name;
	}
	return "msm_vidc_dma_fence_get_timeline_name: invalid fence";
}

static void msm_vidc_dma_fence_release(struct dma_fence *df)
{
	struct msm_vidc_fence *fence;

	if (df) {
		fence = container_of(df, struct msm_vidc_fence, dma_fence);
		d_vpr_l("%s: name %s\n", __func__, fence->name);
		kfree(fence);
	} else {
		d_vpr_e("%s: invalid fence\n", __func__);
	}
}

static const struct dma_fence_ops msm_vidc_dma_fence_ops = {
	.get_driver_name = msm_vidc_dma_fence_get_driver_name,
	.get_timeline_name = msm_vidc_dma_fence_get_timeline_name,
	.release = msm_vidc_dma_fence_release,
};

int msm_vidc_fence_create(struct msm_vidc_inst *inst,
		struct msm_vidc_buffer *buf)
{
	int rc = 0;
	struct msm_vidc_fence *fence;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;

	spin_lock_init(&fence->lock);
	dma_fence_init(&fence->dma_fence, &msm_vidc_dma_fence_ops,
		&fence->lock, inst->fence.ctx_num, inst->fence.seq_num++);
	snprintf(fence->name, sizeof(fence->name), "%s: %llu",
		inst->fence.name, inst->fence.seq_num);

	fence->fd = get_unused_fd_flags(0);
	if (fence->fd < 0) {
		i_vpr_e(inst, "%s: getting fd (%d) failed\n", __func__,
			fence->fd);
		rc = -EINVAL;
		goto err_fd;
	}
	fence->sync_file = sync_file_create(&fence->dma_fence);
	if (!fence->sync_file) {
		i_vpr_e(inst, "%s: sync_file_create failed\n", __func__);
		rc = -EINVAL;
		goto err_sync_file;
	}
	fd_install(fence->fd, fence->sync_file->file);

	buf->fence = fence;
	i_vpr_h(inst, "%s: created %s\n", __func__, fence->name);
	return 0;

err_sync_file:
	put_unused_fd(fence->fd);
err_fd:
	dma_fence_put(&fence->dma_fence);
	buf->fence = NULL;
	return rc;
}

int msm_vidc_fence_signal(struct msm_vidc_inst *inst,
		struct msm_vidc_buffer *buf)
{
	int rc = 0;

	if (!inst || !buf) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	if (!buf->fence)
		return 0;

	i_vpr_l(inst, "%s: fence %s\n", __func__, buf->fence->name);
	dma_fence_signal(&buf->fence->dma_fence);
	dma_fence_put(&buf->fence->dma_fence);
	buf->fence = NULL;

	return rc;
}

void msm_vidc_fence_destroy(struct msm_vidc_inst *inst,
		struct msm_vidc_buffer *buf)
{
	if (!inst || !buf) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}
	if (!buf->fence)
		return;

	i_vpr_e(inst, "%s: fence %s\n", __func__, buf->fence->name);
	dma_fence_set_error(&buf->fence->dma_fence, -EINVAL);
	dma_fence_signal(&buf->fence->dma_fence);
	dma_fence_put(&buf->fence->dma_fence);
	buf->fence = NULL;
}

int msm_vidc_fence_init(struct msm_vidc_inst *inst)
{
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	inst->fence.ctx_num = dma_fence_context_alloc(1);
	snprintf(inst->fence.name, sizeof(inst->fence.name),
		"msm_vidc_fence: %s: %llu", inst->debug_str,
		inst->fence.ctx_num);
	i_vpr_h(inst, "%s: %s\n", __func__, inst->fence.name);

       return rc;
}

void msm_vidc_fence_deinit(struct msm_vidc_inst *inst)
{
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}
	i_vpr_h(inst, "%s: %s\n", __func__, inst->fence.name);
	inst->fence.ctx_num = 0;
	snprintf(inst->fence.name, sizeof(inst->fence.name), "%s", "");
}
