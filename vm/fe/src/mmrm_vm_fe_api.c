// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/gunyah/gh_msgq.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/sysfs.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "mmrm_vm_fe.h"
#include "mmrm_vm_interface.h"
#include "mmrm_vm_msgq.h"

#define get_client_handle_2_id(client) (client->client_uid)

extern struct mmrm_vm_driver_data *drv_vm_fe;

#define MAX_TIMEOUT_MS 300

int mmrm_fe_append_work_list(struct mmrm_vm_msg_q *msg_q, int msg_sz)
{
	struct mmrm_vm_request_msg_pkt *msg_pkt = msg_q->m_req;
	struct mmrm_vm_fe_priv *fe_data = drv_vm_fe->vm_pvt_data;
	unsigned long waited_time_ms;

	init_completion(&msg_q->complete);
	mutex_lock(&fe_data->work_list_lock);
	list_add_tail(&msg_q->link, &fe_data->mmrm_work_list);
	mutex_unlock(&fe_data->work_list_lock);

	mutex_lock(&fe_data->msg_send_lock);
	msg_pkt->msg.hd.seq_no = fe_data->seq_no++;
	mutex_unlock(&fe_data->msg_send_lock);

	mmrm_vm_fe_request_send(drv_vm_fe, msg_pkt, msg_sz);

	waited_time_ms = wait_for_completion_timeout(&msg_q->complete,
		msecs_to_jiffies(MAX_TIMEOUT_MS));
	if (waited_time_ms >= MAX_TIMEOUT_MS)
		return -1;
	return 0;
}

struct mmrm_client *mmrm_client_register(struct mmrm_client_desc *desc)
{
	struct mmrm_vm_request_msg_pkt msg;
	struct mmrm_vm_response_msg_pkt resp_pkt;
	struct mmrm_vm_api_request_msg *api_msg = &msg.msg;
	struct mmrm_vm_register_request *reg_data = &api_msg->data.reg;
	size_t msg_size = sizeof(api_msg->hd) + sizeof(*reg_data);
	int rc = 0;

	struct mmrm_vm_msg_q msg_q;

	api_msg->hd.cmd_id = MMRM_VM_REQUEST_REGISTER;
	reg_data->client_type = desc->client_type;
	reg_data->priority = desc->priority;
	memcpy(&reg_data->desc, &desc->client_info.desc, sizeof(reg_data->desc));

	msg_q.m_req = &msg;
	msg_q.m_resp = &resp_pkt;
	rc = mmrm_fe_append_work_list(&msg_q, msg_size);
	if (rc != 0)
		return NULL;

	return mmrm_vm_fe_get_client(resp_pkt.msg.data.reg.client_id);
}
EXPORT_SYMBOL(mmrm_client_register);

int mmrm_client_deregister(struct mmrm_client *client)
{
	int rc = 0;
	struct mmrm_vm_request_msg_pkt msg;
	struct mmrm_vm_response_msg_pkt resp_pkt;
	struct mmrm_vm_api_request_msg *api_msg = &msg.msg;
	struct mmrm_vm_deregister_request *reg_data = &api_msg->data.dereg;
	size_t msg_size = sizeof(api_msg->hd) + sizeof(*reg_data);

	struct mmrm_vm_msg_q msg_q;

	api_msg->hd.cmd_id = MMRM_VM_REQUEST_DEREGISTER;
	reg_data->client_id = get_client_handle_2_id(client);

	msg_q.m_req = &msg;
	msg_q.m_resp = &resp_pkt;

	rc = mmrm_fe_append_work_list(&msg_q, msg_size);
	if (rc != 0)
		return rc;

	rc = resp_pkt.msg.data.dereg.ret_code;
	return rc;
}
EXPORT_SYMBOL(mmrm_client_deregister);

int mmrm_client_set_value(struct mmrm_client *client,
	struct mmrm_client_data *client_data, unsigned long val)
{
	int rc = 0;
	struct mmrm_vm_request_msg_pkt msg;
	struct mmrm_vm_response_msg_pkt resp_pkt;
	struct mmrm_vm_api_request_msg *api_msg = &msg.msg;
	struct mmrm_vm_setvalue_request *reg_data = &api_msg->data.setval;
	size_t msg_size = sizeof(api_msg->hd) + sizeof(*reg_data);

	struct mmrm_vm_msg_q msg_q;

	api_msg->hd.cmd_id = MMRM_VM_REQUEST_SETVALUE;
	reg_data->client_id = get_client_handle_2_id(client);
	reg_data->data.flags = client_data->flags;
	reg_data->data.num_hw_blocks = client_data->num_hw_blocks;
	reg_data->val = val;

	msg_q.m_req = &msg;
	msg_q.m_resp = &resp_pkt;

	rc = mmrm_fe_append_work_list(&msg_q, msg_size);
	if (rc != 0)
		return rc;

	rc = resp_pkt.msg.data.setval.val;

	return rc;
}
EXPORT_SYMBOL(mmrm_client_set_value);

int mmrm_client_set_value_in_range(struct mmrm_client *client,
	struct mmrm_client_data *client_data,
	struct mmrm_client_res_value *val)
{
	int rc = 0;
	struct mmrm_vm_request_msg_pkt msg;
	struct mmrm_vm_response_msg_pkt resp_pkt;
	struct mmrm_vm_api_request_msg *api_msg = &msg.msg;
	struct mmrm_vm_setvalue_inrange_request *reg_data = &api_msg->data.setval_range;
	size_t msg_size = sizeof(api_msg->hd) + sizeof(*reg_data);

	struct mmrm_vm_msg_q msg_q;

	api_msg->hd.cmd_id = MMRM_VM_REQUEST_SETVALUE_INRANGE;
	reg_data->client_id = get_client_handle_2_id(client);
	reg_data->data.flags = client_data->flags;
	reg_data->data.num_hw_blocks = client_data->num_hw_blocks;
	reg_data->val.cur = val->cur;
	reg_data->val.max = val->max;
	reg_data->val.min = val->min;

	msg_q.m_req = &msg;
	msg_q.m_resp = &resp_pkt;

	rc = mmrm_fe_append_work_list(&msg_q, msg_size);
	return rc;
}
EXPORT_SYMBOL(mmrm_client_set_value_in_range);


int mmrm_client_get_value(struct mmrm_client *client,
	struct mmrm_client_res_value *val)
{
	int rc = 0;
	struct mmrm_vm_request_msg_pkt msg;
	struct mmrm_vm_response_msg_pkt resp_pkt;
	struct mmrm_vm_api_request_msg *api_msg = &msg.msg;
	struct mmrm_vm_getvalue_request *reg_data = &api_msg->data.getval;
	size_t msg_size = sizeof(api_msg->hd) + sizeof(*reg_data);

	struct mmrm_vm_msg_q msg_q;

	api_msg->hd.cmd_id = MMRM_VM_REQUEST_GETVALUE;
	reg_data->client_id = get_client_handle_2_id(client);

	msg_q.m_req = &msg;
	msg_q.m_resp = &resp_pkt;

	rc = mmrm_fe_append_work_list(&msg_q, msg_size);

	if (rc == 0) {
		val->cur = resp_pkt.msg.data.getval.val.cur;
		val->max = resp_pkt.msg.data.getval.val.max;
		val->min = resp_pkt.msg.data.getval.val.min;
	}
	return rc;
}
EXPORT_SYMBOL(mmrm_client_get_value);
