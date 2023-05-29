// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/refcount.h>
#include <linux/idr.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/module.h>
#include "btfm_codec.h"
#include "btfm_codec_pkt.h"

#define dev_to_btfmcodec(_dev) container_of(_dev, struct btfmcodec_data, dev)

static DEFINE_IDR(dev_minor);
static struct class *dev_class;
static dev_t dev_major;
struct btfmcodec_data *btfmcodec;
struct device_driver driver = {.name = "btfmcodec-driver", .owner = THIS_MODULE};
struct btfmcodec_char_device *btfmcodec_dev;
#define cdev_to_btfmchardev(_cdev) container_of(_cdev, struct btfmcodec_char_device, cdev)
#define MIN_PKT_LEN  0x9

char *coverttostring(enum btfmcodec_states state) {
	switch (state) {
	case IDLE:
		return "IDLE";
		break;
	case BT_Connected:
		return "BT_CONNECTED";
		break;
	case BT_Connecting:
		return "BT_CONNECTING";
		break;
	case BTADV_AUDIO_Connected:
		return "BTADV_AUDIO_CONNECTED";
		break;
	case BTADV_AUDIO_Connecting:
		return "BTADV_AUDIO_CONNECTING";
		break;
	default:
		return "INVALID_STATE";
		break;
	}
}

/*
 * btfmcodec_dev_open() - open() syscall for the btfmcodec dev node
 * inode:	Pointer to the inode structure.
 * file:	Pointer to the file structure.
 *
 * This function is used to open the btfmcodec char device when
 * userspace client do a open() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 * Note: btfmcodec dev node works on nonblocking mode.
 */
static int btfmcodec_dev_open(struct inode *inode, struct file *file)
{
	struct btfmcodec_char_device *btfmcodec_dev = cdev_to_btfmchardev(inode->i_cdev);
	struct btfmcodec_data *btfmcodec = (struct btfmcodec_data *)btfmcodec_dev->btfmcodec;
	unsigned int active_clients = refcount_read(&btfmcodec_dev->active_clients);

	btfmcodec->states.current_state = IDLE; /* Just a temp*/
	btfmcodec->states.next_state = IDLE;
	BTFMCODEC_INFO("for %s by %s:%d active_clients[%d]\n",
		       btfmcodec_dev->dev_name, current->comm,
		       task_pid_nr(current), refcount_read(&btfmcodec_dev->active_clients));
	/* Don't allow a new client if already one is active. */
	if (active_clients > 1) {
		BTFMCODEC_WARN("%s: Not honoring open as other client is active", __func__);
		return EACCES;
	}
	/* for now have btfmcodec and later we can think of having it btfmcodec_dev */
	file->private_data = btfmcodec;
	refcount_inc(&btfmcodec_dev->active_clients);
	return 0;
}

/*
 * btfmcodec_pkt_release() - release operation on btfmcodec device
 * inode:	Pointer to the inode structure.
 * file:	Pointer to the file structure.
 *
 * This function is used to release the btfmcodec dev node when
 * userspace client do a close() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static int btfmcodec_dev_release(struct inode *inode, struct file *file)
{
	struct btfmcodec_char_device *btfmcodec_dev = cdev_to_btfmchardev(inode->i_cdev);
	unsigned long flags;
	int idx;

	BTFMCODEC_INFO("for %s by %s:%d active_clients[%u]\n",
		       btfmcodec_dev->dev_name, current->comm,
		       task_pid_nr(current), refcount_read(&btfmcodec_dev->active_clients));

	refcount_dec(&btfmcodec_dev->active_clients);
	if (refcount_read(&btfmcodec_dev->active_clients) == 1) {
		spin_lock_irqsave(&btfmcodec_dev->tx_queue_lock, flags);
		skb_queue_purge(&btfmcodec_dev->txq);
		/* Wakeup the device if waiting for the data */
		wake_up_interruptible(&btfmcodec_dev->readq);
		spin_unlock_irqrestore(&btfmcodec_dev->tx_queue_lock, flags);
		/* we need to have separte rx lock for below buff */
		skb_queue_purge(&btfmcodec_dev->rxq);
	}

	/* Notify waiting clients that client is closed or killed */
	for (idx = 0; idx < BTM_PKT_TYPE_MAX; idx++) {
		btfmcodec_dev->status[idx] = BTM_RSP_NOT_RECV_CLIENT_KILLED;
		wake_up_interruptible(&btfmcodec_dev->rsp_wait_q[idx]);
	}

	cancel_work_sync(&btfmcodec_dev->wq_hwep_shutdown);
	cancel_work_sync(&btfmcodec_dev->wq_hwep_configure);
	cancel_work_sync(&btfmcodec_dev->wq_prepare_bearer);

	btfmcodec->states.current_state = IDLE;
	btfmcodec->states.next_state = IDLE;
	return 0;
}

