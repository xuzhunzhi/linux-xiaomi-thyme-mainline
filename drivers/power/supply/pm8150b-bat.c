// SPDX-License-Identifier: GPL-2.0
/*
 * Simple battery driver for Xiaomi SM8250 phones (Mi 10 "umi", Mi 10S "thyme").
 *
 * Mainline has no fuel-gauge driver for the PM8150B FG-GEN4 block, so there
 * is otherwise no battery percentage for the desktop to show. This driver
 * reads the cell terminal voltage from the PM8150B ADC5 VBAT_SNS channel and
 * estimates state of charge from a static OCV table. Charging state is taken
 * from the TCPM source power supply when the Type-C port acts as a sink.
 *
 * Derived from the out-of-tree pm8150b-bat module used for Mobian on umi.
 */

#include <linux/iio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#define PM8150B_BAT_POLL_SEC	30
#define PM8150B_BAT_MAX_STEP	2	/* max percentage change per poll */

#define TCPM_SOURCE_PSY_PREFIX	"tcpm-source-psy-"

struct pm8150b_bat {
	struct device *dev;
	struct power_supply *psy;
	struct iio_channel *vbat_chan;
	struct delayed_work poll_work;
	int cached_capacity;
	int cached_voltage;
	bool charging;
};

/*
 * Resting voltage (OCV) vs state of charge for the 4.45 V high-voltage
 * Li-ion cell in these phones. Vendor DT for thyme: fv-max-uv = 4450000,
 * fg-cutoff-voltage = 3400 mV, nom-batt-capacity-mah = 4780.
 *
 * VBAT_SNS is the terminal voltage, not a true OCV, so the estimate reads a
 * little high while charging and a little low under load. The per-poll step
 * limit keeps it from jumping around.
 */
static const struct {
	int mv;
	int pct;
} ocv_table[] = {
	{ 4400, 100 }, { 4330, 95 }, { 4280, 90 }, { 4220, 85 },
	{ 4170,  80 }, { 4120, 75 }, { 4070, 70 }, { 4030, 65 },
	{ 3990,  60 }, { 3950, 55 }, { 3910, 50 }, { 3870, 45 },
	{ 3840,  40 }, { 3810, 35 }, { 3790, 30 }, { 3770, 25 },
	{ 3740,  20 }, { 3710, 15 }, { 3680, 10 }, { 3620,  5 },
	{ 3400,   0 },
};

static int voltage_to_capacity(int vbat_uv)
{
	int vbat_mv = vbat_uv / 1000;
	int i;

	if (vbat_mv >= ocv_table[0].mv)
		return 100;
	if (vbat_mv <= ocv_table[ARRAY_SIZE(ocv_table) - 1].mv)
		return 0;

	for (i = 0; i < ARRAY_SIZE(ocv_table) - 1; i++) {
		if (vbat_mv >= ocv_table[i + 1].mv) {
			int range_mv = ocv_table[i].mv - ocv_table[i + 1].mv;
			int range_pct = ocv_table[i].pct - ocv_table[i + 1].pct;
			int offset_mv = vbat_mv - ocv_table[i + 1].mv;

			return ocv_table[i + 1].pct +
			       offset_mv * range_pct / range_mv;
		}
	}
	return 0;
}

static int read_vbat(struct pm8150b_bat *bat)
{
	int ret, vbat_uv;

	ret = iio_read_channel_processed(bat->vbat_chan, &vbat_uv);
	if (ret < 0)
		return ret;
	return vbat_uv;
}

/*
 * Charging is detected through the TCPM "source" supply: it reports ONLINE
 * when the Type-C port is sinking power. The supply name embeds the device
 * path, so match on the fixed prefix instead of a hardcoded full name.
 */
static int pm8150b_bat_check_source(struct power_supply *psy, void *data)
{
	union power_supply_propval val;
	bool *charging = data;

	if (!psy->desc || !psy->desc->name)
		return 0;
	if (strncmp(psy->desc->name, TCPM_SOURCE_PSY_PREFIX,
		    strlen(TCPM_SOURCE_PSY_PREFIX)))
		return 0;
	if (power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val))
		return 0;
	if (val.intval) {
		*charging = true;
		return 1;	/* stop iterating */
	}
	return 0;
}

