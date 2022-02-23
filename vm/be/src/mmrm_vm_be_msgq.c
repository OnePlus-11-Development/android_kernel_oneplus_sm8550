// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/gunyah/gh_msgq.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/workqueue.h>
#include <linux/list.h>

#include "mmrm_vm_be.h"
#include "mmrm_vm_interface.h"
#include "mmrm_vm_debug.h"

#define MAX_ERR_COUNT 5

/**
 * mmrm_vm_msgq_msg_handler - fe request handler
 * work: work parameter that workqueue thread transfer
 */
static void mmrm_vm_msgq_msg_handler(struct work_struct *work)
{
	struct mmrm_vm_thread_info *pthread_info =
		container_of(work, struct mmrm_vm_thread_info, msgq_work.work);
	struct mmrm_vm_driver_data *mmrm_vm =
		container_of(pthread_info, struct mmrm_vm_driver_data, thread_info);
	struct mmrm_vm_msg *msg;
	struct mmrm_vm_msg *next_msg;
	struct list_head   head;

	if (IS_ERR_OR_NULL(work))
		return;

	mutex_lock(&pthread_info->list_lock);
	list_replace_init(&pthread_info->queued_msg, &head);
	mutex_unlock(&pthread_info->list_lock);

	list_for_each_entry_safe(msg, next_msg, &head, link) {
		mmrm_vm_be_recv(mmrm_vm, msg->msg_buf, msg->msg_size);
		list_del(&msg->link);
		kfree(msg);
	}
}

/**
 * mmrm_vm_be_msgq_listener - gunyah's message receiving thread
 * data: parameter that caller transfer
 */
static int mmrm_vm_be_msgq_listener(void *data)
{
	struct mmrm_vm_driver_data *mmrm_vm;
	struct mmrm_vm_gh_msgq_info *pmsg_info;
	struct mmrm_vm_thread_info *thread_info;

	struct mmrm_vm_msg *msg;
	size_t size;
	int ret = 0;
	int err_count = 0;

	if (IS_ERR_OR_NULL(data))
		return -EINVAL;

	mmrm_vm = (struct mmrm_vm_driver_data *)data;
	pmsg_info = &mmrm_vm->msg_info;
	thread_info = &mmrm_vm->thread_info;

	while (true) {
		msg = kzalloc(sizeof(struct mmrm_vm_msg), GFP_KERNEL);
		if (!msg)
			return -ENOMEM;

		ret = gh_msgq_recv(pmsg_info->msgq_handle, msg->msg_buf,
				GH_MSGQ_MAX_MSG_SIZE_BYTES, &size, 0);
		if (ret < 0) {
			kfree(msg);
			d_mpr_e("gh_msgq_recv failed, rc=%d\n", ret);
			err_count++;
			if (err_count < MAX_ERR_COUNT)
				continue;

			return -EINVAL;
		}

		err_count = 0;
		msg->msg_size = size;

		mutex_lock(&thread_info->list_lock);
		list_add_tail(&thread_info->queued_msg, &msg->link);
		mutex_unlock(&thread_info->list_lock);

		queue_delayed_work(thread_info->msg_workq,
				 &thread_info->msgq_work, msecs_to_jiffies(0));
	}

	return 0;
}

/**
 * mmrm_vm_msgq_send - send response message by gunyah API
 * mmrm_vm: driver data
 * msg: message buffer pointer
 * msg_size: message size
 */
int mmrm_vm_msgq_send(struct mmrm_vm_driver_data *mmrm_vm, void *msg, size_t msg_size)
{
	if (!mmrm_vm->msg_info.msgq_handle) {
		d_mpr_e("Failed to send msg, invalid msgq handle\n");
		return -EINVAL;
	}

	if (msg_size > GH_MSGQ_MAX_MSG_SIZE_BYTES) {
		d_mpr_e("msg size unsupported for msgq: %ld > %d\n", msg_size,
				GH_MSGQ_MAX_MSG_SIZE_BYTES);
		return -E2BIG;
	}

	return gh_msgq_send(mmrm_vm->msg_info.msgq_handle, msg, msg_size, GH_MSGQ_TX_PUSH);
}

/**
 * mmrm_vm_be_gh_validate_register - check gunyah connection validation
 * msg_info: gunyah meesage info
 * vm_status_payload: gunyah notification message status info
 */
int mmrm_vm_be_gh_validate_register(struct mmrm_vm_gh_msgq_info *msg_info,
		struct gh_rm_notif_vm_status_payload *vm_status_payload)
{
	gh_vmid_t peer_vmid;
	gh_vmid_t self_vmid;
	int rc = -1;

	if (vm_status_payload->vm_status != GH_RM_VM_STATUS_READY)
		return rc;

	d_mpr_l("%s: status=%d\n", __func__, vm_status_payload->vm_status);
	if (gh_rm_get_vmid(msg_info->peer_id, &peer_vmid))
		return rc;

	if (gh_rm_get_vmid(GH_PRIMARY_VM, &self_vmid))
		return rc;