btm_opcode STREAM_TO_UINT32 (struct sk_buff *skb)
{
	return (skb->data[0] | (skb->data[1] << 8) |
		(skb->data[2] << 16) | (skb->data[3] << 24));
}

static void btfmcodec_dev_rxwork(struct work_struct *work)
{
	struct btfmcodec_char_device *btfmcodec_dev = container_of(work, struct btfmcodec_char_device, rx_work);
	struct sk_buff *skb;
	uint32_t len;
	uint8_t status;
	int idx;

	BTFMCODEC_DBG("start");
	while ((skb = skb_dequeue(&btfmcodec_dev->rxq))) {
		btm_opcode opcode = STREAM_TO_UINT32(skb);
		skb_pull(skb, sizeof(btm_opcode));
		len = STREAM_TO_UINT32(skb);
		skb_pull(skb, sizeof(len));
		switch (opcode) {
		case BTM_BTFMCODEC_PREPARE_AUDIO_BEARER_SWITCH_REQ:
			idx = BTM_PKT_TYPE_PREPARE_REQ;
			BTFMCODEC_DBG("BTM_BTFMCODEC_PREPARE_AUDIO_BEARER_SWITCH_REQ");
			if (len == BTM_PREPARE_AUDIO_BEARER_SWITCH_REQ_LEN) {
				/* there are chances where bearer indication is not recevied,
				 * So inform waiting thread to unblock itself and move to
				 * previous state.
				 */
				if (btfmcodec_dev->status[BTM_PKT_TYPE_BEARER_SWITCH_IND] == BTM_WAITING_RSP) {
				  BTFMCODEC_DBG("Notifying waiting beare indications");
				  btfmcodec_dev->status[BTM_PKT_TYPE_BEARER_SWITCH_IND] = BTM_FAIL_RESP_RECV;
				  wake_up_interruptible(&btfmcodec_dev->rsp_wait_q[BTM_PKT_TYPE_BEARER_SWITCH_IND]);
				}
				btfmcodec_dev->status[idx] = skb->data[0];
				queue_work(btfmcodec_dev->workqueue, &btfmcodec_dev->wq_prepare_bearer);
			} else {
				BTFMCODEC_ERR("wrong packet format with len:%d", len);
			}
			break;
		case BTM_BTFMCODEC_MASTER_CONFIG_RSP:
			idx = BTM_PKT_TYPE_MASTER_CONFIG_RSP;
			if (len == BTM_MASTER_CONFIG_RSP_LEN) {
				status = skb->data[1];
				if (status == MSG_SUCCESS)
				  btfmcodec_dev->status[idx] = BTM_RSP_RECV;
				else
				  btfmcodec_dev->status[idx] = BTM_FAIL_RESP_RECV;
			} else {
				BTFMCODEC_ERR("wrong packet format with len:%d", len);
				btfmcodec_dev->status[idx] = BTM_FAIL_RESP_RECV;
			}
			BTFMCODEC_INFO("Rx BTM_BTFMCODEC_MASTER_CONFIG_RSP status:%d",
				status);
			wake_up_interruptible(&btfmcodec_dev->rsp_wait_q[idx]);
			break;
		case BTM_BTFMCODEC_CTRL_MASTER_SHUTDOWN_RSP:
			idx = BTM_PKT_TYPE_MASTER_SHUTDOWN_RSP;
			if (len == BTM_MASTER_CONFIG_RSP_LEN) {
				status = skb->data[1];
				if (status == MSG_SUCCESS)
				  btfmcodec_dev->status[idx] = BTM_RSP_RECV;
				else
				  btfmcodec_dev->status[idx] = BTM_FAIL_RESP_RECV;
			} else {
				BTFMCODEC_ERR("wrong packet format with len:%d", len);
				btfmcodec_dev->status[idx] = BTM_FAIL_RESP_RECV;
			}
			BTFMCODEC_INFO("Rx BTM_BTFMCODEC_CTRL_MASTER_SHUTDOWN_RSP status:%d",
				status);
			wake_up_interruptible(&btfmcodec_dev->rsp_wait_q[idx]);
			break;
		case BTM_BTFMCODEC_BEARER_SWITCH_IND:
			idx = BTM_PKT_TYPE_BEARER_SWITCH_IND;
			if (len == BTM_BEARER_SWITCH_IND_LEN) {
				status = skb->data[0];
				if (status == MSG_SUCCESS)
				  btfmcodec_dev->status[idx] = BTM_RSP_RECV;
				else
				  btfmcodec_dev->status[idx] = BTM_FAIL_RESP_RECV;
			} else {
				BTFMCODEC_ERR("wrong packet format with len:%d", len);
				btfmcodec_dev->status[idx] = BTM_FAIL_RESP_RECV;
			}
			BTFMCODEC_INFO("Rx BTM_BTFMCODEC_BEARER_SWITCH_IND status:%d",
				status);
			wake_up_interruptible(&btfmcodec_dev->rsp_wait_q[idx]);
			break;
		default:
			BTFMCODEC_ERR("wrong opcode:%08x", opcode);
		}
		kfree_skb(skb);
	}
	BTFMCODEC_DBG("end");
}

