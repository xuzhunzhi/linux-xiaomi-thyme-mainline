// SPDX-License-Identifier: GPL-2.0
/* One-shot diagnostic: clear a failed, unused VCM's runtime PM state. */
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

static char *supply;
module_param(supply, charp, 0400);
static struct regulator *extra_supply;

static int __init thyme_vcm_pm_reset_init(void)
{
	struct device *dev;
	int ret = 0;
	struct device *sensor;

	if (supply) {
		sensor = bus_find_device_by_name(&i2c_bus_type, NULL, "21-003d");
		if (!sensor)
			return -ENODEV;
		extra_supply = regulator_get(sensor, supply);
		put_device(sensor);
		if (IS_ERR(extra_supply))
			return PTR_ERR(extra_supply);
		ret = regulator_enable(extra_supply);
		if (ret) {
			regulator_put(extra_supply);
			return ret;
		}
		usleep_range(10000, 11000);
	}

	dev = bus_find_device_by_name(&i2c_bus_type, NULL, "21-000c");
	if (!dev)
		goto err_supply;

	device_lock(dev);
	pm_runtime_disable(dev);
	if (atomic_read(&dev->power.usage_count)) {
		ret = -EBUSY;
	} else if (dev->power.runtime_error) {
		dev_info(dev, "diagnostic: clearing runtime PM error %d\n",
			 dev->power.runtime_error);
		ret = pm_runtime_set_suspended(dev);
	}
	pm_runtime_enable(dev);
	device_unlock(dev);
	put_device(dev);
	if (ret)
		goto err_supply;
	return ret;

err_supply:
	if (extra_supply) {
		regulator_disable(extra_supply);
		regulator_put(extra_supply);
	}
	return ret ?: -ENODEV;
}

static void __exit thyme_vcm_pm_reset_exit(void)
{
	if (extra_supply) {
		regulator_disable(extra_supply);
		regulator_put(extra_supply);
	}
}
module_init(thyme_vcm_pm_reset_init);
module_exit(thyme_vcm_pm_reset_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("One-shot thyme VCM runtime PM diagnostic recovery");
