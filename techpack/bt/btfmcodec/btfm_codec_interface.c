// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include "btfm_codec.h"
#include "btfm_codec_interface.h"
#include "btfm_codec_pkt.h"
#include "btfm_codec_btadv_interface.h"

static struct snd_soc_dai_driver *btfmcodec_dai_info;
uint32_t bits_per_second;

static int btfm_codec_get_mixer_control(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = kcontrol->private_data;
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(codec);
	struct hwep_data *hwepinfo = btfmcodec->hwep_info;
	struct btfmcodec_state_machine states = btfmcodec->states;
	struct snd_kcontrol_new *mixer_ctrl = hwepinfo->mixer_ctrl;
	struct snd_ctl_elem_id id = kcontrol->id;
	int num_mixer_ctrl = hwepinfo->num_mixer_ctrl;
	int i = 0;

	BTFMCODEC_DBG("");
	if (states.current_state != IDLE) {
		BTFMCODEC_WARN("Received probe when state is :%s",
			coverttostring(states.current_state));
	} else {
		for (; i < num_mixer_ctrl ; i++) {
			BTFMCODEC_DBG("checking mixer_ctrl:%s and current mixer:%s",
				id.name, mixer_ctrl[i].name);
			if (!strncmp(id.name, mixer_ctrl[i].name, 64)) {
				BTFMCODEC_DBG("Matched");
				mixer_ctrl[i].get(kcontrol, ucontrol);
				break;
			}
		}
		if (num_mixer_ctrl == i)
			return 0;
	}
	return 1;
}


static int btfmcodec_put_mixer_control(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = kcontrol->private_data;
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(codec);
	struct hwep_data *hwepinfo = btfmcodec->hwep_info;
	struct btfmcodec_state_machine states = btfmcodec->states;
	struct snd_kcontrol_new *mixer_ctrl = hwepinfo->mixer_ctrl;
	struct snd_ctl_elem_id id = kcontrol->id;
	int num_mixer_ctrl = hwepinfo->num_mixer_ctrl;
	int i = 0;

	BTFMCODEC_DBG("");
	if (states.current_state != IDLE) {
		BTFMCODEC_WARN("Received probe when state is :%s",
			coverttostring(states.current_state));
	} else {
		for (; i < num_mixer_ctrl ; i++) {
			BTFMCODEC_DBG("checking mixer_ctrl:%s and current mixer:%s",
			       id.name, mixer_ctrl[i].name);
			if (!strncmp(id.name, mixer_ctrl[i].name, 64)) {
				BTFMCODEC_DBG("Matched");
				mixer_ctrl[i].put(kcontrol, ucontrol);
				break;
			}
		}
		if (num_mixer_ctrl == i)
			return 0;
	}
	return 1;
}

static int btfmcodec_codec_probe(struct snd_soc_component *codec)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(codec);
	struct btfmcodec_state_machine *state = &btfmcodec->states;
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	int num_mixer_ctrl = hwep_info->num_mixer_ctrl;
	BTFMCODEC_DBG("");

	// ToDo: check weather probe has to allowed when state if different
	if (btfmcodec_get_current_transport(state)!= IDLE) {
		BTFMCODEC_WARN("Received probe when state is :%s",
			coverttostring(btfmcodec_get_current_transport(state)));
	} else if (hwep_info->drv && hwep_info->drv->hwep_probe) {
		hwep_info->drv->hwep_probe(codec);
		/* Register mixer control */
		if (hwep_info->mixer_ctrl && num_mixer_ctrl >= 1) {
			struct snd_kcontrol_new *mixer_ctrl;
			int i = 0;
			mixer_ctrl = (struct snd_kcontrol_new *)
				      kzalloc(num_mixer_ctrl *
				      sizeof(struct snd_kcontrol_new), GFP_KERNEL);
			if (!mixer_ctrl) {
				BTFMCODEC_ERR("failed to register mixer controls");
				goto end;
			}

			BTFMCODEC_INFO("Registering %d mixer controls", num_mixer_ctrl);
			memcpy(mixer_ctrl, hwep_info->mixer_ctrl, num_mixer_ctrl * sizeof(struct snd_kcontrol_new));
			for (; i< num_mixer_ctrl; i++) {
				BTFMCODEC_INFO("name of control:%s", mixer_ctrl[i].name);
				mixer_ctrl[i].get = btfm_codec_get_mixer_control;
			        mixer_ctrl[i].put = btfmcodec_put_mixer_control;
			}
			snd_soc_add_component_controls(codec, mixer_ctrl, num_mixer_ctrl);
			BTFMCODEC_INFO("CODEC address while registering mixer ctrl:%p", codec);
		}
	}