/*
 * btfmcodec_pkt_write() - write() syscall for the btfmcodec_pkt device
 * file:	Pointer to the file structure.
 * buf:		Pointer to the userspace buffer.
 * count:	Number bytes to read from the file.
 * ppos:	Pointer to the position into the file.
 *
 * This function is used to write the data to btfmcodec dev node when
 * userspace client do a write() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static ssize_t btfmcodec_dev_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	struct btfmcodec_data *btfmcodec = file->private_data;
	struct btfmcodec_char_device *btfmcodec_dev= NULL;
	struct sk_buff *skb;
	void *kbuf;
	int ret =  0;

	if (!btfmcodec || !btfmcodec->btfmcodec_dev || refcount_read(&btfmcodec->btfmcodec_dev->active_clients) == 1) {
		BTFMCODEC_INFO("%s: %d\n", current->comm, task_pid_nr(current));
		return -EINVAL;
	} else {
		btfmcodec_dev = btfmcodec->btfmcodec_dev;
	}

	if (mutex_lock_interruptible(&btfmcodec_dev->lock)) {
		ret = -ERESTARTSYS;
		goto free_kbuf;
	}

	/* Hack for Now */
	if (count < MIN_PKT_LEN) {
		BTFMCODEC_ERR("minimum packet len should be greater than 3 bytes");
		goto free_kbuf;
	}
	if (refcount_read(&btfmcodec->btfmcodec_dev->active_clients) == 0) {
		BTFMCODEC_WARN("Client disconnected");
		ret = -ENETRESET;
		goto free_kbuf;
	}

	BTFMCODEC_DBG("begin to %s buffer_size %zu\n", btfmcodec_dev->dev_name, count);
	kbuf = memdup_user(buf, count);
	if (IS_ERR(kbuf)) {
		ret = PTR_ERR(kbuf);
		goto free_kbuf;
	}

	/* Check whether we need a dedicated chunk of memory for this driver */
	skb = alloc_skb(count* sizeof(size_t), GFP_KERNEL);
	if (!skb) {
		BTFMCODEC_ERR("failed to allocate memory for recevied packet");
		ret = -ENOMEM;
		goto free_kbuf;
	}

	skb_put_data(skb, (uint8_t *)kbuf, count);
	skb_queue_tail(&btfmcodec_dev->rxq, skb);
	schedule_work(&btfmcodec_dev->rx_work);

	kfree(kbuf);

free_kbuf:
	mutex_unlock(&btfmcodec_dev->lock);
	BTFMCODEC_DBG("finish to %s ret %d\n", btfmcodec_dev->dev_name, ret);
	return ret < 0 ? ret : count;
}

