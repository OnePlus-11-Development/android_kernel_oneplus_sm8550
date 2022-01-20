/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __H_MSM_VIDC_FENCE_H__
#define __H_MSM_VIDC_FENCE_H__

#include "msm_vidc_inst.h"
#include "msm_vidc_buffer.h"

int msm_vidc_fence_create(struct msm_vidc_inst *inst,
		struct msm_vidc_buffer *buf);
int msm_vidc_fence_signal(struct msm_vidc_inst *inst,
		struct msm_vidc_buffer *buf);
void msm_vidc_fence_destroy(struct msm_vidc_inst *inst,
		struct msm_vidc_buffer *buf);
int msm_vidc_fence_init(struct msm_vidc_inst *inst);
void msm_vidc_fence_deinit(struct msm_vidc_inst *inst);

#endif // __H_MSM_VIDC_FENCE_H__