end:
	// ToDo to add mixer control.
	return 0;
}

static void btfmcodec_codec_remove(struct snd_soc_component *codec)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(codec);
	struct btfmcodec_state_machine *state = &btfmcodec->states;
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	BTFMCODEC_DBG("");

	// ToDo: check whether remove has to allowed when state if different
	if (btfmcodec_get_current_transport(state)!= IDLE) {
		BTFMCODEC_WARN("Received probe when state is :%s",
			coverttostring(btfmcodec_get_current_transport(state)));
	} else if (hwep_info->drv && hwep_info->drv->hwep_remove) {
		hwep_info->drv->hwep_remove(codec);
	}
}

static int btfmcodec_codec_write(struct snd_soc_component *codec,
			unsigned int reg, unsigned int value)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(codec);
	struct btfmcodec_state_machine *state = &btfmcodec->states;
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	BTFMCODEC_DBG("");

	// ToDo: check whether write has to allowed when state if different
	if (btfmcodec_get_current_transport(state)!= IDLE) {
		BTFMCODEC_WARN("Received probe when state is :%s",
			coverttostring(btfmcodec_get_current_transport(state)));
	} else if (hwep_info->drv && hwep_info->drv->hwep_remove) {
		return hwep_info->drv->hwep_write(codec, reg, value);
	}

	return 0;
}

static unsigned int btfmcodec_codec_read(struct snd_soc_component *codec,
				unsigned int reg)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(codec);
	struct btfmcodec_state_machine *state = &btfmcodec->states;
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	BTFMCODEC_DBG("");

	// ToDo: check whether read has to allowed when state if different
	if (btfmcodec_get_current_transport(state)!= IDLE) {
		BTFMCODEC_WARN("Received probe when state is :%s",
			coverttostring(btfmcodec_get_current_transport(state)));
	} else if (hwep_info->drv && hwep_info->drv->hwep_read) {
		return hwep_info->drv->hwep_read(codec, reg);
	}

	return 0;
}

static const struct snd_soc_component_driver btfmcodec_codec_component = {
	.probe	= btfmcodec_codec_probe,
	.remove	= btfmcodec_codec_remove,
	.read	= btfmcodec_codec_read,
	.write	= btfmcodec_codec_write,
};


static inline void * btfmcodec_get_dai_drvdata(struct hwep_data *hwep_info)
{
	if (!hwep_info || !hwep_info->dai_drv) return NULL;
	return hwep_info->dai_drv;
}

int btfmcodec_hwep_startup(struct btfmcodec_data *btfmcodec)
{
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	struct hwep_dai_driver *dai_drv = (struct hwep_dai_driver *)
					      btfmcodec_get_dai_drvdata(hwep_info);

	if (dai_drv && dai_drv->dai_ops && dai_drv->dai_ops->hwep_startup) {
		return dai_drv->dai_ops->hwep_startup((void *)btfmcodec->hwep_info);
	} else {
		return -1;
	}
}

static int btfmcodec_dai_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(dai->component);
	struct btfmcodec_state_machine *state = &btfmcodec->states;

	BTFMCODEC_DBG("substream = %s  stream = %d dai->name = %s",
		 substream->name, substream->stream, dai->name);

	if (btfmcodec_get_current_transport(state) != IDLE &&
		btfmcodec_get_current_transport(state) != BT_Connected) {
		BTFMCODEC_DBG("Not allowing as state is:%s",
			coverttostring(btfmcodec_get_current_transport(state)));
	} else {
		return btfmcodec_hwep_startup(btfmcodec);
	}

	return 0;
}