int btfmcodec_dev_enqueue_pkt(struct btfmcodec_char_device *btfmcodec_dev, void *buf, int len)
{
	struct sk_buff *skb;
	unsigned long flags;
	uint8_t *cmd = buf;

	BTFMCODEC_DBG("start");
	spin_lock_irqsave(&btfmcodec_dev->tx_queue_lock, flags);
	if (refcount_read(&btfmcodec_dev->active_clients) == 1) {
		BTFMCODEC_WARN("no active clients discarding the packet");
		spin_unlock_irqrestore(&btfmcodec_dev->tx_queue_lock, flags);
		return -EINVAL;
	}
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		BTFMCODEC_ERR("failed to allocate memory");
		spin_unlock_irqrestore(&btfmcodec_dev->tx_queue_lock, flags);
		return -ENOMEM;
	}

	skb_put_data(skb, cmd, len);
	skb_queue_tail(&btfmcodec_dev->txq, skb);
	wake_up_interruptible(&btfmcodec_dev->readq);
	spin_unlock_irqrestore(&btfmcodec_dev->tx_queue_lock, flags);
	BTFMCODEC_DBG("end");
	return 0;
}

/*
 * btfmcodec_pkt_poll() - poll() syscall for the btfmcodec device
 * file:	Pointer to the file structure.
 * wait:	pointer to Poll table.
 *
 * This function is used to poll on the btfmcodec dev node when
 * userspace client do a poll() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static __poll_t btfmcodec_dev_poll(struct file *file, poll_table *wait)
{
	struct btfmcodec_data *btfmcodec = file->private_data;
	struct btfmcodec_char_device *btfmcodec_dev= NULL;
	__poll_t mask = 0;
	unsigned long flags;

	BTFMCODEC_DBG("start");
	if (!btfmcodec || !btfmcodec->btfmcodec_dev || refcount_read(&btfmcodec->btfmcodec_dev->active_clients) == 1) {
		BTFMCODEC_INFO("%s: %d\n", current->comm, task_pid_nr(current));
		return -EINVAL;
	} else {
		btfmcodec_dev = btfmcodec->btfmcodec_dev;
	}

	/* Wait here for timeout or for a wakeup signal on readq */
	poll_wait(file, &btfmcodec_dev->readq, wait);

	mutex_lock(&btfmcodec_dev->lock);
	/* recheck if the client has released by the driver */
	if (refcount_read(&btfmcodec_dev->active_clients) == 1) {
		BTFMCODEC_WARN("port has been closed alreadt");
		mutex_unlock(&btfmcodec_dev->lock);
		return POLLHUP;
	}

	spin_lock_irqsave(&btfmcodec_dev->tx_queue_lock, flags);
	/* Set flags if data is avilable to read */
	if (!skb_queue_empty(&btfmcodec_dev->txq))
		mask |= POLLIN | POLLRDNORM;

	spin_unlock_irqrestore(&btfmcodec_dev->tx_queue_lock, flags);
	mutex_unlock(&btfmcodec_dev->lock);

	BTFMCODEC_DBG("end with reason %d", mask);
	return mask;
}

/*
 * btfmcodec_dev_read() - read() syscall for the btfmcodec dev node
 * file:	Pointer to the file structure.
 * buf:		Pointer to the userspace buffer.
 * count:	Number bytes to read from the file.
 * ppos:	Pointer to the position into the file.
 *
 * This function is used to Read the data from btfmcodec pkt device when
 * userspace client do a read() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static ssize_t btfmcodec_dev_read(struct file *file,
			char __user *buf, size_t count, loff_t *ppos)
{
	struct btfmcodec_data *btfmcodec = file->private_data;
	struct btfmcodec_char_device *btfmcodec_dev= NULL;
	unsigned long flags;
	struct sk_buff *skb;
	int use;

	BTFMCODEC_DBG("start");
	if (!btfmcodec || !btfmcodec->btfmcodec_dev || refcount_read(&btfmcodec->btfmcodec_dev->active_clients) == 1) {
		BTFMCODEC_INFO("%s: %d\n", current->comm, task_pid_nr(current));
		return -EINVAL;
	} else {
		btfmcodec_dev = btfmcodec->btfmcodec_dev;
	}

	spin_lock_irqsave(&btfmcodec_dev->tx_queue_lock, flags);
	/* Wait for data in the queue */
	if (skb_queue_empty(&btfmcodec_dev->txq)) {
		spin_unlock_irqrestore(&btfmcodec_dev->tx_queue_lock, flags);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		/* Wait until we get data*/
		if (wait_event_interruptible(btfmcodec_dev->readq,
					     !skb_queue_empty(&btfmcodec_dev->txq)))
			return -ERESTARTSYS;

		/* We lost the client while waiting */
		if (refcount_read(&btfmcodec->btfmcodec_dev->active_clients) == 1)
			return -ENETRESET;

		spin_lock_irqsave(&btfmcodec_dev->tx_queue_lock, flags);
	}

	skb = skb_dequeue(&btfmcodec_dev->txq);
	spin_unlock_irqrestore(&btfmcodec_dev->tx_queue_lock, flags);
	if (!skb)
		return -EFAULT;

	use = min_t(size_t, count, skb->len);
	if (copy_to_user(buf, skb->data, use))
		use = -EFAULT;

	kfree_skb(skb);

	BTFMCODEC_DBG("end for %s by %s:%d ret[%d]\n", btfmcodec_dev->dev_name,
		       current->comm, task_pid_nr(current), use);

	return use;
}
static const struct file_operations btfmcodec_fops = {
	.owner = THIS_MODULE,
	.open = btfmcodec_dev_open,
	.release = btfmcodec_dev_release,
	.write = btfmcodec_dev_write,
	.poll = btfmcodec_dev_poll,
	.read = btfmcodec_dev_read,
	/* For Now add no hookups for below callbacks */
	.unlocked_ioctl = NULL,
	.compat_ioctl = NULL,
};