	if (peer_vmid != vm_status_payload->vmid)
		return NOTIFY_DONE;

	d_mpr_l("%s: vmid=%d peer_vmid=%d\n", __func__, vm_status_payload->vmid, peer_vmid);

	if (msg_info->msgq_handle) {
		return rc;
	}

	msg_info->msgq_handle = gh_msgq_register(msg_info->msgq_label);

	d_mpr_l("%s: label=%d\n", __func__, msg_info->msgq_label);
	rc = 0;

	if (IS_ERR_OR_NULL(msg_info->msgq_handle)) {
		rc = -1;
		d_mpr_e("%s: gunyah message queue registration failed\n", __func__);
	}

	return rc;
}

/**
 * mmrm_vm_be_msgq_cb - check gunyah connection validation
 * nb: gunyah notofier block info
 * cmd: gunyah notification status category info
 * data: user defined data pointer
 */
static int mmrm_vm_be_msgq_cb(struct notifier_block *nb, unsigned long cmd, void *data)
{
	struct gh_rm_notif_vm_status_payload *vm_status_payload;
	struct mmrm_vm_driver_data *mmrm_vm;
	struct mmrm_vm_gh_msgq_info *msg_info;
	struct  mmrm_vm_thread_info *thread_info;
	int rc;

	if (IS_ERR_OR_NULL(nb))
		return -EINVAL;

	msg_info = container_of(nb, struct mmrm_vm_gh_msgq_info, pvt_nb);
	mmrm_vm = container_of(msg_info, struct mmrm_vm_driver_data, msg_info);

	thread_info = &mmrm_vm->thread_info;
	if (cmd != GH_RM_NOTIF_VM_STATUS)
		return NOTIFY_DONE;

	/*
	 * check VM status, only GH_TRUSTED_VM notification activate
	 * GUNYAH message queue registering
	 */
	vm_status_payload = (struct gh_rm_notif_vm_status_payload *)data;
	rc = mmrm_vm_be_gh_validate_register(msg_info, vm_status_payload);
	if (rc != 0)
		d_mpr_e("%s: msgq registration failed: err:%d\n",
			__func__, PTR_ERR(msg_info->msgq_handle));

	thread_info->msgq_listener_thread = kthread_run(mmrm_vm_be_msgq_listener,
			(void *)mmrm_vm, "mmrm_msgq_listener");
	if (IS_ERR_OR_NULL(thread_info->msgq_listener_thread)) {
		return NOTIFY_DONE;
	};

	wake_up_process(thread_info->msgq_listener_thread);

	return NOTIFY_DONE;
}

/**
 * mmrm_vm_msgq_init - gunyah message queue initialization
 * mmrm_vm: driver data
 */
int mmrm_vm_msgq_init(struct mmrm_vm_driver_data *mmrm_vm)
{
	struct mmrm_vm_gh_msgq_info *msg_info;
	struct mmrm_vm_thread_info *thread_info;
	int rc;

	if (IS_ERR_OR_NULL(mmrm_vm))
		return -EINVAL;

	msg_info = &mmrm_vm->msg_info;
	thread_info = &mmrm_vm->thread_info;

	msg_info->msgq_label = GH_MSGQ_LABEL_MMRM;
	d_mpr_l("%s:  msgq-label=%d\n", __func__, msg_info->msgq_label);

	msg_info->peer_id = GH_TRUSTED_VM;
	msg_info->pvt_nb.notifier_call = mmrm_vm_be_msgq_cb;
	rc = gh_rm_register_notifier(&msg_info->pvt_nb);
	if (rc != 0)
		return -1;

	mutex_init(&thread_info->list_lock);
	INIT_LIST_HEAD(&thread_info->queued_msg);
	thread_info->msg_workq = create_singlethread_workqueue("vm_message_workq");
	if (IS_ERR_OR_NULL(thread_info->msg_workq)) {
		return -1;
	};
	INIT_DELAYED_WORK(&thread_info->msgq_work, mmrm_vm_msgq_msg_handler);

	return 0;
}

/**
 * mmrm_vm_msgq_init - gunyah message queue de-initialization
 * mmrm_vm: driver data
 */
int mmrm_vm_msgq_deinit(struct mmrm_vm_driver_data *mmrm_vm)
{
	struct mmrm_vm_gh_msgq_info *msg_info;
	struct mmrm_vm_thread_info *thread_info;
	int rc = 0;

	if (IS_ERR_OR_NULL(mmrm_vm))
		return -EINVAL;

	msg_info = &mmrm_vm->msg_info;
	thread_info = &mmrm_vm->thread_info;
	if (thread_info->msgq_listener_thread)
		kthread_stop(thread_info->msgq_listener_thread);

	gh_rm_unregister_notifier(&msg_info->pvt_nb);

	if (msg_info->msgq_handle) {
		rc = gh_msgq_unregister(msg_info->msgq_handle);
		if (rc != 0)
			d_mpr_e("%s: msgq unregistration failed: err:%d\n", __func__, rc);
	}
	return rc;
}
