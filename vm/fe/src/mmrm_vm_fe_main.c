// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/kthread.h>

#include <linux/module.h>
#include <linux/of.h>

#include "mmrm_vm_fe.h"
#include "mmrm_vm_msgq.h"
#include "mmrm_vm_interface.h"
#include "mmrm_vm_debug.h"

struct mmrm_vm_driver_data *drv_vm_fe = (void *) -EPROBE_DEFER;

ssize_t msgq_send_trigger_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct mmrm_vm_driver_data *priv = dev->driver_data;
	char send_buf[64] = "test msg";
	char recv_buf[64];
	int ret;
	bool flag;
	size_t recv_size;

	ret = strtobool(buf, &flag);
	if (ret) {
		dev_err(dev, "invalid user input\n");
		return -1;
	}
	if (flag) {
		ret = gh_msgq_send(priv->msg_info.msgq_handle, send_buf, sizeof(send_buf), 0);
		if (ret)
			dev_err(dev, "send msgq failed\n");
		else
			dev_info(dev, "send msgq success\n");
		ret = gh_msgq_recv(priv->msg_info.msgq_handle, recv_buf, sizeof(recv_buf),
			&recv_size, 0);
		if (ret)
			dev_err(dev, "recv msgq failed ret = %d\n", ret);
		else
			dev_info(dev, "recv msg: %s\n", recv_buf);
	}
	return ret ? ret : count;
}

static DEVICE_ATTR_WO(msgq_send_trigger);

static struct attribute *mmrm_vm_fe_fs_attrs[] = {
	&dev_attr_msgq_send_trigger.attr,
	NULL,
};

static struct attribute_group mmrm_vm_fe_fs_attrs_group = {
	.attrs = mmrm_vm_fe_fs_attrs,
};

static int mmrm_vm_fe_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mmrm_vm_fe_priv *fe_priv_data;
	int rc = 0;

	drv_vm_fe = devm_kzalloc(dev, sizeof(*drv_vm_fe), GFP_KERNEL);
	if (!drv_vm_fe)
		return -ENOMEM;

	fe_priv_data = devm_kzalloc(dev, sizeof(*fe_priv_data), GFP_KERNEL);
	if (!fe_priv_data) {
		rc = -ENOMEM;
		goto err_priv_data;
	}

	drv_vm_fe->vm_pvt_data = fe_priv_data;
	fe_priv_data->seq_no = 0;
	fe_priv_data->dev = dev;

	mutex_init(&fe_priv_data->msg_send_lock);
	dev_set_drvdata(&pdev->dev, drv_vm_fe);

	INIT_LIST_HEAD(&fe_priv_data->mmrm_work_list);
	mutex_init(&fe_priv_data->work_list_lock);

	mmrm_vm_fe_load_clk_rsrc(drv_vm_fe);
	mmrm_vm_msgq_init(drv_vm_fe);

	if (sysfs_create_group(&pdev->dev.kobj, &mmrm_vm_fe_fs_attrs_group)) {
		d_mpr_e("%s: failed to create sysfs\n",
			__func__);
	}

	dev_err(dev, "msgq probe success");
err_priv_data:

	return rc;
}

static int mmrm_vm_fe_driver_remove(struct platform_device *pdev)
{
	struct mmrm_vm_driver_data *mmrm_vm = dev_get_drvdata(&pdev->dev);

	mmrm_vm_msgq_deinit(mmrm_vm);
	return 0;
}

static const struct of_device_id mmrm_vm_fe_match[] = {
	{ .compatible = "qcom,mmrm-vm-fe" },
	{},
};
MODULE_DEVICE_TABLE(of, mmrm_vm_fe_match);

static struct platform_driver mmrm_vm_fe_driver = {
	.probe = mmrm_vm_fe_driver_probe,
	.driver = {
		.name = "mmrm-vm-fe",
		.of_match_table = mmrm_vm_fe_match,
	},
	.remove = mmrm_vm_fe_driver_remove,
};

static int __init mmrm_vm_fe_module_init(void)
{
	d_mpr_e("%s:  init start\n", __func__);

	return platform_driver_register(&mmrm_vm_fe_driver);
}
subsys_initcall(mmrm_vm_fe_module_init);

static void __exit mmrm_vm_fe_module_exit(void)
{
	platform_driver_unregister(&mmrm_vm_fe_driver);
}
module_exit(mmrm_vm_fe_module_exit);

MODULE_SOFTDEP("pre: gunyah_transport");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Test MSGQ Driver");
MODULE_LICENSE("GPL v2");