int btfmcodec_hwep_shutdown(struct btfmcodec_data *btfmcodec, int id,
			    bool disable_master)
{
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	struct btfmcodec_char_device *btfmcodec_dev = btfmcodec->btfmcodec_dev;
	struct hwep_dai_driver *dai_drv = (struct hwep_dai_driver *)
					      btfmcodec_get_dai_drvdata(hwep_info);
	struct btfmcodec_state_machine *state = &btfmcodec->states;
	struct btm_master_shutdown_req shutdown_req;
	wait_queue_head_t *rsp_wait_q =
			&btfmcodec_dev->rsp_wait_q[BTM_PKT_TYPE_MASTER_SHUTDOWN_RSP];
	uint8_t *status = &btfmcodec_dev->status[BTM_PKT_TYPE_MASTER_SHUTDOWN_RSP];
	int ret = 0;

	/* for master configurations failure cases, we don't need to send
	 * shutdown request
	 */
	if (btfmcodec_get_current_transport(state) == BT_Connected && disable_master) {
		BTFMCODEC_DBG("sending master shutdown request..");
		shutdown_req.opcode = BTM_BTFMCODEC_MASTER_SHUTDOWN_REQ;
		shutdown_req.len = BTM_MASTER_SHUTDOWN_REQ_LEN;
		shutdown_req.stream_id = id;
		/* See if we need to protect below with lock */
		*status = BTM_WAITING_RSP;
		btfmcodec_dev_enqueue_pkt(btfmcodec_dev, &shutdown_req, (shutdown_req.len +
					BTM_HEADER_LEN));

		ret = wait_event_interruptible_timeout(*rsp_wait_q,
			(*status) != BTM_WAITING_RSP,
			msecs_to_jiffies(BTM_MASTER_CONFIG_RSP_TIMEOUT));
		if (ret == 0) {
			BTFMCODEC_ERR("failed to recevie response from BTADV audio Manager");
		} else {
			if (*status == BTM_RSP_RECV)
				ret = 0;
			else if (*status == BTM_FAIL_RESP_RECV ||
				 *status == BTM_RSP_NOT_RECV_CLIENT_KILLED)
				ret = -1;
		}
	} else {
		if (!disable_master)
		BTFMCODEC_WARN("Not sending master shutdown request as graph might have closed");
		else	
		BTFMCODEC_WARN("Not sending master shutdown request as state is:%s",
			coverttostring(btfmcodec_get_current_transport(state)));
	}

	if (dai_drv && dai_drv->dai_ops && dai_drv->dai_ops->hwep_shutdown) {
		dai_drv->dai_ops->hwep_shutdown((void *)btfmcodec->hwep_info, id);
	}

	return ret;
}

void btfmcodec_wq_hwep_shutdown(struct work_struct *work)
{
	struct btfmcodec_char_device *btfmcodec_dev = container_of(work,
						struct btfmcodec_char_device,
						wq_hwep_shutdown);
	struct btfmcodec_data *btfmcodec = (struct btfmcodec_data *)btfmcodec_dev->btfmcodec;
	struct list_head *head = &btfmcodec->config_head;
	struct hwep_configurations *hwep_configs = NULL;
	int ret = -1;
	int idx = BTM_PKT_TYPE_HWEP_SHUTDOWN;

	BTFMCODEC_INFO(" starting shutdown");
	/* Just check if first Rx has to be closed first or
	 * any order should be ok.
	 */
	list_for_each_entry(hwep_configs, head, dai_list) {
		BTFMCODEC_INFO("shuting down dai id:%d", hwep_configs->stream_id);
		ret = btfmcodec_hwep_shutdown(btfmcodec, hwep_configs->stream_id, true);
		if (ret < 0) {
			BTFMCODEC_ERR("failed to shutdown master with id", hwep_configs->stream_id);
			break;
		}
	}

	if (ret < 0)
		btfmcodec_dev->status[idx] = BTM_FAIL_RESP_RECV;
	else
		btfmcodec_dev->status[idx] = BTM_RSP_RECV;

	wake_up_interruptible(&btfmcodec_dev->rsp_wait_q[idx]);
}

