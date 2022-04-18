// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/gunyah/gh_msgq.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/of.h>

#include <mmrm_vm_interface.h>
#include "mmrm_vm_fe.h"
#include "mmrm_vm_debug.h"

void mmrm_vm_fe_msgq_msg_handler(struct work_struct *work)
{
	struct mmrm_vm_thread_info *pthread_info =
		container_of(work, struct mmrm_vm_thread_info, msgq_work.work);
	struct mmrm_vm_driver_data *mmrm_vm =
		container_of(pthread_info, struct mmrm_vm_driver_data, thread_info);
	struct list_head head;
	struct mmrm_vm_msg *next_msg;
	struct mmrm_vm_msg *msg;

	mutex_lock(&pthread_info->list_lock);
	list_replace_init(&pthread_info->queued_msg, &head);
	mutex_unlock(&pthread_info->list_lock);

	list_for_each_entry_safe(msg, next_msg, &head, link) {
		mmrm_vm_fe_recv(mmrm_vm, msg->msg_buf, msg->msg_size);
		list_del(&msg->link);
		kfree(msg);
	}
}

int mmrm_vm_msgq_listener(void *data)
{
	struct mmrm_vm_driver_data *mmrm_vm = (struct mmrm_vm_driver_data *)data;

	struct mmrm_vm_gh_msgq_info *pmsg_info = &mmrm_vm->msg_info;
	struct mmrm_vm_thread_info *pthread_info = &mmrm_vm->thread_info;
	size_t size;
	int ret = 0;
	struct mmrm_vm_msg *msg;

	while (true) {
		msg = kzalloc(sizeof(*msg), GFP_KERNEL);
		if (!msg)
			return -ENOMEM;

		ret = gh_msgq_recv(pmsg_info->msgq_handle, msg->msg_buf,
				GH_MSGQ_MAX_MSG_SIZE_BYTES, &size, 0);

		if (ret < 0) {
			kfree(msg);
			d_mpr_e("gh_msgq_recv failed, rc=%d\n", ret);
			return -EINVAL;
		}

		msg->msg_size = size;
		list_add_tail(&pthread_info->queued_msg, &msg->link);
		mutex_unlock(&pthread_info->list_lock);

		queue_delayed_work(pthread_info->msg_workq,
				 &pthread_info->msgq_work, msecs_to_jiffies(0));
	}
	return 0;
}

int mmrm_vm_msgq_send(struct mmrm_vm_driver_data *mmrm_vm, void *msg, size_t msg_size)
{
	int  rc;
	struct mmrm_vm_gh_msgq_info *pmsg_info;

	if (IS_ERR_OR_NULL(mmrm_vm))
		return -EINVAL;

	pmsg_info = &mmrm_vm->msg_info;

	if (!pmsg_info->msgq_handle) {
		d_mpr_e("Failed to send msg, invalid msgq handle\n");
		return -EINVAL;
	}

	if (msg_size > GH_MSGQ_MAX_MSG_SIZE_BYTES) {
		d_mpr_e("msg size unsupported for msgq: %ld > %d\n", msg_size,
			GH_MSGQ_MAX_MSG_SIZE_BYTES);
		return -E2BIG;
	}

	rc = gh_msgq_send(pmsg_info->msgq_handle, msg, msg_size, 0);
	d_mpr_e("%s: handle=%p result:%d\n", __func__, pmsg_info->msgq_handle, rc);

	return rc;
}

int mmrm_vm_msgq_init(struct mmrm_vm_driver_data *mmrm_vm)
{
	int rc = 0;
	struct mmrm_vm_gh_msgq_info *pmsg_info;
	struct mmrm_vm_thread_info *pthread_info;

	if (IS_ERR_OR_NULL(mmrm_vm))
		return -EINVAL;

	pmsg_info = &mmrm_vm->msg_info;
	pthread_info = &mmrm_vm->thread_info;


	pthread_info->msg_workq = create_singlethread_workqueue("vm_be_message_workq");
	INIT_DELAYED_WORK(&pthread_info->msgq_work, mmrm_vm_fe_msgq_msg_handler);

	pmsg_info->msgq_label = GH_MSGQ_LABEL_MMRM;
	pmsg_info->msgq_handle = gh_msgq_register(pmsg_info->msgq_label);
	d_mpr_h("%s: label:%d handle:%p\n", __func__,
		pmsg_info->msgq_label, pmsg_info->msgq_handle);

	if (IS_ERR(pmsg_info->msgq_handle)) {
		rc = PTR_ERR(pmsg_info->msgq_handle);
		d_mpr_e("msgq register failed rc:%d\n", rc);
		return rc;
	}

	pthread_info->msgq_listener_thread =
			kthread_create(mmrm_vm_msgq_listener, mmrm_vm, "mmrm_vm_fe");
	wake_up_process(pthread_info->msgq_listener_thread);

	d_mpr_e("%s:  msgq_handle=%p\n", __func__, pmsg_info->msgq_handle);

	return rc;
}

int mmrm_vm_msgq_deinit(struct mmrm_vm_driver_data *mmrm_vm)
{
	struct mmrm_vm_gh_msgq_info *pmsg_info;
	struct mmrm_vm_thread_info *pthread_info;

	if (IS_ERR_OR_NULL(mmrm_vm))
		return -EINVAL;

	pmsg_info = &mmrm_vm->msg_info;
	pthread_info = &mmrm_vm->thread_info;

	if (pthread_info->msgq_listener_thread)
		kthread_stop(pthread_info->msgq_listener_thread);

	if (pmsg_info->msgq_handle)
		gh_msgq_unregister(pmsg_info->msgq_handle);
	return 0;
}
