/* arch/arm/mach-omap2/board-tuna-vibrator.c
 *
 * Copyright (C) 2011 Samsung Electronics Co. Ltd. All Rights Reserved.
 * Author: Rom Lemarchand <rlemarchand@sta.samsung.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/hrtimer.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>
#include <linux/mutex.h>
#include <asm/mach-types.h>
#include <plat/dmtimer.h>
#ifdef CONFIG_FEATURE_TGN
#include <linux/miscdevice.h>
#endif

#include <../../../drivers/staging/android/timed_output.h>

#include "mux.h"
#include "board-tuna.h"

/* Vibrator enable pin is changed on Rev 05 to block not intended vibration. */
#define GPIO_MOTOR_EN		162
#define GPIO_MOTOR_EN_REV05	54

#define VIB_GPTIMER_NUM		10
#define PWM_DUTY_MAX		1450
#define MAX_TIMEOUT		10000 /* 10s */

static struct vibrator {
	struct wake_lock wklock;
	struct hrtimer timer;
	struct mutex lock;
	struct omap_dm_timer *gptimer;
	bool enabled;
	unsigned gpio_en;
#ifdef CONFIG_FEATURE_TGN
	int vib_level;
	int cur_vib_level;
#endif
} vibdata;

#ifdef CONFIG_FEATURE_TGN
static const int vib_duty_tbl[] = { 750, 850, 950, 1150, 1250, 1350, 1450, 1550, 1650, 1750, 1850, 1950, 2050, };
#define VIB_LEVEL_MAX		(ARRAY_SIZE(vib_duty_tbl))
#define VIB_LEVEL_MIN		(0)
#define VIB_LEVEL_DEFAULT	(6)

/* sysfs */
static ssize_t show_vibrator_level_max(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%d\n", VIB_LEVEL_MAX - 1);
}

static ssize_t show_vibrator_level(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%d\n", vibdata.vib_level);
}

static ssize_t store_vibrator_level(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	int data = 0;
	if (sscanf(buf, "%u\n", &data) == 1) {
		if (data >= VIB_LEVEL_MAX)
			data = VIB_LEVEL_MAX - 1;
		else if (data < VIB_LEVEL_MIN)
			data = VIB_LEVEL_MIN;
		vibdata.vib_level = data;
	} else {
		printk(KERN_ERR "tspdrv: invalid vibrator level\n");
	}
	return len;
}

static DEVICE_ATTR(vibrator_level_max, S_IRUGO | S_IWUGO, show_vibrator_level_max, NULL);
static DEVICE_ATTR(vibrator_level, S_IRUGO | S_IWUGO, show_vibrator_level, store_vibrator_level);

static struct miscdevice vib_ctrl_dev = {
	.minor =    MISC_DYNAMIC_MINOR,
	.name =     "vib_ctrl",
};
#endif /* CONFIG_FEATURE_TGN */

static void vibrator_off(void)
{
	if (!vibdata.enabled)
		return;
	omap_dm_timer_stop(vibdata.gptimer);
	gpio_set_value(vibdata.gpio_en, 0);
	vibdata.enabled = false;
	wake_unlock(&vibdata.wklock);
}

static int vibrator_get_time(struct timed_output_dev *dev)
{
	if (hrtimer_active(&vibdata.timer)) {
		ktime_t r = hrtimer_get_remaining(&vibdata.timer);
		return ktime_to_ms(r);
	}

	return 0;
}

static void vibrator_enable(struct timed_output_dev *dev, int value)
{
	mutex_lock(&vibdata.lock);

	/* cancel previous timer and set GPIO according to value */
	hrtimer_cancel(&vibdata.timer);

	if (value) {
		wake_lock(&vibdata.wklock);

#ifdef CONFIG_FEATURE_TGN
	if (vibdata.vib_level != vibdata.cur_vib_level) {
		int duty = vib_duty_tbl[vibdata.vib_level];
		omap_dm_timer_set_load(vibdata.gptimer, 1, -duty);
		vibdata.gptimer->context.tldr = (unsigned int)-duty;
		omap_dm_timer_set_match(vibdata.gptimer, 1, -duty+10);
		vibdata.gptimer->context.tmar = (unsigned int)(-duty+10);
		vibdata.cur_vib_level = vibdata.vib_level;
	}
#endif

		gpio_set_value(vibdata.gpio_en, 1);
		omap_dm_timer_start(vibdata.gptimer);

		vibdata.enabled = true;

		if (value > 0) {
			if (value > MAX_TIMEOUT)
				value = MAX_TIMEOUT;

			hrtimer_start(&vibdata.timer,
				ns_to_ktime((u64)value * NSEC_PER_MSEC),
				HRTIMER_MODE_REL);
		}
	} else {
		vibrator_off();
	}

	mutex_unlock(&vibdata.lock);
}