static int btfmcodec_delete_configs(struct btfmcodec_data *btfmcodec, uint8_t id)
{
	struct list_head *head = &btfmcodec->config_head;
	struct hwep_configurations *hwep_configs;
	int ret = -1;

	list_for_each_entry(hwep_configs, head, dai_list) {
		if (hwep_configs->stream_id == id) {
			BTFMCODEC_INFO("deleting configs with id %d", id);
			list_del(&hwep_configs->dai_list);
			ret = 1;
			break;
		}
	}

	return ret;
}

static void btfmcodec_dai_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(dai->component);
	struct btfmcodec_state_machine *state = &btfmcodec->states;

	BTFMCODEC_DBG("dai->name: %s, dai->id: %d, dai->rate: %d", dai->name,
		dai->id, dai->rate);

	if (btfmcodec_get_current_transport(state) != IDLE &&
	    btfmcodec_get_current_transport(state) != BT_Connected) {
		BTFMCODEC_WARN("not allowing shutdown as state is:%s",
			coverttostring(btfmcodec_get_current_transport(state)));
		/* Delete stored configs */
		btfmcodec_delete_configs(btfmcodec, dai->id);
	} else {
		/* first master shutdown has to done */
		btfmcodec_hwep_shutdown(btfmcodec, dai->id, false);
		btfmcodec_delete_configs(btfmcodec, dai->id);
		if (!btfmcodec_is_valid_cache_avb(btfmcodec))
			btfmcodec_set_current_state(state, IDLE);
		else {
			BTFMCODEC_WARN("valid stream id is available not updating state\n");
			btfmcodec_set_current_state(state, BT_Connected);
		}
	}
}

int btfmcodec_hwep_hw_params (struct btfmcodec_data *btfmcodec, uint32_t bps,
			   uint32_t direction)
{
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	struct hwep_dai_driver *dai_drv = (struct hwep_dai_driver *)
					      btfmcodec_get_dai_drvdata(hwep_info);

	if (dai_drv && dai_drv->dai_ops && dai_drv->dai_ops->hwep_hw_params) {
		return dai_drv->dai_ops->hwep_hw_params((void *)btfmcodec->hwep_info,
							bps, direction);
	} else {
		return -1;
	}
}

static int btfmcodec_dai_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(dai->component);
	struct btfmcodec_state_machine *state = &btfmcodec->states;
	uint32_t bps = params_width(params);
	uint32_t direction = substream->stream;

	BTFMCODEC_DBG("dai->name = %s DAI-ID %x rate %d bps %d num_ch %d",
		dai->name, dai->id, params_rate(params), params_width(params),
		params_channels(params));

	if (btfmcodec_get_current_transport(state) != IDLE &&
		btfmcodec_get_current_transport(state) != BT_Connected) {
		BTFMCODEC_WARN("caching bps as state is :%s",
			coverttostring(btfmcodec_get_current_transport(state)));
		bits_per_second = bps;
	} else {
		return btfmcodec_hwep_hw_params(btfmcodec, bps, direction);
	}

	return 0;
}

bool btfmcodec_is_valid_cache_avb(struct btfmcodec_data *btfmcodec)
{
	struct list_head *head = &btfmcodec->config_head;
	struct hwep_configurations *hwep_configs;
	bool cache_avb = false;

	list_for_each_entry(hwep_configs, head, dai_list) {
		cache_avb = true;
		break;
	}

	return cache_avb;
}

