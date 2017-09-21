/*
 * imx208.c - imx208 sensor driver
 *
 * Copyright (c) 2014-2015, NVIDIA CORPORATION, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <media/imx208.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include "nvc_utilities.h"

struct imx208_reg {
	u16 addr;
	u8 val;
};

struct imx208_info {
	struct miscdevice		miscdev_info;
	int				mode;
	struct imx208_power_rail	power;
	struct imx208_sensordata	sensor_data;
	struct i2c_client		*i2c_client;
	struct imx208_platform_data	*pdata;
	struct clk			*mclk;
	struct regmap			*regmap;
	struct mutex			imx208_camera_lock;
	atomic_t			in_use;
};

static const struct regmap_config sensor_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

#define IMX208_TABLE_WAIT_MS 0
#define IMX208_TABLE_END 1
#define IMX208_MAX_RETRIES 3
#define IMX208_WAIT_MS 3

#define MAX_BUFFER_SIZE 32
#define IMX208_FRAME_LENGTH_ADDR_MSB 0x0340
#define IMX208_FRAME_LENGTH_ADDR_LSB 0x0341
#define IMX208_COARSE_TIME_ADDR_MSB 0x0202
#define IMX208_COARSE_TIME_ADDR_LSB 0x0203
#define IMX208_GAIN_ADDR 0x0205

static struct imx208_reg mode_1920x1080[] = {
	/* PLL Setting */
	{0x0305, 0x04},
	{0x0307, 0x87},
	{0x303C, 0x4B},
	{0x30A4, 0x02},
	/* Mode setting */
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0340, 0x04},
	{0x0341, 0xB0},
	{0x0342, 0x08},
	{0x0343, 0xC8},
	{0x0344, 0x00},
	{0x0345, 0x08},
	{0x0346, 0x00},
	{0x0347, 0x08},
	{0x0348, 0x07},
	{0x0349, 0x87},
	{0x034A, 0x04},
	{0x034B, 0x3F},
	{0x034C, 0x07},
	{0x034D, 0x80},
	{0x034E, 0x04},
	{0x034F, 0x38},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x3048, 0x00},
	{0x304E, 0x0A},
	{0x3050, 0x02},
	{0x309B, 0x00},
	{0x30D5, 0x00},
	{0x3301, 0x01},
	{0x3318, 0x61},
	/* Shutter Gain Setting */
	{0x0202, 0x01},
	{0x0203, 0x90},
	{0x0205, 0x00},
	{0x0100, 0x01},
	{IMX208_TABLE_WAIT_MS, IMX208_WAIT_MS},
	{IMX208_TABLE_END, 0x00}
};

enum {
	IMX208_MODE_1920X1080,
};

static struct imx208_reg *mode_table[] = {
	[IMX208_MODE_1920X1080] = mode_1920x1080,
};

static inline void
msleep_range(unsigned int delay_base)
{
	usleep_range(delay_base*1000, delay_base*1000+500);
}

static inline void
imx208_get_frame_length_regs(struct imx208_reg *regs, u32 frame_length)
{
	regs->addr = IMX208_FRAME_LENGTH_ADDR_MSB;
	regs->val = (frame_length >> 8) & 0xff;
	(regs + 1)->addr = IMX208_FRAME_LENGTH_ADDR_LSB;
	(regs + 1)->val = (frame_length) & 0xff;
}

static inline void
imx208_get_coarse_time_regs(struct imx208_reg *regs, u32 coarse_time)
{
	regs->addr = IMX208_COARSE_TIME_ADDR_MSB;
	regs->val = (coarse_time >> 8) & 0xff;
	(regs + 1)->addr = IMX208_COARSE_TIME_ADDR_LSB;
	(regs + 1)->val = (coarse_time) & 0xff;
}

static inline void
imx208_get_gain_reg(struct imx208_reg *regs, u16 gain)
{
	regs->addr = IMX208_GAIN_ADDR;
	regs->val = gain;
}