static ssize_t btfmcodec_attributes_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t n)
{
	struct btfmcodec_data *btfmcodec = dev_to_btfmcodec(dev);
	struct btfmcodec_char_device *btfmcodec_dev = btfmcodec->btfmcodec_dev;
	long tmp;

	mutex_lock(&btfmcodec_dev->lock);
	if (kstrtol(buf, 0, &tmp)) {
		mutex_unlock(&btfmcodec_dev->lock);
/*		BTFMCODEC_ERR("unable to convert string to int for /dev/%s\n",
			      btfmcodec->dev->name);
*/		return -EINVAL;
	}
	mutex_unlock(&btfmcodec_dev->lock);

	return n;
}

static ssize_t btfmcodec_attributes_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
//	struct btfmcodec_get_current_transport *btfmcodec_dev = dev_to_btfmcodec(dev);

	return 0;
}

struct btfmcodec_data* btfm_get_btfmcodec(void)
{
	return btfmcodec;
}
EXPORT_SYMBOL(btfm_get_btfmcodec);

static DEVICE_ATTR_RW(btfmcodec_attributes);

static int __init btfmcodec_init(void)
{
	struct btfmcodec_state_machine *states;
	struct btfmcodec_char_device *btfmcodec_dev;
	struct device *dev;
	int ret, i;

	BTFMCODEC_INFO("starting up the module");
	btfmcodec = kzalloc(sizeof(struct btfmcodec_data), GFP_KERNEL);
	if (!btfmcodec) {
		BTFMCODEC_ERR("failed to allocate memory");
		return -ENOMEM;
	}

	states = &btfmcodec->states;
	states->current_state = IDLE;
	states->next_state = IDLE;

	BTFMCODEC_INFO("creating device node");
	/* create device node for communication between userspace and kernel */
	btfmcodec_dev = kzalloc(sizeof(struct btfmcodec_char_device), GFP_KERNEL);
	if (!btfmcodec_dev) {
		BTFMCODEC_ERR("failed to allocate memory");
		ret = -ENOMEM;
		goto info_cleanup;
	}

	BTFMCODEC_INFO("trying to get major number\n");
	ret = alloc_chrdev_region(&dev_major, 0, 0, "btfmcodec");
	if (ret < 0) {
		BTFMCODEC_ERR("failed to allocate character device region");
		goto dev_cleanup;
	}

	BTFMCODEC_INFO("creating btfm codec class");
	dev_class = class_create(THIS_MODULE, "btfmcodec");
	if (IS_ERR(dev_class)) {
		ret = PTR_ERR(dev_class);
		BTFMCODEC_ERR("class_create failed ret:%d\n", ret);
		goto deinit_chrdev;
	}

	btfmcodec_dev->reuse_minor = idr_alloc(&dev_minor, btfmcodec, 1, 0, GFP_KERNEL);
	if (ret < 0) {
		BTFMCODEC_ERR("failed to allocated minor number");
		goto deinit_class;
	}

	dev = &btfmcodec->dev;
	dev->driver = &driver;

	// ToDo Rethink of having btfmcodec alone instead of btfmcodec
	btfmcodec->btfmcodec_dev = btfmcodec_dev;
	refcount_set(&btfmcodec_dev->active_clients, 1);
	mutex_init(&btfmcodec_dev->lock);
	strlcpy(btfmcodec_dev->dev_name, "btfmcodec_dev", DEVICE_NAME_MAX_LEN);
	device_initialize(dev);
	dev->class = dev_class;
	dev->devt = MKDEV(MAJOR(dev_major), btfmcodec_dev->reuse_minor);
	dev_set_drvdata(dev, btfmcodec);

	cdev_init(&btfmcodec_dev->cdev, &btfmcodec_fops);
	btfmcodec_dev->cdev.owner = THIS_MODULE;
	btfmcodec_dev->btfmcodec = (struct btfmcodec_data *)btfmcodec;
	dev_set_name(dev, btfmcodec_dev->dev_name, btfmcodec_dev->reuse_minor);
	ret = cdev_add(&btfmcodec_dev->cdev, dev->devt, 1);
	if (ret) {
		BTFMCODEC_ERR("cdev_add failed with error no %d", ret);
		goto idr_cleanup;
	}

	// ToDo to handler HIDL abrupt kill
	dev->release = NULL;
	ret = device_add(dev);
	if (ret) {
		BTFMCODEC_ERR("Failed to add device error no %d", ret);
		goto free_device;
	}

	BTFMCODEC_ERR("Creating a sysfs entry with name: %s", btfmcodec_dev->dev_name);
	ret = device_create_file(dev, &dev_attr_btfmcodec_attributes);
	if (ret) {
		BTFMCODEC_ERR("Failed to create a devicd node: %s", btfmcodec_dev->dev_name);
		goto free_device;
	}

	BTFMCODEC_INFO("created a node at /dev/%s with %u:%u\n",
		btfmcodec_dev->dev_name, dev_major, btfmcodec_dev->reuse_minor);

	skb_queue_head_init(&btfmcodec_dev->rxq);
	mutex_init(&btfmcodec_dev->lock);
	INIT_WORK(&btfmcodec_dev->rx_work, btfmcodec_dev_rxwork);
	init_waitqueue_head(&btfmcodec_dev->readq);
	spin_lock_init(&btfmcodec_dev->tx_queue_lock);
	skb_queue_head_init(&btfmcodec_dev->txq);
	INIT_LIST_HEAD(&btfmcodec->config_head);
	for (i = 0; i < BTM_PKT_TYPE_MAX; i++) {
		init_waitqueue_head(&btfmcodec_dev->rsp_wait_q[i]);
	}
	mutex_init(&states->state_machine_lock);
	btfmcodec_dev->workqueue = alloc_ordered_workqueue("btfmcodec_wq", 0);
	if (!btfmcodec_dev->workqueue) {
		BTFMCODEC_ERR("btfmcodec_dev Workqueue not initialized properly");
		ret = -ENOMEM;
		goto free_device;
	}
	return ret;

free_device:
	put_device(dev);
idr_cleanup:
	idr_remove(&dev_minor, btfmcodec_dev->reuse_minor);
deinit_class:
	class_destroy(dev_class);
deinit_chrdev:
	unregister_chrdev_region(MAJOR(dev_major), 0);
dev_cleanup:
	kfree(btfmcodec_dev);
info_cleanup:
	kfree(btfmcodec);

	return ret;
}