static int btfmcodec_check_and_cache_configs(struct btfmcodec_data *btfmcodec,
				   uint32_t sampling_rate, uint32_t direction,
				   int id, uint8_t codectype)
{
	struct list_head *head = &btfmcodec->config_head;
	struct hwep_configurations *hwep_configs;

	list_for_each_entry(hwep_configs, head, dai_list) {
		if (hwep_configs->stream_id == id) {
			BTFMCODEC_WARN("previous entry for %d is already available",
				id);
			list_del(&hwep_configs->dai_list);
		}
	}

	hwep_configs = kzalloc(sizeof(struct hwep_configurations),
				GFP_KERNEL);
	if (!hwep_configs) {
		BTFMCODEC_ERR("failed to allocate memory");
		return -ENOMEM;
	}

	hwep_configs->stream_id = id; /* Stream identifier */
	hwep_configs->sample_rate = sampling_rate;
	hwep_configs->bit_width = bits_per_second;
	hwep_configs->codectype = codectype;
	hwep_configs->direction = direction;

	list_add(&hwep_configs->dai_list, head);
	BTFMCODEC_INFO("added dai id:%d to list with sampling_rate :%u, direction:%u", id, sampling_rate, direction);
	return 1;
}

static int btfmcodec_configure_master(struct btfmcodec_data *btfmcodec, uint8_t id)
{
	struct btfmcodec_char_device *btfmcodec_dev = btfmcodec->btfmcodec_dev;
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	struct master_hwep_configurations hwep_configs;
	struct btm_master_config_req config_reg;
	struct hwep_dai_driver *dai_drv = (struct hwep_dai_driver *)
					      btfmcodec_get_dai_drvdata(hwep_info);
	wait_queue_head_t *rsp_wait_q =
		&btfmcodec_dev->rsp_wait_q[BTM_PKT_TYPE_MASTER_CONFIG_RSP];
	uint8_t *status = &btfmcodec_dev->status[BTM_PKT_TYPE_MASTER_CONFIG_RSP];
	int ret = 0;

	if (dai_drv && dai_drv->dai_ops && dai_drv->dai_ops->hwep_get_configs) {
		dai_drv->dai_ops->hwep_get_configs((void *)btfmcodec->hwep_info,
						   &hwep_configs, id);
	} else {
		BTFMCODEC_ERR("No hwep_get_configs is set by hw ep driver");
		return -1;
	}

	BTFMCODEC_INFO("framing packet for %d", id);
	config_reg.opcode = BTM_BTFMCODEC_MASTER_CONFIG_REQ;
	config_reg.len = BTM_MASTER_CONFIG_REQ_LEN;
	config_reg.stream_id = hwep_configs.stream_id;
	config_reg.device_id = hwep_configs.device_id;
	config_reg.sample_rate = hwep_configs.sample_rate;
	config_reg.bit_width = hwep_configs.bit_width;
	config_reg.num_channels = hwep_configs.num_channels;
	config_reg.channel_num = hwep_configs.chan_num;
	config_reg.codec_id = hwep_configs.codectype;
	BTFMCODEC_DBG("================================================\n");
	BTFMCODEC_DBG("config_reg.len :%d", config_reg.len);
	BTFMCODEC_DBG("config_reg.stream_id :%d", config_reg.stream_id);
	BTFMCODEC_DBG("config_reg.device_id :%d", config_reg.device_id);
	BTFMCODEC_DBG("config_reg.sample_rate :%d", config_reg.sample_rate);
	BTFMCODEC_DBG("config_reg.bit_width :%d", config_reg.bit_width);
	BTFMCODEC_DBG("config_reg.num_channels :%d", config_reg.num_channels);
	BTFMCODEC_DBG("config_reg.channel_num :%d", config_reg.channel_num);
	BTFMCODEC_DBG("config_reg.codec_id :%d", config_reg.codec_id);
	BTFMCODEC_DBG("================================================\n");
	/* See if we need to protect below with lock */
	*status = BTM_WAITING_RSP;
	btfmcodec_dev_enqueue_pkt(btfmcodec_dev, &config_reg, (config_reg.len +
				BTM_HEADER_LEN));
	ret = wait_event_interruptible_timeout(*rsp_wait_q,
		(*status) != BTM_WAITING_RSP,
		msecs_to_jiffies(BTM_MASTER_CONFIG_RSP_TIMEOUT));
	if (ret == 0) {
		BTFMCODEC_ERR("failed to recevie response from BTADV audio Manager");
		ret = -ETIMEDOUT;
	} else {
		if (*status == BTM_RSP_RECV)
			return 0;
		else if (*status == BTM_FAIL_RESP_RECV ||
			 *status == BTM_RSP_NOT_RECV_CLIENT_KILLED)
			return -1;
	}

	return ret;
}

