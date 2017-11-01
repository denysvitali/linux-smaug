/*
 * Copyright (c) 2011 - 2014, NVIDIA CORPORATION.  All rights reserved.
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
#ifndef THERM_GK20A_H
#define THERM_GK20A_H

/* priority for EXT_THERM_0 event set to highest */
#define NV_THERM_EVT_EXT_THERM_0_INIT	0x3000100
#define NV_THERM_EVT_EXT_THERM_1_INIT	0x2000200
#define NV_THERM_EVT_EXT_THERM_2_INIT	0x1000300
/* configures the thermal events that may cause clock slowdown */
#define NV_THERM_USE_A_INIT	0x7

int gk20a_init_therm_support(struct gk20a *g);

#endif /* THERM_GK20A_H */
