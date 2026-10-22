// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/mhi.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <net/sock.h>

#include "qrtr.h"

struct qrtr_mhi_dev {
	struct qrtr_endpoint ep;
	struct mhi_device *mhi_dev;
	struct device *dev;
	wait_queue_head_t tx_wait;
	bool stopping;
};

/* From MHI to QRTR */
static void qcom_mhi_qrtr_dl_callback(struct mhi_device *mhi_dev,
				      struct mhi_result *mhi_res)
{
	struct qrtr_mhi_dev *qdev = dev_get_drvdata(&mhi_dev->dev);
	int rc;

	if (!qdev || (mhi_res->transaction_status && mhi_res->transaction_status != -ENOTCONN))
		return;

	/* Channel got reset. So just free the buffer */
	if (mhi_res->transaction_status == -ENOTCONN) {
		devm_kfree(&mhi_dev->dev, mhi_res->buf_addr);
		return;
	}

	rc = qrtr_endpoint_post(&qdev->ep, mhi_res->buf_addr,
				mhi_res->bytes_xferd);
	if (rc == -EINVAL)
		dev_err(qdev->dev, "invalid ipcrouter packet\n");

	/* Done with the buffer, now recycle it for future use */
	rc = mhi_queue_buf(mhi_dev, DMA_FROM_DEVICE, mhi_res->buf_addr,
			   mhi_dev->mhi_cntrl->buffer_len, MHI_EOT);
	if (rc)
		dev_err(&mhi_dev->dev, "Failed to recycle the buffer: %d\n", rc);
}

/* From QRTR to MHI */
static void qcom_mhi_qrtr_ul_callback(struct mhi_device *mhi_dev,
				      struct mhi_result *mhi_res)
{
	struct qrtr_mhi_dev *qdev = dev_get_drvdata(&mhi_dev->dev);
	struct sk_buff *skb = mhi_res->buf_addr;

	wake_up_all(&qdev->tx_wait);
	if (skb->sk)
		sock_put(skb->sk);
	consume_skb(skb);
}

/* Send data over MHI */
static int qcom_mhi_qrtr_send(struct qrtr_endpoint *ep, struct sk_buff *skb)
{
	struct qrtr_mhi_dev *qdev = container_of(ep, struct qrtr_mhi_dev, ep);
	int rc;

	if (skb->sk)
		sock_hold(skb->sk);

	rc = skb_linearize(skb);
	if (rc)
		goto free_skb;

	/* A QRTR window can exceed the small Fusion IPCR ring. The endpoint
	 * lock serializes senders; wait for completion instead of dropping
	 * service announcements and firmware data on a full ring.
	 */
	rc = wait_event_interruptible_timeout(qdev->tx_wait,
		READ_ONCE(qdev->stopping) ||
		mhi_get_free_desc_count(qdev->mhi_dev, DMA_TO_DEVICE),
		msecs_to_jiffies(1000));
	if (rc <= 0 || READ_ONCE(qdev->stopping)) {
		rc = rc < 0 ? rc : -ENOTCONN;
		goto free_skb;
	}

	rc = mhi_queue_skb(qdev->mhi_dev, DMA_TO_DEVICE, skb, skb->len,
			   MHI_EOT);
	if (rc)
		goto free_skb;

	return rc;

free_skb:
	if (skb->sk)
		sock_put(skb->sk);
	kfree_skb(skb);

	return rc;
}