int btfmcodec_hwep_prepare(struct btfmcodec_data *btfmcodec, uint32_t sampling_rate,
			uint32_t direction, int id)
{
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	struct hwep_dai_driver *dai_drv = (struct hwep_dai_driver *)
					      btfmcodec_get_dai_drvdata(hwep_info);
	struct btfmcodec_state_machine *state = &btfmcodec->states;

	int ret;
	if (dai_drv && dai_drv->dai_ops && dai_drv->dai_ops->hwep_prepare) {
		ret = dai_drv->dai_ops->hwep_prepare((void *)hwep_info, sampling_rate,
						      direction, id);
		if (ret == 0 && test_bit(BTADV_AUDIO_MASTER_CONFIG, &hwep_info->flags)) {
			ret = btfmcodec_configure_master(btfmcodec, (uint8_t)id);
			if (ret < 0) {
				BTFMCODEC_ERR("failed to configure master error %d", ret);
				/* close slave port and reset the state*/
				btfmcodec_set_current_state(state, IDLE);
				/* we don't need to do shutdown, ASOC is doing it */
//				btfmcodec_hwep_shutdown(btfmcodec, id);
			} else {
				btfmcodec_set_current_state(state, BT_Connected);
			}
		}
	} else {
		return -1;
	}

	return ret;
}

static int btfmcodec_dai_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(dai->component);
	struct btfmcodec_state_machine *state = &btfmcodec->states;
	struct hwep_data *hwep_info = btfmcodec->hwep_info;
	struct hwep_dai_driver *dai_drv = (struct hwep_dai_driver *)
					      btfmcodec_get_dai_drvdata(hwep_info);
	uint8_t *codectype = dai_drv->dai_ops->hwep_codectype;
	uint32_t sampling_rate = dai->rate;
	uint32_t direction = substream->stream;
	int id = dai->id;
	int ret;

	BTFMCODEC_INFO("dai->name: %s, dai->id: %d, dai->rate: %d direction: %d",
		dai->name, id, sampling_rate, direction);

	if (btfmcodec_get_current_transport(state) != IDLE &&
	    btfmcodec_get_current_transport(state) != BT_Connected) {
		BTFMCODEC_WARN("caching required info as state is:%s",
			coverttostring(btfmcodec_get_current_transport(state)));
		ret = btfmcodec_check_and_cache_configs(btfmcodec, sampling_rate, direction,
					      id, *codectype);
	} else {
	        ret = btfmcodec_hwep_prepare(btfmcodec, sampling_rate, direction, id);
		if (ret >= 0) {
			btfmcodec_check_and_cache_configs(btfmcodec,  sampling_rate, direction,
						id, *codectype);
		}
	}

	return ret;
}

int btfmcodec_hwep_set_channel_map(void *hwep_info, unsigned int tx_num,
				unsigned int *tx_slot, unsigned int rx_num,
				unsigned int *rx_slot)
{
	struct hwep_dai_driver *dai_drv = (struct hwep_dai_driver *)
					      btfmcodec_get_dai_drvdata(hwep_info);

	if (dai_drv && dai_drv->dai_ops && dai_drv->dai_ops->hwep_set_channel_map) {
		return dai_drv->dai_ops->hwep_set_channel_map(hwep_info, tx_num,
							      tx_slot, rx_num,
							      rx_slot);
	} else {
		return -1;
	}

}