static inline int
imx208_read_reg(struct imx208_info *info, u16 addr, u8 *val)
{
	return regmap_read(info->regmap, addr, (unsigned int *) val);
}

static int
imx208_write_reg(struct imx208_info *info, u16 addr, u8 val)
{
	int err;

	err = regmap_write(info->regmap, addr, val);

	if (err)
		pr_err("%s:i2c write failed, %x = %x\n",
			__func__, addr, val);

	return err;
}

static int
imx208_write_table(struct imx208_info *info,
				 const struct imx208_reg table[],
				 const struct imx208_reg override_list[],
				 int num_override_regs)
{
	int err;
	const struct imx208_reg *next;
	int i;
	u16 val;

	for (next = table; next->addr != IMX208_TABLE_END; next++) {
		if (next->addr == IMX208_TABLE_WAIT_MS) {
			msleep_range(next->val);
			continue;
		}

		val = next->val;

		/* When an override list is passed in, replace the reg */
		/* value to write if the reg is in the list            */
		if (override_list) {
			for (i = 0; i < num_override_regs; i++) {
				if (next->addr == override_list[i].addr) {
					val = override_list[i].val;
					break;
				}
			}
		}

		err = imx208_write_reg(info, next->addr, val);
		if (err) {
			pr_err("%s:imx208_write_table:%d", __func__, err);
			return err;
		}
	}
	return 0;
}

static int imx208_get_flash_cap(struct imx208_info *info)
{
	struct imx208_flash_control *fctl;

	dev_dbg(&info->i2c_client->dev, "%s: %p\n", __func__, info->pdata);
	if (info->pdata) {
		fctl = &info->pdata->flash_cap;
		dev_dbg(&info->i2c_client->dev,
			"edg: %x, st: %x, rpt: %x, dl: %x\n",
			fctl->edge_trig_en,
			fctl->start_edge,
			fctl->repeat,
			fctl->delay_frm);

		if (fctl->enable)
			return 0;
	}
	return -ENODEV;
}

static inline int imx208_set_flash_control(
	struct imx208_info *info, struct imx208_flash_control *fc)
{
	dev_dbg(&info->i2c_client->dev, "%s\n", __func__);
	return imx208_write_reg(info, 0x0802, 0x01);
}

static int
imx208_set_mode(struct imx208_info *info, struct imx208_mode *mode)
{
	int sensor_mode;
	int err;
	struct imx208_reg reg_list[8];

	pr_info("%s: xres %u yres %u framelength %u coarsetime %u gain %u\n",
			 __func__, mode->xres, mode->yres, mode->frame_length,
			 mode->coarse_time, mode->gain);

	if (mode->xres == 1920 && mode->yres == 1080) {
		sensor_mode = IMX208_MODE_1920X1080;
	} else {
		pr_err("%s: invalid resolution supplied to set mode %d %d\n",
			 __func__, mode->xres, mode->yres);
		return -EINVAL;
	}

	/* get a list of override regs for the asking frame length, */
	/* coarse integration time, and gain.                       */
	imx208_get_frame_length_regs(reg_list, mode->frame_length);
	imx208_get_coarse_time_regs(reg_list + 2, mode->coarse_time);
	imx208_get_gain_reg(reg_list + 4, mode->gain);

	err = imx208_write_table(info,
				mode_table[sensor_mode],
				reg_list, 5);
	if (err)
		return err;
	info->mode = sensor_mode;
	pr_info("[IMX208]: stream on.\n");
	return 0;
}

static int
imx208_get_status(struct imx208_info *info, u8 *dev_status)
{
	*dev_status = 0;
	return 0;
}

