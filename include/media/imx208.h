/**
 * Copyright (c) 2014-2015, NVIDIA Corporation.  All rights reserved.
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

#ifndef __IMX208_H__
#define __IMX208_H__

#include <linux/ioctl.h>  /* For IOCTL macros */
#include <media/nvc.h>
#include <media/nvc_image.h>

#define IMX208_IOCTL_SET_MODE		_IOW('o', 1, struct imx208_mode)
#define IMX208_IOCTL_GET_STATUS		_IOR('o', 2, __u8)
#define IMX208_IOCTL_SET_FRAME_LENGTH	_IOW('o', 3, __u32)
#define IMX208_IOCTL_SET_COARSE_TIME	_IOW('o', 4, __u32)
#define IMX208_IOCTL_SET_GAIN		_IOW('o', 5, __u16)
#define IMX208_IOCTL_GET_SENSORDATA	_IOR('o', 6, struct imx208_sensordata)
#define IMX208_IOCTL_SET_GROUP_HOLD	_IOW('o', 7, struct imx208_ae)
#define IMX208_IOCTL_SET_POWER		_IOW('o', 20, __u32)
#define IMX208_IOCTL_GET_FLASH_CAP	_IOR('o', 30, __u32)
#define IMX208_IOCTL_SET_FLASH_MODE	_IOW('o', 31, \
						struct imx208_flash_control)

struct imx208_mode {
	int xres;
	int yres;
	__u32 frame_length;
	__u32 coarse_time;
	__u16 gain;
};

struct imx208_ae {
	__u32 frame_length;
	__u8  frame_length_enable;
	__u32 coarse_time;
	__u8  coarse_time_enable;
	__s32 gain;
	__u8  gain_enable;
};

struct imx208_sensordata {
	__u32 fuse_id_size;
	__u8  fuse_id[16];
};

struct imx208_flash_control {
	u8 enable;
	u8 edge_trig_en;
	u8 start_edge;
	u8 repeat;
	u16 delay_frm;
};


#ifdef __KERNEL__
struct imx208_power_rail {
	struct regulator *dvdd;
	struct regulator *avdd;
	struct regulator *iovdd;
};

struct imx208_platform_data {
	struct imx208_flash_control flash_cap;
	const char *mclk_name; /* NULL for default default_mclk */
	unsigned int cam2_gpio;
	bool ext_reg;
	int (*power_on)(struct imx208_power_rail *pw);
	int (*power_off)(struct imx208_power_rail *pw);
};
#endif /* __KERNEL__ */

#endif  /* __IMX208_H__ */