static int btfmcodec_dai_set_channel_map(struct snd_soc_dai *dai,
				unsigned int tx_num, unsigned int *tx_slot,
				unsigned int rx_num, unsigned int *rx_slot)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(dai->component);
	struct btfmcodec_state_machine states = btfmcodec->states;

	BTFMCODEC_DBG("");
	// ToDo: check whether hw_params has to allowed when state if different
	if (states.current_state != IDLE) {
		BTFMCODEC_WARN("Received probe when state is :%s", coverttostring(states.current_state));
	} else {
		return btfmcodec_hwep_set_channel_map((void *)btfmcodec->hwep_info, tx_num,
						   tx_slot, rx_num, rx_slot);
	}

	return 0;
}


int btfmcodec_hwep_get_channel_map(void *hwep_info, unsigned int *tx_num,
				unsigned int *tx_slot, unsigned int *rx_num,
				unsigned int *rx_slot, int id)
{
	struct hwep_dai_driver *dai_drv = (struct hwep_dai_driver *)
					      btfmcodec_get_dai_drvdata(hwep_info);

	if (dai_drv && dai_drv->dai_ops && dai_drv->dai_ops->hwep_get_channel_map) {
		return dai_drv->dai_ops->hwep_get_channel_map(hwep_info, tx_num,
							      tx_slot, rx_num,
							      rx_slot, id);
	} else {
		return -1;
	}
}

static int btfmcodec_dai_get_channel_map(struct snd_soc_dai *dai,
				 unsigned int *tx_num, unsigned int *tx_slot,
				 unsigned int *rx_num, unsigned int *rx_slot)
{
	struct btfmcodec_data *btfmcodec = snd_soc_component_get_drvdata(dai->component);
//	struct btfmcodec_state_machine states = btfmcodec->states;

	BTFMCODEC_DBG("");
	// ToDo: get_channel_map is not needed for new driver
/*	if (states.current_state != IDLE) {
		BTFMCODEC_WARN("Received probe when state is :%s", coverttostring(states.current_state));
	} else {
*/		return btfmcodec_hwep_get_channel_map((void *)btfmcodec->hwep_info,
						   tx_num, tx_slot, rx_num,
						   rx_slot, dai->id);
//	}

	return 0;
}

void btfmcodec_wq_hwep_configure(struct work_struct *work)
{
	struct btfmcodec_char_device *btfmcodec_dev = container_of(work,
						struct btfmcodec_char_device,
						wq_hwep_configure);
	struct btfmcodec_data *btfmcodec = (struct btfmcodec_data *)btfmcodec_dev->btfmcodec;
	struct list_head *head = &btfmcodec->config_head;
	struct hwep_configurations *hwep_configs = NULL;
	int ret;
	int idx = BTM_PKT_TYPE_HWEP_CONFIG;
	uint32_t sample_rate, direction;
	uint8_t id, bit_width, codectype;

	list_for_each_entry(hwep_configs, head, dai_list) {
		id = hwep_configs->stream_id;
		sample_rate = hwep_configs->sample_rate;
		bit_width = hwep_configs->bit_width;
		codectype = hwep_configs->codectype;
		direction = hwep_configs->direction;

		BTFMCODEC_INFO("configuring dai id:%d with sampling rate:%d bit_width:%d", id, sample_rate, bit_width);
		ret = btfmcodec_hwep_startup(btfmcodec);
		if (ret >= 0)
			ret = btfmcodec_hwep_hw_params(btfmcodec, bit_width, direction);
		if (ret >= 0)
			ret = btfmcodec_hwep_prepare(btfmcodec, sample_rate, direction, id);
		if (ret < 0) {
			BTFMCODEC_ERR("failed to configure hwep", hwep_configs->stream_id);
			break;
		}
	}

	if (ret < 0)
		btfmcodec_dev->status[idx] = BTM_FAIL_RESP_RECV;
	else
		btfmcodec_dev->status[idx] = BTM_RSP_RECV;

	wake_up_interruptible(&btfmcodec_dev->rsp_wait_q[idx]);
}
static struct snd_soc_dai_ops btfmcodec_dai_ops = {
	.startup = btfmcodec_dai_startup,
	.shutdown = btfmcodec_dai_shutdown,
	.hw_params = btfmcodec_dai_hw_params,
	.prepare = btfmcodec_dai_prepare,
	.set_channel_map = btfmcodec_dai_set_channel_map,
	.get_channel_map = btfmcodec_dai_get_channel_map,
};