static int
imx208_set_frame_length(struct imx208_info *info, u32 frame_length,
						 bool group_hold)
{
	struct imx208_reg reg_list[2];
	int i = 0;
	int ret;

	imx208_get_frame_length_regs(reg_list, frame_length);

	if (group_hold) {
		ret = imx208_write_reg(info, 0x0104, 0x01);
		if (ret)
			return ret;
	}

	for (i = 0; i < 2; i++) {
		ret = imx208_write_reg(info, reg_list[i].addr,
			 reg_list[i].val);
		if (ret)
			return ret;
	}

	if (group_hold) {
		ret = imx208_write_reg(info, 0x0104, 0x0);
		if (ret)
			return ret;
	}

	return 0;
}

static int
imx208_set_coarse_time(struct imx208_info *info, u32 coarse_time,
						 bool group_hold)
{
	int ret;

	struct imx208_reg reg_list[2];
	int i = 0;

	imx208_get_coarse_time_regs(reg_list, coarse_time);

	if (group_hold) {
		ret = imx208_write_reg(info, 0x104, 0x01);
		if (ret)
			return ret;
	}

	for (i = 0; i < 2; i++) {
		ret = imx208_write_reg(info, reg_list[i].addr,
			 reg_list[i].val);
		if (ret)
			return ret;
	}

	if (group_hold) {
		ret = imx208_write_reg(info, 0x104, 0x0);
		if (ret)
			return ret;
	}
	return 0;
}

static int
imx208_set_gain(struct imx208_info *info, u16 gain, bool group_hold)
{
	int ret;
	struct imx208_reg reg_list;

	imx208_get_gain_reg(&reg_list, gain);

	if (group_hold) {
		ret = imx208_write_reg(info, 0x104, 0x1);
		if (ret)
			return ret;
	}

	ret = imx208_write_reg(info, reg_list.addr, reg_list.val);
	if (ret)
		return ret;

	if (group_hold) {
		ret = imx208_write_reg(info, 0x104, 0x0);
		if (ret)
			return ret;
	}
	return 0;
}

static int
imx208_set_group_hold(struct imx208_info *info, struct imx208_ae *ae)
{
	int ret;
	int count = 0;
	bool group_hold_enabled = false;

	if (ae->gain_enable)
		count++;
	if (ae->coarse_time_enable)
		count++;
	if (ae->frame_length_enable)
		count++;
	if (count >= 2)
		group_hold_enabled = true;

	if (group_hold_enabled) {
		ret = imx208_write_reg(info, 0x104, 0x1);
		if (ret)
			return ret;
	}

	if (ae->gain_enable)
		imx208_set_gain(info, ae->gain, false);
	if (ae->coarse_time_enable)
		imx208_set_coarse_time(info, ae->coarse_time, true);
	if (ae->frame_length_enable)
		imx208_set_frame_length(info, ae->frame_length, false);

	if (group_hold_enabled) {
		ret = imx208_write_reg(info, 0x104, 0x0);
		if (ret)
			return ret;
	}

	return 0;
}

static int imx208_get_sensor_id(struct imx208_info *info)
{
	int ret = 0;

	pr_info("%s\n", __func__);
	if (info->sensor_data.fuse_id_size)
		return 0;

	/* Note 1: If the sensor does not have power at this point
	Need to supply the power, e.g. by calling power on function */

	/*ret |= imx208_write_reg(info, 0x3B02, 0x00);
	ret |= imx208_write_reg(info, 0x3B00, 0x01);
	for (i = 0; i < 9; i++) {
		ret |= imx208_read_reg(info, 0x3B24 + i, &bak);
		info->sensor_data.fuse_id[i] = bak;
	}

	if (!ret)
		info->sensor_data.fuse_id_size = i;*/

	/* Note 2: Need to clean up any action carried out in Note 1 */

	return ret;
}

static void imx208_mclk_disable(struct imx208_info *info)
{
	dev_dbg(&info->i2c_client->dev, "%s: disable MCLK\n", __func__);
	clk_disable_unprepare(info->mclk);
}