static void __exit btfmcodec_deinit(void)
{
	struct btfmcodec_char_device *btfmcodec_dev;
	struct device *dev;
	BTFMCODEC_INFO("cleaning up btfm codec driver", __func__);
	if (!btfmcodec) {
		BTFMCODEC_ERR("skiping driver cleanup", __func__);
		goto info_cleanup;
	}

	dev = &btfmcodec->dev;

	device_remove_file(dev, &dev_attr_btfmcodec_attributes);
	put_device(dev);

	if (!btfmcodec->btfmcodec_dev) {
		BTFMCODEC_ERR("skiping device node cleanup", __func__);
		goto info_cleanup;
	}

	btfmcodec_dev = btfmcodec->btfmcodec_dev;
	skb_queue_purge(&btfmcodec_dev->rxq);
	idr_remove(&dev_minor, btfmcodec_dev->reuse_minor);
	class_destroy(dev_class);
	unregister_chrdev_region(MAJOR(dev_major), 0);
	kfree(btfmcodec_dev);
info_cleanup:
	kfree(btfmcodec);
	BTFMCODEC_INFO("btfm codec driver cleanup completed", __func__);
	return;
}

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM Bluetooth FM CODEC driver");

module_init(btfmcodec_init);
module_exit(btfmcodec_deinit);