int btfm_register_codec(struct hwep_data *hwep_info)
{
	struct btfmcodec_data *btfmcodec;
	struct btfmcodec_char_device *btfmcodec_dev;
	struct device *dev;
	struct hwep_dai_driver *dai_drv;
	int i, ret;

	btfmcodec = btfm_get_btfmcodec();
	btfmcodec_dev = btfmcodec->btfmcodec_dev;
	dev = &btfmcodec->dev;
	btfmcodec_dai_info = kzalloc((sizeof(struct snd_soc_dai_driver) * hwep_info->num_dai), GFP_KERNEL);
	if (!btfmcodec_dai_info) {
		BTFMCODEC_ERR("failed to allocate memory");
		return -ENOMEM;
	}

	for (i = 0; i < hwep_info->num_dai; i++) {
		dai_drv = &hwep_info->dai_drv[i];
		btfmcodec_dai_info[i].name = dai_drv->dai_name;
		btfmcodec_dai_info[i].id = dai_drv->id;
		btfmcodec_dai_info[i].capture = dai_drv->capture;
		btfmcodec_dai_info[i].playback = dai_drv->playback;
		btfmcodec_dai_info[i].ops = &btfmcodec_dai_ops;
	}

	BTFMCODEC_INFO("Adding %d dai support to codec", hwep_info->num_dai);
	BTFMCODEC_INFO("slim bus driver name:%s", dev->driver->name);
	ret = snd_soc_register_component(dev, &btfmcodec_codec_component,
					btfmcodec_dai_info, hwep_info->num_dai);
	BTFMCODEC_INFO("Dev node address: %p", dev);
	BTFMCODEC_INFO("btfmcodec address :%p, btfmcodec");
	BTFMCODEC_INFO("HWEPINFO address:%p", hwep_info);
	BTFMCODEC_INFO("btfmcodec_dev INFO address:%p", btfmcodec->btfmcodec_dev);
	BTFMCODEC_INFO("before wq_hwep_shutdown:%p", btfmcodec_dev->wq_hwep_shutdown);
	BTFMCODEC_INFO("before wq_prepare_bearer:%p", btfmcodec_dev->wq_prepare_bearer);
	INIT_WORK(&btfmcodec_dev->wq_hwep_shutdown, btfmcodec_wq_hwep_shutdown);
	INIT_WORK(&btfmcodec_dev->wq_prepare_bearer, btfmcodec_wq_prepare_bearer);
	INIT_WORK(&btfmcodec_dev->wq_hwep_configure, btfmcodec_wq_hwep_configure);
	BTFMCODEC_INFO("after wq_hwep_shutdown:%p", btfmcodec_dev->wq_hwep_shutdown);
	BTFMCODEC_INFO("after wq_prepare_bearer:%p", btfmcodec_dev->wq_prepare_bearer);
	BTFMCODEC_INFO("btfmcodec_wq_prepare_bearer:%p", btfmcodec_wq_prepare_bearer);
	BTFMCODEC_INFO("btfmcodec_wq_hwep_shutdown:%p", btfmcodec_wq_hwep_shutdown);

	return ret;
}

void btfm_unregister_codec(void)
{
	struct btfmcodec_data *btfmcodec;

	btfmcodec = btfm_get_btfmcodec();
	snd_soc_unregister_component(&btfmcodec->dev);
}