static int imx208_mclk_enable(struct imx208_info *info)
{
	int err;
	unsigned long mclk_init_rate = 24000000;

	dev_dbg(&info->i2c_client->dev, "%s: enable MCLK with %lu Hz\n",
		__func__, mclk_init_rate);

	err = clk_set_rate(info->mclk, mclk_init_rate);
	if (!err)
		err = clk_prepare_enable(info->mclk);
	return err;
}

static long
imx208_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct imx208_info *info = file->private_data;

	switch (cmd) {
	case IMX208_IOCTL_SET_POWER:
		if (!info->pdata)
			break;
		if (arg && info->pdata->power_on) {
			err = imx208_mclk_enable(info);
			if (!err)
				err = info->pdata->power_on(&info->power);
			if (err < 0)
				imx208_mclk_disable(info);
		}
		if (!arg && info->pdata->power_off) {
			info->pdata->power_off(&info->power);
			imx208_mclk_disable(info);
		}
		break;
	case IMX208_IOCTL_SET_MODE:
	{
		struct imx208_mode mode;
		if (copy_from_user(&mode, (const void __user *)arg,
			sizeof(struct imx208_mode))) {
			pr_err("%s:Failed to get mode from user.\n", __func__);
			return -EFAULT;
		}
		return imx208_set_mode(info, &mode);
	}
	case IMX208_IOCTL_SET_FRAME_LENGTH:
		return imx208_set_frame_length(info, (u32)arg, true);
	case IMX208_IOCTL_SET_COARSE_TIME:
		return imx208_set_coarse_time(info, (u32)arg, true);
	case IMX208_IOCTL_SET_GAIN:
		return imx208_set_gain(info, (u16)arg, true);
	case IMX208_IOCTL_GET_STATUS:
	{
		u8 status;

		err = imx208_get_status(info, &status);
		if (err)
			return err;
		if (copy_to_user((void __user *)arg, &status, 1)) {
			pr_err("%s:Failed to copy status to user\n", __func__);
			return -EFAULT;
		}
		return 0;
	}
	case IMX208_IOCTL_GET_SENSORDATA:
	{
		err = imx208_get_sensor_id(info);

		if (err) {
			pr_err("%s:Failed to get fuse id info.\n", __func__);
			return err;
		}
		if (copy_to_user((void __user *)arg, &info->sensor_data,
				sizeof(struct imx208_sensordata))) {
			pr_info("%s:Failed to copy fuse id to user space\n",
				__func__);
			return -EFAULT;
		}
		return 0;
	}
	case IMX208_IOCTL_SET_GROUP_HOLD:
	{
		struct imx208_ae ae;
		if (copy_from_user(&ae, (const void __user *)arg,
			sizeof(struct imx208_ae))) {
			pr_info("%s:fail group hold\n", __func__);
			return -EFAULT;
		}
		return imx208_set_group_hold(info, &ae);
	}
	case IMX208_IOCTL_SET_FLASH_MODE:
	{
		struct imx208_flash_control values;

		dev_dbg(&info->i2c_client->dev,
			"IMX208_IOCTL_SET_FLASH_MODE\n");
		if (copy_from_user(&values,
			(const void __user *)arg,
			sizeof(struct imx208_flash_control))) {
			err = -EFAULT;
			break;
		}
		err = imx208_set_flash_control(info, &values);
		break;
	}
	case IMX208_IOCTL_GET_FLASH_CAP:
		err = imx208_get_flash_cap(info);
		break;
	default:
		pr_err("%s:unknown cmd.\n", __func__);
		err = -EINVAL;
	}

	return err;
}