static int qcom_mhi_qrtr_queue_dl_buffers(struct mhi_device *mhi_dev)
{
	u32 free_desc;
	void *buf;
	int ret;

	free_desc = mhi_get_free_desc_count(mhi_dev, DMA_FROM_DEVICE);
	while (free_desc--) {
		buf = devm_kmalloc(&mhi_dev->dev, mhi_dev->mhi_cntrl->buffer_len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		ret = mhi_queue_buf(mhi_dev, DMA_FROM_DEVICE, buf, mhi_dev->mhi_cntrl->buffer_len,
				    MHI_EOT);
		if (ret) {
			dev_err(&mhi_dev->dev, "Failed to queue buffer: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int qcom_mhi_qrtr_probe(struct mhi_device *mhi_dev,
			       const struct mhi_device_id *id)
{
	struct qrtr_mhi_dev *qdev;
	int rc;

	qdev = devm_kzalloc(&mhi_dev->dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;

	qdev->mhi_dev = mhi_dev;
	qdev->dev = &mhi_dev->dev;
	qdev->ep.xmit = qcom_mhi_qrtr_send;
	init_waitqueue_head(&qdev->tx_wait);

	dev_set_drvdata(&mhi_dev->dev, qdev);

	/* start channels */
	rc = mhi_prepare_for_transfer(mhi_dev);
	if (rc)
		return rc;

	rc = qrtr_endpoint_register(&qdev->ep, QRTR_EP_NID_AUTO);
	if (rc)
		goto err_unprepare;

	rc = qcom_mhi_qrtr_queue_dl_buffers(mhi_dev);
	if (rc)
		goto err_unregister;

	dev_dbg(qdev->dev, "Qualcomm MHI QRTR driver probed\n");

	return 0;

err_unregister:
	qrtr_endpoint_unregister(&qdev->ep);
err_unprepare:
	mhi_unprepare_from_transfer(mhi_dev);

	return rc;
}

static void qcom_mhi_qrtr_remove(struct mhi_device *mhi_dev)
{
	struct qrtr_mhi_dev *qdev = dev_get_drvdata(&mhi_dev->dev);

	WRITE_ONCE(qdev->stopping, true);
	wake_up_all(&qdev->tx_wait);
	qrtr_endpoint_unregister(&qdev->ep);
	mhi_unprepare_from_transfer(mhi_dev);
	dev_set_drvdata(&mhi_dev->dev, NULL);
}

static const struct mhi_device_id qcom_mhi_qrtr_id_table[] = {
	{ .chan = "IPCR" },
	{}
};
MODULE_DEVICE_TABLE(mhi, qcom_mhi_qrtr_id_table);

static int __maybe_unused qcom_mhi_qrtr_pm_suspend_late(struct device *dev)
{
	struct mhi_device *mhi_dev = container_of(dev, struct mhi_device, dev);
	enum mhi_state state;

	state = mhi_get_mhi_state(mhi_dev->mhi_cntrl);
	/*
	 * Retain channels in M3, or when Fusion declined M3 and stayed in M0.
	 */
	if (state == MHI_STATE_M3 ||
	    (state == MHI_STATE_M0 &&
	     mhi_dev->mhi_cntrl->amss_image))
		return 0;

	mhi_unprepare_from_transfer(mhi_dev);

	return 0;
}

static int __maybe_unused qcom_mhi_qrtr_pm_resume_early(struct device *dev)
{
	struct mhi_device *mhi_dev = container_of(dev, struct mhi_device, dev);
	enum mhi_state state;
	int rc;

	state = mhi_get_mhi_state(mhi_dev->mhi_cntrl);
	/*
	 * Mirror suspend: restore only channels that were torn down.
	 */
	if (state == MHI_STATE_M3 ||
	    (state == MHI_STATE_M0 &&
	     mhi_dev->mhi_cntrl->amss_image))
		return 0;

	rc = mhi_prepare_for_transfer(mhi_dev);
	if (rc) {
		dev_err(dev, "failed to prepare for autoqueue transfer %d\n", rc);
		return rc;
	}

	return qcom_mhi_qrtr_queue_dl_buffers(mhi_dev);
}

static const struct dev_pm_ops qcom_mhi_qrtr_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(qcom_mhi_qrtr_pm_suspend_late,
				     qcom_mhi_qrtr_pm_resume_early)
};

static struct mhi_driver qcom_mhi_qrtr_driver = {
	.probe = qcom_mhi_qrtr_probe,
	.remove = qcom_mhi_qrtr_remove,
	.dl_xfer_cb = qcom_mhi_qrtr_dl_callback,
	.ul_xfer_cb = qcom_mhi_qrtr_ul_callback,
	.id_table = qcom_mhi_qrtr_id_table,
	.driver = {
		.name = "qcom_mhi_qrtr",
		.pm = &qcom_mhi_qrtr_pm_ops,
	},
};

module_mhi_driver(qcom_mhi_qrtr_driver);

MODULE_AUTHOR("Chris Lew <clew@codeaurora.org>");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Qualcomm IPC-Router MHI interface driver");
MODULE_LICENSE("GPL v2");