static void pm8150b_bat_poll(struct work_struct *work)
{
	struct pm8150b_bat *bat = container_of(work, struct pm8150b_bat,
					       poll_work.work);
	bool charging = false;
	bool changed = false;
	int vbat_uv, new_cap, delta;

	power_supply_for_each_psy(&charging, pm8150b_bat_check_source);
	if (charging != bat->charging) {
		bat->charging = charging;
		changed = true;
	}

	vbat_uv = read_vbat(bat);
	if (vbat_uv >= 0) {
		if (vbat_uv != bat->cached_voltage)
			changed = true;
		bat->cached_voltage = vbat_uv;

		new_cap = voltage_to_capacity(vbat_uv);
		delta = new_cap - bat->cached_capacity;
		if (delta > PM8150B_BAT_MAX_STEP)
			new_cap = bat->cached_capacity + PM8150B_BAT_MAX_STEP;
		else if (delta < -PM8150B_BAT_MAX_STEP)
			new_cap = bat->cached_capacity - PM8150B_BAT_MAX_STEP;
		new_cap = clamp(new_cap, 0, 100);
		if (new_cap != bat->cached_capacity) {
			bat->cached_capacity = new_cap;
			changed = true;
		}
	}

	if (changed)
		power_supply_changed(bat->psy);

	schedule_delayed_work(&bat->poll_work, PM8150B_BAT_POLL_SEC * HZ);
}

static enum power_supply_property pm8150b_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_SCOPE,
};

static int pm8150b_bat_get_prop(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct pm8150b_bat *bat = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (bat->charging)
			val->intval = bat->cached_capacity >= 100 ?
				POWER_SUPPLY_STATUS_FULL :
				POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = bat->cached_voltage;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = 3400000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 4450000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = bat->cached_capacity;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct power_supply_desc pm8150b_bat_desc = {
	.name = "pm8150b-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = pm8150b_bat_props,
	.num_properties = ARRAY_SIZE(pm8150b_bat_props),
	.get_property = pm8150b_bat_get_prop,
};

static int pm8150b_bat_probe(struct platform_device *pdev)
{
	struct power_supply_config cfg = {};
	struct pm8150b_bat *bat;
	int vbat_uv;

	bat = devm_kzalloc(&pdev->dev, sizeof(*bat), GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	bat->dev = &pdev->dev;
	platform_set_drvdata(pdev, bat);

	bat->vbat_chan = devm_iio_channel_get(&pdev->dev, "vbat_sns");
	if (IS_ERR(bat->vbat_chan))
		return dev_err_probe(&pdev->dev, PTR_ERR(bat->vbat_chan),
				     "failed to get vbat_sns channel\n");

	vbat_uv = read_vbat(bat);
	if (vbat_uv >= 0) {
		bat->cached_voltage = vbat_uv;
		bat->cached_capacity = voltage_to_capacity(vbat_uv);
	} else {
		dev_warn(&pdev->dev, "initial vbat read failed: %d\n", vbat_uv);
	}

	cfg.fwnode = dev_fwnode(&pdev->dev);
	cfg.drv_data = bat;

	bat->psy = devm_power_supply_register(&pdev->dev, &pm8150b_bat_desc,
					      &cfg);
	if (IS_ERR(bat->psy))
		return PTR_ERR(bat->psy);

	INIT_DELAYED_WORK(&bat->poll_work, pm8150b_bat_poll);
	schedule_delayed_work(&bat->poll_work, PM8150B_BAT_POLL_SEC * HZ);

	dev_info(&pdev->dev, "pm8150b battery registered: %d mV -> %d%%\n",
		 bat->cached_voltage / 1000, bat->cached_capacity);
	return 0;
}

static void pm8150b_bat_remove(struct platform_device *pdev)
{
	struct pm8150b_bat *bat = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&bat->poll_work);
}

static const struct of_device_id pm8150b_bat_of_match[] = {
	{ .compatible = "xiaomi,thyme-battery" },
	{ .compatible = "xiaomi,umi-battery" },
	{}
};
MODULE_DEVICE_TABLE(of, pm8150b_bat_of_match);

static struct platform_driver pm8150b_bat_driver = {
	.probe = pm8150b_bat_probe,
	.remove = pm8150b_bat_remove,
	.driver = {
		.name = "pm8150b-battery",
		.of_match_table = pm8150b_bat_of_match,
	},
};
module_platform_driver(pm8150b_bat_driver);

MODULE_DESCRIPTION("PM8150B VBAT_SNS battery estimate for Xiaomi SM8250 phones");
MODULE_LICENSE("GPL");