static int imx208_power_on(struct imx208_power_rail *pw)
{
	int err;
	struct imx208_info *info = container_of(pw, struct imx208_info, power);

	if (unlikely(WARN_ON(!pw || !pw->iovdd || !pw->avdd || !pw->dvdd)))
		return -EFAULT;

	gpio_set_value(info->pdata->cam2_gpio, 0);
	usleep_range(10, 20);

	err = regulator_enable(pw->avdd);
	if (err)
		goto imx208_avdd_fail;

	err = regulator_enable(pw->dvdd);
	if (err)
		goto imx208_dvdd_fail;

	err = regulator_enable(pw->iovdd);
	if (err)
		goto imx208_iovdd_fail;

	usleep_range(1, 2);
	gpio_set_value(info->pdata->cam2_gpio, 1);

	usleep_range(300, 310);

	return 1;


imx208_iovdd_fail:
	regulator_disable(pw->dvdd);

imx208_dvdd_fail:
	regulator_disable(pw->avdd);

imx208_avdd_fail:
	pr_err("%s failed.\n", __func__);
	return -ENODEV;
}

static int imx208_power_off(struct imx208_power_rail *pw)
{
	struct imx208_info *info = container_of(pw, struct imx208_info, power);

	if (unlikely(WARN_ON(!pw || !pw->iovdd || !pw->avdd || !pw->dvdd)))
		return -EFAULT;

	usleep_range(1, 2);
	gpio_set_value(info->pdata->cam2_gpio, 0);
	usleep_range(1, 2);

	regulator_disable(pw->iovdd);
	regulator_disable(pw->dvdd);
	regulator_disable(pw->avdd);

	return 0;
}

static int
imx208_open(struct inode *inode, struct file *file)
{
	struct miscdevice	*miscdev = file->private_data;
	struct imx208_info *info;

	info = container_of(miscdev, struct imx208_info, miscdev_info);
	/* check if the device is in use */
	if (atomic_xchg(&info->in_use, 1)) {
		pr_info("%s:BUSY!\n", __func__);
		return -EBUSY;
	}

	file->private_data = info;

	return 0;
}

static int
imx208_release(struct inode *inode, struct file *file)
{
	struct imx208_info *info = file->private_data;

	file->private_data = NULL;

	/* warn if device is already released */
	WARN_ON(!atomic_xchg(&info->in_use, 0));
	return 0;
}

static int imx208_power_put(struct imx208_power_rail *pw)
{
	if (unlikely(!pw))
		return -EFAULT;

	if (likely(pw->avdd))
		regulator_put(pw->avdd);

	if (likely(pw->iovdd))
		regulator_put(pw->iovdd);

	if (likely(pw->dvdd))
		regulator_put(pw->dvdd);

	pw->avdd = NULL;
	pw->iovdd = NULL;
	pw->dvdd = NULL;

	return 0;
}

static int imx208_regulator_get(struct imx208_info *info,
	struct regulator **vreg, char vreg_name[])
{
	struct regulator *reg = NULL;
	int err = 0;

	reg = regulator_get(&info->i2c_client->dev, vreg_name);
	if (unlikely(IS_ERR(reg))) {
		dev_err(&info->i2c_client->dev, "%s %s ERR: %d\n",
			__func__, vreg_name, (int)reg);
		err = PTR_ERR(reg);
		reg = NULL;
	} else
		dev_dbg(&info->i2c_client->dev, "%s: %s\n",
			__func__, vreg_name);

	*vreg = reg;
	return err;
}

static int imx208_power_get(struct imx208_info *info)
{
	struct imx208_power_rail *pw = &info->power;
	int err = 0;

	err |= imx208_regulator_get(info, &pw->avdd, "vana"); /* ananlog 2.7v */
	err |= imx208_regulator_get(info, &pw->dvdd, "vdig"); /* digital 1.2v */
	err |= imx208_regulator_get(info, &pw->iovdd, "vif"); /* IO 1.8v */

	return err;
}

static const struct file_operations imx208_fileops = {
	.owner = THIS_MODULE,
	.open = imx208_open,
	.unlocked_ioctl = imx208_ioctl,
	.release = imx208_release,
};

static struct miscdevice imx208_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "imx208",
	.fops = &imx208_fileops,
};