static struct timed_output_dev to_dev = {
	.name		= "vibrator",
	.get_time	= vibrator_get_time,
	.enable		= vibrator_enable,
};

static enum hrtimer_restart vibrator_timer_func(struct hrtimer *timer)
{
	vibrator_off();
	return HRTIMER_NORESTART;
}

static int __init vibrator_init(void)
{
	int ret;

#ifdef CONFIG_FEATURE_TGN
	vibdata.vib_level = VIB_LEVEL_DEFAULT;
	vibdata.cur_vib_level = VIB_LEVEL_DEFAULT;
#endif

	vibdata.enabled = false;

	hrtimer_init(&vibdata.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vibdata.timer.function = vibrator_timer_func;

	vibdata.gptimer = omap_dm_timer_request_specific(VIB_GPTIMER_NUM);
	if (vibdata.gptimer == NULL)
		return -1;

	ret = omap_dm_timer_set_source(vibdata.gptimer,
		OMAP_TIMER_SRC_SYS_CLK);
	if (ret < 0)
		goto err_dm_timer_src;

	omap_dm_timer_set_load(vibdata.gptimer, 1, -PWM_DUTY_MAX);
	omap_dm_timer_set_match(vibdata.gptimer, 1, -PWM_DUTY_MAX+10);
	omap_dm_timer_set_pwm(vibdata.gptimer, 0, 1,
		OMAP_TIMER_TRIGGER_OVERFLOW_AND_COMPARE);
	omap_dm_timer_enable(vibdata.gptimer);
	omap_dm_timer_write_counter(vibdata.gptimer, -2);
	omap_dm_timer_disable(vibdata.gptimer);

	wake_lock_init(&vibdata.wklock, WAKE_LOCK_SUSPEND, "vibrator");
	mutex_init(&vibdata.lock);

	ret = timed_output_dev_register(&to_dev);
	if (ret < 0)
		goto err_to_dev_reg;

#ifdef CONFIG_FEATURE_TGN
	printk(KERN_DEBUG "[Vib] %s misc_register(%s)\n", __func__, vib_ctrl_dev.name);
	ret = misc_register(&vib_ctrl_dev);
	if (ret) {
		pr_err("[BLN] %s misc_register(%s) fail\n", __func__, vib_ctrl_dev.name);
		goto err_to_dev_reg;
	}
	if (device_create_file(vib_ctrl_dev.this_device, &dev_attr_vibrator_level_max) < 0) {
		printk(KERN_ERR "[Vib] Failed to create device file(%s)!\n", dev_attr_vibrator_level_max.attr.name);
	}
	if (device_create_file(vib_ctrl_dev.this_device, &dev_attr_vibrator_level) < 0) {
		printk(KERN_ERR "[Vib] Failed to create device file(%s)!\n", dev_attr_vibrator_level.attr.name);
	}
#endif

	return 0;

err_to_dev_reg:
	mutex_destroy(&vibdata.lock);
	wake_lock_destroy(&vibdata.wklock);

err_dm_timer_src:
	omap_dm_timer_free(vibdata.gptimer);
	vibdata.gptimer = NULL;

	return -1;
}

static int __init omap4_tuna_vibrator_init(void)
{
	int ret;

	if (!machine_is_tuna())
		return 0;

	vibdata.gpio_en = (omap4_tuna_get_revision() >= 5) ?
			GPIO_MOTOR_EN_REV05 : GPIO_MOTOR_EN;

	omap_mux_init_gpio(vibdata.gpio_en, OMAP_PIN_OUTPUT |
						OMAP_PIN_OFF_OUTPUT_LOW);
	omap_mux_init_signal("dpm_emu18.dmtimer10_pwm_evt", OMAP_PIN_OUTPUT);

	ret = gpio_request(vibdata.gpio_en, "vibrator-en");
	if (ret)
		return ret;

	gpio_direction_output(vibdata.gpio_en, 0);

	ret = vibrator_init();
	if (ret < 0)
		gpio_free(vibdata.gpio_en);

	return ret;
}

/*
 * This is needed because the vibrator is dependent on omap_dm_timers which get
 * initialized at device_init time
 */
late_initcall(omap4_tuna_vibrator_init);