static struct of_device_id imx208_of_match[] = {
	{ .compatible = "nvidia,imx208", },
	{ },
};

MODULE_DEVICE_TABLE(of, imx208_of_match);

static struct imx208_platform_data *imx208_parse_dt(struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node;
	struct imx208_platform_data *board_info_pdata;
	const struct of_device_id *match;

	match = of_match_device(imx208_of_match, &client->dev);
	if (!match) {
		dev_err(&client->dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_info_pdata = devm_kzalloc(&client->dev, sizeof(*board_info_pdata),
			GFP_KERNEL);
	if (!board_info_pdata) {
		dev_err(&client->dev, "Failed to allocate pdata\n");
		return NULL;
	}

	board_info_pdata->cam2_gpio = of_get_named_gpio(np, "cam1-gpios", 0);
	board_info_pdata->ext_reg = of_property_read_bool(np, "nvidia,ext_reg");

	board_info_pdata->power_on = imx208_power_on;
	board_info_pdata->power_off = imx208_power_off;

	return board_info_pdata;
}

static int
imx208_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct imx208_info *info;
	int err;
	const char *mclk_name;

	pr_err("[IMX208]: probing sensor.\n");

	info = devm_kzalloc(&client->dev,
			sizeof(struct imx208_info), GFP_KERNEL);
	if (!info) {
		pr_err("%s:Unable to allocate memory!\n", __func__);
		return -ENOMEM;
	}

	info->regmap = devm_regmap_init_i2c(client, &sensor_regmap_config);
	if (IS_ERR(info->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(info->regmap));
		return -ENODEV;
	}

	if (client->dev.of_node)
		info->pdata = imx208_parse_dt(client);
	else
		info->pdata = client->dev.platform_data;

	if (!info->pdata) {
		pr_err("[IMX208]:%s:Unable to get platform data\n", __func__);
		return -EFAULT;
	}

	info->i2c_client = client;
	atomic_set(&info->in_use, 0);
	info->mode = -1;

	mclk_name = info->pdata->mclk_name ?
		    info->pdata->mclk_name : "default_mclk";
	info->mclk = devm_clk_get(&client->dev, mclk_name);
	if (IS_ERR(info->mclk)) {
		dev_err(&client->dev, "%s: unable to get clock %s\n",
			__func__, mclk_name);
		return PTR_ERR(info->mclk);
	}

	imx208_power_get(info);

	memcpy(&info->miscdev_info,
		&imx208_device,
		sizeof(struct miscdevice));

	err = misc_register(&info->miscdev_info);
	if (err) {
		pr_err("%s:Unable to register misc device!\n", __func__);
		goto imx208_probe_fail;
	}

	i2c_set_clientdata(client, info);

	mutex_init(&info->imx208_camera_lock);
	pr_err("[IMX208]: end of probing sensor.\n");
	return 0;

imx208_probe_fail:
	imx208_power_put(&info->power);

	return err;
}

static int
imx208_remove(struct i2c_client *client)
{
	struct imx208_info *info;
	info = i2c_get_clientdata(client);
	misc_deregister(&imx208_device);
	mutex_destroy(&info->imx208_camera_lock);

	imx208_power_put(&info->power);

	return 0;
}

static const struct i2c_device_id imx208_id[] = {
	{ "imx208", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, imx208_id);

static struct i2c_driver imx208_i2c_driver = {
	.driver = {
		.name = "imx208",
		.owner = THIS_MODULE,
	},
	.probe = imx208_probe,
	.remove = imx208_remove,
	.id_table = imx208_id,
};

static int __init imx208_init(void)
{
	pr_info("[IMX208] sensor driver loading\n");
	return i2c_add_driver(&imx208_i2c_driver);
}

static void __exit imx208_exit(void)
{
	i2c_del_driver(&imx208_i2c_driver);
}

module_init(imx208_init);
module_exit(imx208_exit);
