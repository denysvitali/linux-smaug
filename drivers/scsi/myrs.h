/*
 * Linux Driver for Mylex DAC960/AcceleRAID/eXtremeRAID PCI RAID Controllers
 *
 * This driver supports the newer, SCSI-based firmware interface only.
 *
 * Copyright 2018 Hannes Reinecke, SUSE Linux GmbH <hare@suse.com>
 *
 * Based on the original DAC960 driver, which has
 * Copyright 1998-2001 by Leonard N. Zubkoff <lnz@dandelion.com>
 * Portions Copyright 2002 by Mylex (An IBM Business Unit)
 *
 * This program is free software; you may redistribute and/or modify it under
 * the terms of the GNU General Public License Version 2 as published by the
 *  Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for complete details.
 */

#ifndef _MYRS_H
#define _MYRS_H

#define MYRS_MAILBOX_TIMEOUT 1000000

#define MYRS_DCMD_TAG 1
#define MYRS_MCMD_TAG 2

#define MYRS_LINE_BUFFER_SIZE 128

#define MYRS_PRIMARY_MONITOR_INTERVAL (10 * HZ)
#define MYRS_SECONDARY_MONITOR_INTERVAL (60 * HZ)

/* Maximum number of Scatter/Gather Segments supported */
#define MYRS_SG_LIMIT		128

/*
 * Number of Command and Status Mailboxes used by the
 * DAC960 V2 Firmware Memory Mailbox Interface.
 */
#define MYRS_MAX_CMD_MBOX		512
#define MYRS_MAX_STAT_MBOX		512

#define MYRS_DCDB_SIZE			16
#define MYRS_SENSE_SIZE			14

/*
  Define the DAC960 V2 Firmware Command Opcodes.
*/

typedef enum
{
	DAC960_V2_MemCopy =				0x01,
	DAC960_V2_SCSI_10_Passthru =			0x02,
	DAC960_V2_SCSI_255_Passthru =			0x03,
	DAC960_V2_SCSI_10 =				0x04,
	DAC960_V2_SCSI_256 =				0x05,
	DAC960_V2_IOCTL =				0x20
}
__attribute__ ((packed))
myrs_cmd_opcode;


/*
  Define the DAC960 V2 Firmware IOCTL Opcodes.
*/

typedef enum
{
	DAC960_V2_GetControllerInfo =			0x01,
	DAC960_V2_GetLogicalDeviceInfoValid =		0x03,
	DAC960_V2_GetPhysicalDeviceInfoValid =		0x05,
	DAC960_V2_GetHealthStatus =			0x11,
	DAC960_V2_GetEvent =				0x15,
	DAC960_V2_StartDiscovery =			0x81,
	DAC960_V2_SetDeviceState =			0x82,
	DAC960_V2_InitPhysicalDeviceStart =		0x84,
	DAC960_V2_InitPhysicalDeviceStop =		0x85,
	DAC960_V2_InitLogicalDeviceStart =		0x86,
	DAC960_V2_InitLogicalDeviceStop =		0x87,
	DAC960_V2_RebuildDeviceStart =			0x88,
	DAC960_V2_RebuildDeviceStop =			0x89,
	DAC960_V2_MakeConsistencDataStart =		0x8A,
	DAC960_V2_MakeConsistencDataStop =		0x8B,
	DAC960_V2_ConsistencyCheckStart =		0x8C,
	DAC960_V2_ConsistencyCheckStop =		0x8D,
	DAC960_V2_SetMemoryMailbox =			0x8E,
	DAC960_V2_ResetDevice =				0x90,
	DAC960_V2_FlushDeviceData =			0x91,
	DAC960_V2_PauseDevice =				0x92,
	DAC960_V2_UnPauseDevice =			0x93,
	DAC960_V2_LocateDevice =			0x94,
	DAC960_V2_CreateNewConfiguration =		0xC0,
	DAC960_V2_DeleteLogicalDevice =			0xC1,
	DAC960_V2_ReplaceInternalDevice =		0xC2,
	DAC960_V2_RenameLogicalDevice =			0xC3,
	DAC960_V2_AddNewConfiguration =			0xC4,
	DAC960_V2_TranslatePhysicalToLogicalDevice =	0xC5,
	DAC960_V2_ClearConfiguration =			0xCA,
}
__attribute__ ((packed))
myrs_ioctl_opcode;


/*
  Define the DAC960 V2 Firmware Command Status Codes.
*/

#define DAC960_V2_NormalCompletion		0x00
#define DAC960_V2_AbnormalCompletion		0x02
#define DAC960_V2_DeviceBusy			0x08
#define DAC960_V2_DeviceNonresponsive		0x0E
#define DAC960_V2_DeviceNonresponsive2		0x0F
#define DAC960_V2_DeviceRevervationConflict	0x18


/*
  Define the DAC960 V2 Firmware Memory Type structure.
*/

typedef struct myrs_mem_type_s
{
	enum {
		DAC960_V2_MemoryType_Reserved =		0x00,
		DAC960_V2_MemoryType_DRAM =		0x01,
		DAC960_V2_MemoryType_EDRAM =		0x02,
		DAC960_V2_MemoryType_EDO =		0x03,
		DAC960_V2_MemoryType_SDRAM =		0x04,
		DAC960_V2_MemoryType_Last =		0x1F
	} __attribute__ ((packed)) MemoryType:5;	/* Byte 0 Bits 0-4 */
	bool rsvd:1;					/* Byte 0 Bit 5 */
	bool MemoryParity:1;				/* Byte 0 Bit 6 */
	bool MemoryECC:1;				/* Byte 0 Bit 7 */
} myrs_mem_type;


/*
  Define the DAC960 V2 Firmware Processor Type structure.
*/

typedef enum
{
	DAC960_V2_ProcessorType_i960CA =		0x01,
	DAC960_V2_ProcessorType_i960RD =		0x02,
	DAC960_V2_ProcessorType_i960RN =		0x03,
	DAC960_V2_ProcessorType_i960RP =		0x04,
	DAC960_V2_ProcessorType_NorthBay =		0x05,
	DAC960_V2_ProcessorType_StrongArm =		0x06,
	DAC960_V2_ProcessorType_i960RM =		0x07
}
__attribute__ ((packed))
myrs_cpu_type;


/*
  Define the DAC960 V2 Firmware Get Controller Info reply structure.
*/

typedef struct myrs_ctlr_info_s
{
	unsigned char :8;				/* Byte 0 */
	enum {
		DAC960_V2_SCSI_Bus =			0x00,
		DAC960_V2_Fibre_Bus =			0x01,
		DAC960_V2_PCI_Bus =			0x03
	} __attribute__ ((packed)) BusInterfaceType;	/* Byte 1 */
	enum {
		DAC960_V2_DAC960E =			0x01,
		DAC960_V2_DAC960M =			0x08,
		DAC960_V2_DAC960PD =			0x10,
		DAC960_V2_DAC960PL =			0x11,
		DAC960_V2_DAC960PU =			0x12,
		DAC960_V2_DAC960PE =			0x13,
		DAC960_V2_DAC960PG =			0x14,
		DAC960_V2_DAC960PJ =			0x15,
		DAC960_V2_DAC960PTL0 =			0x16,
		DAC960_V2_DAC960PR =			0x17,
		DAC960_V2_DAC960PRL =			0x18,
		DAC960_V2_DAC960PT =			0x19,
		DAC960_V2_DAC1164P =			0x1A,
		DAC960_V2_DAC960PTL1 =			0x1B,
		DAC960_V2_EXR2000P =			0x1C,
		DAC960_V2_EXR3000P =			0x1D,
		DAC960_V2_AcceleRAID352 =		0x1E,
		DAC960_V2_AcceleRAID170 =		0x1F,
		DAC960_V2_AcceleRAID160 =		0x20,
		DAC960_V2_DAC960S =			0x60,
		DAC960_V2_DAC960SU =			0x61,
		DAC960_V2_DAC960SX =			0x62,
		DAC960_V2_DAC960SF =			0x63,
		DAC960_V2_DAC960SS =			0x64,
		DAC960_V2_DAC960FL =			0x65,
		DAC960_V2_DAC960LL =			0x66,
		DAC960_V2_DAC960FF =			0x67,
		DAC960_V2_DAC960HP =			0x68,
		DAC960_V2_RAIDBRICK =			0x69,
		DAC960_V2_METEOR_FL =			0x6A,
		DAC960_V2_METEOR_FF =			0x6B
	} __attribute__ ((packed)) ControllerType;	/* Byte 2 */
	unsigned char :8;				/* Byte 3 */
	unsigned short BusInterfaceSpeedMHz;		/* Bytes 4-5 */
	unsigned char BusWidthBits;			/* Byte 6 */
	unsigned char FlashCodeTypeOrProductID;		/* Byte 7 */
	unsigned char NumberOfHostPortsPresent;		/* Byte 8 */
	unsigned char Reserved1[7];			/* Bytes 9-15 */
	unsigned char BusInterfaceName[16];		/* Bytes 16-31 */
	unsigned char ControllerName[16];		/* Bytes 32-47 */
	unsigned char Reserved2[16];			/* Bytes 48-63 */
	/* Firmware Release Information */
	unsigned char FirmwareMajorVersion;		/* Byte 64 */
	unsigned char FirmwareMinorVersion;		/* Byte 65 */
	unsigned char FirmwareTurnNumber;		/* Byte 66 */
	unsigned char FirmwareBuildNumber;		/* Byte 67 */
	unsigned char FirmwareReleaseDay;		/* Byte 68 */
	unsigned char FirmwareReleaseMonth;		/* Byte 69 */
	unsigned char FirmwareReleaseYearHigh2Digits;	/* Byte 70 */
	unsigned char FirmwareReleaseYearLow2Digits;	/* Byte 71 */
	/* Hardware Release Information */
	unsigned char HardwareRevision;			/* Byte 72 */
	unsigned int :24;				/* Bytes 73-75 */
	unsigned char HardwareReleaseDay;		/* Byte 76 */
	unsigned char HardwareReleaseMonth;		/* Byte 77 */
	unsigned char HardwareReleaseYearHigh2Digits;	/* Byte 78 */
	unsigned char HardwareReleaseYearLow2Digits;	/* Byte 79 */
	/* Hardware Manufacturing Information */
	unsigned char ManufacturingBatchNumber;		/* Byte 80 */
	unsigned char :8;				/* Byte 81 */
	unsigned char ManufacturingPlantNumber;		/* Byte 82 */
	unsigned char :8;				/* Byte 83 */
	unsigned char HardwareManufacturingDay;		/* Byte 84 */
	unsigned char HardwareManufacturingMonth;	/* Byte 85 */
	unsigned char HardwareManufacturingYearHigh2Digits;	/* Byte 86 */
	unsigned char HardwareManufacturingYearLow2Digits;	/* Byte 87 */
	unsigned char MaximumNumberOfPDDperXLD;		/* Byte 88 */
	unsigned char MaximumNumberOfILDperXLD;		/* Byte 89 */
	unsigned short NonvolatileMemorySizeKB;		/* Bytes 90-91 */
	unsigned char MaximumNumberOfXLD;		/* Byte 92 */
	unsigned int :24;				/* Bytes 93-95 */
	/* Unique Information per Controller */
	unsigned char ControllerSerialNumber[16];	/* Bytes 96-111 */
	unsigned char Reserved3[16];			/* Bytes 112-127 */
	/* Vendor Information */
	unsigned int :24;				/* Bytes 128-130 */
	unsigned char OEM_Code;				/* Byte 131 */
	unsigned char VendorName[16];			/* Bytes 132-147 */
	/* Other Physical/Controller/Operation Information */
	bool BBU_Present:1;				/* Byte 148 Bit 0 */
	bool ActiveActiveClusteringMode:1;		/* Byte 148 Bit 1 */
	unsigned char :6;				/* Byte 148 Bits 2-7 */
	unsigned char :8;				/* Byte 149 */
	unsigned short :16;				/* Bytes 150-151 */
	/* Physical Device Scan Information */
	bool pscan_active:1;				/* Byte 152 Bit 0 */
	unsigned char :7;				/* Byte 152 Bits 1-7 */
	unsigned char pscan_chan;			/* Byte 153 */
	unsigned char pscan_target;			/* Byte 154 */
	unsigned char pscan_lun;			/* Byte 155 */
	/* Maximum Command Data Transfer Sizes */
	unsigned short max_transfer_size;		/* Bytes 156-157 */
	unsigned short max_sge;				/* Bytes 158-159 */
	/* Logical/Physical Device Counts */
	unsigned short ldev_present;			/* Bytes 160-161 */
	unsigned short ldev_critical;			/* Bytes 162-163 */
	unsigned short ldev_offline;			/* Bytes 164-165 */
	unsigned short pdev_present;			/* Bytes 166-167 */
	unsigned short pdisk_present;			/* Bytes 168-169 */
	unsigned short pdisk_critical;			/* Bytes 170-171 */
	unsigned short pdisk_offline;			/* Bytes 172-173 */
	unsigned short max_tcq;				/* Bytes 174-175 */
	/* Channel and Target ID Information */
	unsigned char physchan_present;			/* Byte 176 */
	unsigned char virtchan_present;			/* Byte 177 */
	unsigned char physchan_max;			/* Byte 178 */
	unsigned char virtchan_max;			/* Byte 179 */
	unsigned char max_targets[16];			/* Bytes 180-195 */
	unsigned char Reserved4[12];			/* Bytes 196-207 */
	/* Memory/Cache Information */
	unsigned short MemorySizeMB;			/* Bytes 208-209 */
	unsigned short CacheSizeMB;			/* Bytes 210-211 */
	unsigned int ValidCacheSizeInBytes;		/* Bytes 212-215 */
	unsigned int DirtyCacheSizeInBytes;		/* Bytes 216-219 */
	unsigned short MemorySpeedMHz;			/* Bytes 220-221 */
	unsigned char MemoryDataWidthBits;		/* Byte 222 */
	myrs_mem_type MemoryType;			/* Byte 223 */
	unsigned char CacheMemoryTypeName[16];		/* Bytes 224-239 */
	/* Execution Memory Information */
	unsigned short ExecutionMemorySizeMB;		/* Bytes 240-241 */
	unsigned short ExecutionL2CacheSizeMB;		/* Bytes 242-243 */
	unsigned char Reserved5[8];			/* Bytes 244-251 */
	unsigned short ExecutionMemorySpeedMHz;		/* Bytes 252-253 */
	unsigned char ExecutionMemoryDataWidthBits;	/* Byte 254 */
	myrs_mem_type ExecutionMemoryType;		/* Byte 255 */
	unsigned char ExecutionMemoryTypeName[16];	/* Bytes 256-271 */
	/* First CPU Type Information */
	unsigned short FirstProcessorSpeedMHz;		/* Bytes 272-273 */
	myrs_cpu_type FirstProcessorType;		/* Byte 274 */
	unsigned char FirstProcessorCount;		/* Byte 275 */
	unsigned char Reserved6[12];			/* Bytes 276-287 */
	unsigned char FirstProcessorName[16];		/* Bytes 288-303 */
	/* Second CPU Type Information */
	unsigned short SecondProcessorSpeedMHz;		/* Bytes 304-305 */
	myrs_cpu_type SecondProcessorType;		/* Byte 306 */
	unsigned char SecondProcessorCount;		/* Byte 307 */
	unsigned char Reserved7[12];			/* Bytes 308-319 */
	unsigned char SecondProcessorName[16];		/* Bytes 320-335 */
	/* Debugging/Profiling/Command Time Tracing Information */
	unsigned short CurrentProfilingDataPageNumber;	/* Bytes 336-337 */
	unsigned short ProgramsAwaitingProfilingData;		/* Bytes 338-339 */
	unsigned short CurrentCommandTimeTraceDataPageNumber;	/* Bytes 340-341 */
	unsigned short ProgramsAwaitingCommandTimeTraceData;	/* Bytes 342-343 */
	unsigned char Reserved8[8];			/* Bytes 344-351 */
	/* Error Counters on Physical Devices */
	unsigned short pdev_bus_resets;			/* Bytes 352-353 */
	unsigned short pdev_parity_errors;		/* Bytes 355-355 */
	unsigned short pdev_soft_errors;		/* Bytes 356-357 */
	unsigned short pdev_cmds_failed;		/* Bytes 358-359 */
	unsigned short pdev_misc_errors;		/* Bytes 360-361 */
	unsigned short pdev_cmd_timeouts;		/* Bytes 362-363 */
	unsigned short pdev_sel_timeouts;		/* Bytes 364-365 */
	unsigned short pdev_retries_done;		/* Bytes 366-367 */
	unsigned short pdev_aborts_done;		/* Bytes 368-369 */
	unsigned short pdev_host_aborts_done;		/* Bytes 370-371 */
	unsigned short pdev_predicted_failures;		/* Bytes 372-373 */
	unsigned short pdev_host_cmds_failed;		/* Bytes 374-375 */
	unsigned short pdev_hard_errors;		/* Bytes 376-377 */
	unsigned char Reserved9[6];			/* Bytes 378-383 */
	/* Error Counters on Logical Devices */
	unsigned short ldev_soft_errors;		/* Bytes 384-385 */
	unsigned short ldev_cmds_failed;		/* Bytes 386-387 */
	unsigned short ldev_host_aborts_done;		/* Bytes 388-389 */
	unsigned short :16;				/* Bytes 390-391 */
	/* Error Counters on Controller */
	unsigned short ctlr_mem_errors;			/* Bytes 392-393 */
	unsigned short ctlr_host_aborts_done;		/* Bytes 394-395 */
	unsigned int :32;				/* Bytes 396-399 */
	/* Long Duration Activity Information */
	unsigned short bg_init_active;			/* Bytes 400-401 */
	unsigned short ldev_init_active;		/* Bytes 402-403 */
	unsigned short pdev_init_active;		/* Bytes 404-405 */
	unsigned short cc_active;			/* Bytes 406-407 */
	unsigned short rbld_active;			/* Bytes 408-409 */
	unsigned short exp_active;			/* Bytes 410-411 */
	unsigned short patrol_active;			/* Bytes 412-413 */
	unsigned short :16;				/* Bytes 414-415 */
	/* Flash ROM Information */
	unsigned char flash_type;			/* Byte 416 */
	unsigned char :8;				/* Byte 417 */
	unsigned short flash_size_MB;			/* Bytes 418-419 */
	unsigned int flash_limit;			/* Bytes 420-423 */
	unsigned int flash_count;			/* Bytes 424-427 */
	unsigned int :32;				/* Bytes 428-431 */
	unsigned char flash_type_name[16];		/* Bytes 432-447 */
	/* Firmware Run Time Information */
	unsigned char rbld_rate;			/* Byte 448 */
	unsigned char bg_init_rate;			/* Byte 449 */
	unsigned char fg_init_rate;			/* Byte 450 */
	unsigned char cc_rate;				/* Byte 451 */
	unsigned int :32;				/* Bytes 452-455 */
	unsigned int MaximumDP;				/* Bytes 456-459 */
	unsigned int FreeDP;				/* Bytes 460-463 */
	unsigned int MaximumIOP;			/* Bytes 464-467 */
	unsigned int FreeIOP;				/* Bytes 468-471 */
	unsigned short MaximumCombLengthInBlocks;	/* Bytes 472-473 */
	unsigned short NumberOfConfigurationGroups;	/* Bytes 474-475 */
	bool InstallationAbortStatus:1;			/* Byte 476 Bit 0 */
	bool MaintenanceModeStatus:1;			/* Byte 476 Bit 1 */
	unsigned int :24;				/* Bytes 476-479 */
	unsigned char Reserved10[32];			/* Bytes 480-511 */
	unsigned char Reserved11[512];			/* Bytes 512-1023 */
} myrs_ctlr_info;


/*
  Define the DAC960 V2 Firmware Device State type.
*/

typedef enum
{
	DAC960_V2_Device_Unconfigured =		0x00,
	DAC960_V2_Device_Online =		0x01,
	DAC960_V2_Device_Rebuild =		0x03,
	DAC960_V2_Device_Missing =		0x04,
	DAC960_V2_Device_SuspectedCritical =	0x05,
	DAC960_V2_Device_Offline =		0x08,
	DAC960_V2_Device_Critical =		0x09,
	DAC960_V2_Device_SuspectedDead =	0x0C,
	DAC960_V2_Device_CommandedOffline =	0x10,
	DAC960_V2_Device_Standby =		0x21,
	DAC960_V2_Device_InvalidState =		0xFF
}
__attribute__ ((packed))
myrs_devstate;

/*
 * Define the DAC960 V2 RAID Levels
 */
typedef enum {
	DAC960_V2_RAID_Level0 =		0x0,     /* RAID 0 */
	DAC960_V2_RAID_Level1 =		0x1,     /* RAID 1 */
	DAC960_V2_RAID_Level3 =		0x3,     /* RAID 3 right asymmetric parity */
	DAC960_V2_RAID_Level5 =		0x5,     /* RAID 5 right asymmetric parity */
	DAC960_V2_RAID_Level6 =		0x6,     /* RAID 6 (Mylex RAID 6) */
	DAC960_V2_RAID_JBOD =		0x7,     /* RAID 7 (JBOD) */
	DAC960_V2_RAID_NewSpan =	0x8,     /* New Mylex SPAN */
	DAC960_V2_RAID_Level3F =	0x9,     /* RAID 3 fixed parity */
	DAC960_V2_RAID_Level3L =	0xb,     /* RAID 3 left symmetric parity */
	DAC960_V2_RAID_Span =		0xc,     /* current spanning implementation */
	DAC960_V2_RAID_Level5L =	0xd,     /* RAID 5 left symmetric parity */
	DAC960_V2_RAID_LevelE =		0xe,     /* RAID E (concatenation) */
	DAC960_V2_RAID_Physical =	0xf,     /* physical device */
}
__attribute__ ((packed))
myrs_raid_level;

typedef enum {
	DAC960_V2_StripeSize_0 =	0x0,	/* no stripe (RAID 1, RAID 7, etc) */
	DAC960_V2_StripeSize_512b =	0x1,
	DAC960_V2_StripeSize_1k =	0x2,
	DAC960_V2_StripeSize_2k =	0x3,
	DAC960_V2_StripeSize_4k =	0x4,
	DAC960_V2_StripeSize_8k =	0x5,
	DAC960_V2_StripeSize_16k =	0x6,
	DAC960_V2_StripeSize_32k =	0x7,
	DAC960_V2_StripeSize_64k =	0x8,
	DAC960_V2_StripeSize_128k =	0x9,
	DAC960_V2_StripeSize_256k =	0xa,
	DAC960_V2_StripeSize_512k =	0xb,
	DAC960_V2_StripeSize_1m =	0xc,
} __attribute__ ((packed))
myrs_stripe_size;

typedef enum {
	DAC960_V2_Cacheline_ZERO =	0x0,	/* caching cannot be enabled */
	DAC960_V2_Cacheline_512b =	0x1,
	DAC960_V2_Cacheline_1k =	0x2,
	DAC960_V2_Cacheline_2k =	0x3,
	DAC960_V2_Cacheline_4k =	0x4,
	DAC960_V2_Cacheline_8k =	0x5,
	DAC960_V2_Cacheline_16k =	0x6,
	DAC960_V2_Cacheline_32k =	0x7,
	DAC960_V2_Cacheline_64k =	0x8,
} __attribute__ ((packed))
myrs_cacheline_size;

/*
  Define the DAC960 V2 Firmware Get Logical Device Info reply structure.
*/

typedef struct myrs_ldev_info_s
{
	unsigned char :8;				/* Byte 0 */
	unsigned char Channel;				/* Byte 1 */
	unsigned char TargetID;				/* Byte 2 */
	unsigned char LogicalUnit;			/* Byte 3 */
	myrs_devstate State;				/* Byte 4 */
	unsigned char RAIDLevel;			/* Byte 5 */
	myrs_stripe_size StripeSize;			/* Byte 6 */
	myrs_cacheline_size CacheLineSize;		/* Byte 7 */
	struct {
		enum {
			DAC960_V2_ReadCacheDisabled =		0x0,
			DAC960_V2_ReadCacheEnabled =		0x1,
			DAC960_V2_ReadAheadEnabled =		0x2,
			DAC960_V2_IntelligentReadAheadEnabled =	0x3,
			DAC960_V2_ReadCache_Last =		0x7
		} __attribute__ ((packed)) ReadCache:3;	/* Byte 8 Bits 0-2 */
		enum {
			DAC960_V2_WriteCacheDisabled =		0x0,
			DAC960_V2_LogicalDeviceReadOnly =	0x1,
			DAC960_V2_WriteCacheEnabled =		0x2,
			DAC960_V2_IntelligentWriteCacheEnabled = 0x3,
			DAC960_V2_WriteCache_Last =		0x7
		} __attribute__ ((packed)) WriteCache:3; /* Byte 8 Bits 3-5 */
		bool rsvd1:1;				/* Byte 8 Bit 6 */
		bool ldev_init_done:1;			/* Byte 8 Bit 7 */
	} ldev_control;					/* Byte 8 */
	/* Logical Device Operations Status */
	bool cc_active:1;				/* Byte 9 Bit 0 */
	bool rbld_active:1;				/* Byte 9 Bit 1 */
	bool bg_init_active:1;				/* Byte 9 Bit 2 */
	bool fg_init_active:1;				/* Byte 9 Bit 3 */
	bool migration_active:1;			/* Byte 9 Bit 4 */
	bool patrol_active:1;				/* Byte 9 Bit 5 */
	unsigned char rsvd2:2;				/* Byte 9 Bits 6-7 */
	unsigned char RAID5WriteUpdate;			/* Byte 10 */
	unsigned char RAID5Algorithm;			/* Byte 11 */
	unsigned short ldev_num;			/* Bytes 12-13 */
	/* BIOS Info */
	bool BIOSDisabled:1;				/* Byte 14 Bit 0 */
	bool CDROMBootEnabled:1;			/* Byte 14 Bit 1 */
	bool DriveCoercionEnabled:1;			/* Byte 14 Bit 2 */
	bool WriteSameDisabled:1;			/* Byte 14 Bit 3 */
	bool HBA_ModeEnabled:1;				/* Byte 14 Bit 4 */
	enum {
		DAC960_V2_Geometry_128_32 =		0x0,
		DAC960_V2_Geometry_255_63 =		0x1,
		DAC960_V2_Geometry_Reserved1 =		0x2,
		DAC960_V2_Geometry_Reserved2 =		0x3
	} __attribute__ ((packed)) DriveGeometry:2;	/* Byte 14 Bits 5-6 */
	bool SuperReadAheadEnabled:1;			/* Byte 14 Bit 7 */
	unsigned char rsvd3:8;				/* Byte 15 */
	/* Error Counters */
	unsigned short SoftErrors;			/* Bytes 16-17 */
	unsigned short CommandsFailed;			/* Bytes 18-19 */
	unsigned short HostCommandAbortsDone;		/* Bytes 20-21 */
	unsigned short DeferredWriteErrors;		/* Bytes 22-23 */
	unsigned int rsvd4:32;				/* Bytes 24-27 */
	unsigned int rsvd5:32;				/* Bytes 28-31 */
	/* Device Size Information */
	unsigned short rsvd6:16;			/* Bytes 32-33 */
	unsigned short DeviceBlockSizeInBytes;		/* Bytes 34-35 */
	unsigned int orig_devsize;			/* Bytes 36-39 */
	unsigned int cfg_devsize;			/* Bytes 40-43 */
	unsigned int rsvd7:32;				/* Bytes 44-47 */
	unsigned char ldev_name[32];			/* Bytes 48-79 */
	unsigned char SCSI_InquiryData[36];		/* Bytes 80-115 */
	unsigned char Reserved1[12];			/* Bytes 116-127 */
	u64 last_read_lba;				/* Bytes 128-135 */
	u64 last_write_lba;				/* Bytes 136-143 */
	u64 cc_lba;					/* Bytes 144-151 */
	u64 rbld_lba;					/* Bytes 152-159 */
	u64 bg_init_lba;				/* Bytes 160-167 */
	u64 fg_init_lba;				/* Bytes 168-175 */
	u64 migration_lba;				/* Bytes 176-183 */
	u64 patrol_lba;					/* Bytes 184-191 */
	unsigned char rsvd8[64];			/* Bytes 192-255 */
} myrs_ldev_info;


/*
  Define the DAC960 V2 Firmware Get Physical Device Info reply structure.
*/

typedef struct myrs_pdev_info_s
{
	unsigned char rsvd1:8;				/* Byte 0 */
	unsigned char Channel;				/* Byte 1 */
	unsigned char TargetID;				/* Byte 2 */
	unsigned char LogicalUnit;			/* Byte 3 */
	/* Configuration Status Bits */
	bool PhysicalDeviceFaultTolerant:1;		/* Byte 4 Bit 0 */
	bool PhysicalDeviceConnected:1;			/* Byte 4 Bit 1 */
	bool PhysicalDeviceLocalToController:1;		/* Byte 4 Bit 2 */
	unsigned char rsvd2:5;				/* Byte 4 Bits 3-7 */
	/* Multiple Host/Controller Status Bits */
	bool RemoteHostSystemDead:1;			/* Byte 5 Bit 0 */
	bool RemoteControllerDead:1;			/* Byte 5 Bit 1 */
	unsigned char rsvd3:6;				/* Byte 5 Bits 2-7 */
	myrs_devstate State;				/* Byte 6 */
	unsigned char NegotiatedDataWidthBits;		/* Byte 7 */
	unsigned short NegotiatedSynchronousMegaTransfers; /* Bytes 8-9 */
	/* Multiported Physical Device Information */
	unsigned char NumberOfPortConnections;		/* Byte 10 */
	unsigned char DriveAccessibilityBitmap;		/* Byte 11 */
	unsigned int rsvd4:32;				/* Bytes 12-15 */
	unsigned char NetworkAddress[16];		/* Bytes 16-31 */
	unsigned short MaximumTags;			/* Bytes 32-33 */
	/* Physical Device Operations Status */
	bool ConsistencyCheckInProgress:1;		/* Byte 34 Bit 0 */
	bool RebuildInProgress:1;			/* Byte 34 Bit 1 */
	bool MakingDataConsistentInProgress:1;		/* Byte 34 Bit 2 */
	bool PhysicalDeviceInitializationInProgress:1;	/* Byte 34 Bit 3 */
	bool DataMigrationInProgress:1;			/* Byte 34 Bit 4 */
	bool PatrolOperationInProgress:1;		/* Byte 34 Bit 5 */
	unsigned char rsvd5:2;				/* Byte 34 Bits 6-7 */
	unsigned char LongOperationStatus;		/* Byte 35 */
	unsigned char ParityErrors;			/* Byte 36 */
	unsigned char SoftErrors;			/* Byte 37 */
	unsigned char HardErrors;			/* Byte 38 */
	unsigned char MiscellaneousErrors;		/* Byte 39 */
	unsigned char CommandTimeouts;			/* Byte 40 */
	unsigned char Retries;				/* Byte 41 */
	unsigned char Aborts;				/* Byte 42 */
	unsigned char PredictedFailuresDetected;	/* Byte 43 */
	unsigned int rsvd6:32;				/* Bytes 44-47 */
	unsigned short rsvd7:16;			/* Bytes 48-49 */
	unsigned short DeviceBlockSizeInBytes;		/* Bytes 50-51 */
	unsigned int orig_devsize;			/* Bytes 52-55 */
	unsigned int cfg_devsize;			/* Bytes 56-59 */
	unsigned int rsvd8:32;				/* Bytes 60-63 */
	unsigned char PhysicalDeviceName[16];		/* Bytes 64-79 */
	unsigned char rsvd9[16];			/* Bytes 80-95 */
	unsigned char rsvd10[32];			/* Bytes 96-127 */
	unsigned char SCSI_InquiryData[36];		/* Bytes 128-163 */
	unsigned char rsvd11[20];			/* Bytes 164-183 */
	unsigned char rsvd12[8];			/* Bytes 184-191 */
	u64 LastReadBlockNumber;			/* Bytes 192-199 */
	u64 LastWrittenBlockNumber;			/* Bytes 200-207 */
	u64 ConsistencyCheckBlockNumber;		/* Bytes 208-215 */
	u64 RebuildBlockNumber;				/* Bytes 216-223 */
	u64 MakingDataConsistentBlockNumber;		/* Bytes 224-231 */
	u64 DeviceInitializationBlockNumber;		/* Bytes 232-239 */
	u64 DataMigrationBlockNumber;			/* Bytes 240-247 */
	u64 PatrolOperationBlockNumber;			/* Bytes 248-255 */
	unsigned char rsvd13[256];			/* Bytes 256-511 */
} myrs_pdev_info;


/*
  Define the DAC960 V2 Firmware Health Status Buffer structure.
*/

typedef struct myrs_fwstat_s
{
	unsigned int MicrosecondsFromControllerStartTime;	/* Bytes 0-3 */
	unsigned int MillisecondsFromControllerStartTime;	/* Bytes 4-7 */
	unsigned int SecondsFrom1January1970;			/* Bytes 8-11 */
	unsigned int :32;					/* Bytes 12-15 */
	unsigned int epoch;			/* Bytes 16-19 */
	unsigned int :32;					/* Bytes 20-23 */
	unsigned int DebugOutputMessageBufferIndex;		/* Bytes 24-27 */
	unsigned int CodedMessageBufferIndex;			/* Bytes 28-31 */
	unsigned int CurrentTimeTracePageNumber;		/* Bytes 32-35 */
	unsigned int CurrentProfilerPageNumber;		/* Bytes 36-39 */
	unsigned int next_evseq;			/* Bytes 40-43 */
	unsigned int :32;					/* Bytes 44-47 */
	unsigned char Reserved1[16];				/* Bytes 48-63 */
	unsigned char Reserved2[64];				/* Bytes 64-127 */
} myrs_fwstat;


/*
  Define the DAC960 V2 Firmware Get Event reply structure.
*/

typedef struct myrs_event_s
{
	unsigned int ev_seq;				/* Bytes 0-3 */
	unsigned int ev_time;				/* Bytes 4-7 */
	unsigned int ev_code;				/* Bytes 8-11 */
	unsigned char rsvd1:8;				/* Byte 12 */
	unsigned char channel;				/* Byte 13 */
	unsigned char target;				/* Byte 14 */
	unsigned char lun;				/* Byte 15 */
	unsigned int rsvd2:32;				/* Bytes 16-19 */
	unsigned int ev_parm;				/* Bytes 20-23 */
	unsigned char sense_data[40];			/* Bytes 24-63 */
} myrs_event;


/*
  Define the DAC960 V2 Firmware Command Control Bits structure.
*/

typedef struct myrs_cmd_ctrl_s
{
	bool ForceUnitAccess:1;				/* Byte 0 Bit 0 */
	bool DisablePageOut:1;				/* Byte 0 Bit 1 */
	bool rsvd1:1;						/* Byte 0 Bit 2 */
	bool AdditionalScatterGatherListMemory:1;		/* Byte 0 Bit 3 */
	bool DataTransferControllerToHost:1;			/* Byte 0 Bit 4 */
	bool rsvd2:1;						/* Byte 0 Bit 5 */
	bool NoAutoRequestSense:1;				/* Byte 0 Bit 6 */
	bool DisconnectProhibited:1;				/* Byte 0 Bit 7 */
} myrs_cmd_ctrl;


/*
  Define the DAC960 V2 Firmware Command Timeout structure.
*/

typedef struct myrs_cmd_tmo_s
{
	unsigned char TimeoutValue:6;				/* Byte 0 Bits 0-5 */
	enum {
		DAC960_V2_TimeoutScale_Seconds =		0,
		DAC960_V2_TimeoutScale_Minutes =		1,
		DAC960_V2_TimeoutScale_Hours =		2,
		DAC960_V2_TimeoutScale_Reserved =		3
	} __attribute__ ((packed)) TimeoutScale:2;		/* Byte 0 Bits 6-7 */
} myrs_cmd_tmo;


/*
  Define the DAC960 V2 Firmware Physical Device structure.
*/

typedef struct myrs_pdev_s
{
	unsigned char LogicalUnit;			/* Byte 0 */
	unsigned char TargetID;				/* Byte 1 */
	unsigned char Channel:3;			/* Byte 2 Bits 0-2 */
	unsigned char Controller:5;			/* Byte 2 Bits 3-7 */
}
__attribute__ ((packed))
myrs_pdev;


/*
  Define the DAC960 V2 Firmware Logical Device structure.
*/

typedef struct myrs_ldev_s
{
	unsigned short ldev_num;			/* Bytes 0-1 */
	unsigned char rsvd:3;				/* Byte 2 Bits 0-2 */
	unsigned char Controller:5;			/* Byte 2 Bits 3-7 */
}
__attribute__ ((packed))
myrs_ldev;


/*
  Define the DAC960 V2 Firmware Operation Device type.
*/

typedef enum
{
	DAC960_V2_Physical_Device =		0x00,
	DAC960_V2_RAID_Device =			0x01,
	DAC960_V2_Physical_Channel =		0x02,
	DAC960_V2_RAID_Channel =		0x03,
	DAC960_V2_Physical_Controller =		0x04,
	DAC960_V2_RAID_Controller =		0x05,
	DAC960_V2_Configuration_Group =		0x10,
	DAC960_V2_Enclosure =			0x11
}
__attribute__ ((packed))
myrs_opdev;


/*
  Define the DAC960 V2 Firmware Translate Physical To Logical Device structure.
*/

typedef struct myrs_devmap_s
{
	unsigned short ldev_num;			/* Bytes 0-1 */
	unsigned short :16;					/* Bytes 2-3 */
	unsigned char PreviousBootController;			/* Byte 4 */
	unsigned char PreviousBootChannel;			/* Byte 5 */
	unsigned char PreviousBootTargetID;			/* Byte 6 */
	unsigned char PreviousBootLogicalUnit;		/* Byte 7 */
} myrs_devmap;



/*
  Define the DAC960 V2 Firmware Scatter/Gather List Entry structure.
*/

typedef struct myrs_sge_s
{
	u64 sge_addr;			/* Bytes 0-7 */
	u64 sge_count;			/* Bytes 8-15 */
} myrs_sge;


/*
  Define the DAC960 V2 Firmware Data Transfer Memory Address structure.
*/

typedef union myrs_sgl_s
{
	myrs_sge sge[2]; /* Bytes 0-31 */
	struct {
		unsigned short sge0_len;	/* Bytes 0-1 */
		unsigned short sge1_len;	/* Bytes 2-3 */
		unsigned short sge2_len;	/* Bytes 4-5 */
		unsigned short rsvd:16;		/* Bytes 6-7 */
		u64 sge0_addr;			/* Bytes 8-15 */
		u64 sge1_addr;			/* Bytes 16-23 */
		u64 sge2_addr;			/* Bytes 24-31 */
	} ext;
} myrs_sgl;


/*
  Define the 64 Byte DAC960 V2 Firmware Command Mailbox structure.
*/

typedef union myrs_cmd_mbox_s
{
	unsigned int Words[16];				/* Words 0-15 */
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		unsigned int rsvd1:24;			/* Bytes 16-18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char ioctl_opcode;		/* Byte 21 */
		unsigned char rsvd2[10];		/* Bytes 22-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} Common;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size;				/* Bytes 4-7 */
		u64 sense_addr;				/* Bytes 8-15 */
		myrs_pdev pdev;				/* Bytes 16-18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char cdb_len;			/* Byte 21 */
		unsigned char cdb[10];			/* Bytes 22-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} SCSI_10;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size;				/* Bytes 4-7 */
		u64 sense_addr;				/* Bytes 8-15 */
		myrs_pdev pdev;				/* Bytes 16-18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char cdb_len;			/* Byte 21 */
		unsigned short rsvd:16;			/* Bytes 22-23 */
		u64 cdb_addr;				/* Bytes 24-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} SCSI_255;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		unsigned short rsvd1:16;		/* Bytes 16-17 */
		unsigned char ctlr_num;			/* Byte 18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char ioctl_opcode;		/* Byte 21 */
		unsigned char rsvd2[10];		/* Bytes 22-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} ControllerInfo;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		myrs_ldev ldev;				/* Bytes 16-18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char ioctl_opcode;		/* Byte 21 */
		unsigned char rsvd[10];			/* Bytes 22-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} LogicalDeviceInfo;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		myrs_pdev pdev;				/* Bytes 16-18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char ioctl_opcode;		/* Byte 21 */
		unsigned char rsvd[10];			/* Bytes 22-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} PhysicalDeviceInfo;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		unsigned short evnum_upper;		/* Bytes 16-17 */
		unsigned char ctlr_num;			/* Byte 18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char ioctl_opcode;		/* Byte 21 */
		unsigned short evnum_lower;		/* Bytes 22-23 */
		unsigned char rsvd[8];			/* Bytes 24-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} GetEvent;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		union {
			myrs_ldev ldev;			/* Bytes 16-18 */
			myrs_pdev pdev;			/* Bytes 16-18 */
		};
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char ioctl_opcode;		/* Byte 21 */
		myrs_devstate state;			/* Byte 22 */
		unsigned char rsvd[9];			/* Bytes 23-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} SetDeviceState;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		myrs_ldev ldev;				/* Bytes 16-18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char ioctl_opcode;		/* Byte 21 */
		bool RestoreConsistency:1;		/* Byte 22 Bit 0 */
		bool InitializedAreaOnly:1;		/* Byte 22 Bit 1 */
		unsigned char rsvd1:6;			/* Byte 22 Bits 2-7 */
		unsigned char rsvd2[9];			/* Bytes 23-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} ConsistencyCheck;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		unsigned char FirstCommandMailboxSizeKB;	/* Byte 4 */
		unsigned char FirstStatusMailboxSizeKB;		/* Byte 5 */
		unsigned char SecondCommandMailboxSizeKB;	/* Byte 6 */
		unsigned char SecondStatusMailboxSizeKB;	/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		unsigned int rsvd1:24;			/* Bytes 16-18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char ioctl_opcode;		/* Byte 21 */
		unsigned char HealthStatusBufferSizeKB;		/* Byte 22 */
		unsigned char rsvd2:8;			/* Byte 23 */
		u64 HealthStatusBufferBusAddress;	/* Bytes 24-31 */
		u64 FirstCommandMailboxBusAddress;	/* Bytes 32-39 */
		u64 FirstStatusMailboxBusAddress;	/* Bytes 40-47 */
		u64 SecondCommandMailboxBusAddress;	/* Bytes 48-55 */
		u64 SecondStatusMailboxBusAddress;	/* Bytes 56-63 */
	} SetMemoryMailbox;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		myrs_cmd_opcode opcode;			/* Byte 2 */
		myrs_cmd_ctrl control;			/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		myrs_pdev pdev;				/* Bytes 16-18 */
		myrs_cmd_tmo tmo;			/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char ioctl_opcode;		/* Byte 21 */
		myrs_opdev opdev;			/* Byte 22 */
		unsigned char rsvd[9];			/* Bytes 23-31 */
		myrs_sgl dma_addr;			/* Bytes 32-63 */
	} DeviceOperation;
} myrs_cmd_mbox;


/*
  Define the DAC960 V2 Firmware Controller Status Mailbox structure.
*/

typedef struct myrs_stat_mbox_s
{
	unsigned short id;		/* Bytes 0-1 */
	unsigned char status;		/* Byte 2 */
	unsigned char sense_len;	/* Byte 3 */
	int residual;			/* Bytes 4-7 */
} myrs_stat_mbox;

typedef struct myrs_cmdblk_s
{
	myrs_cmd_mbox mbox;
	unsigned char status;
	unsigned char sense_len;
	int residual;
	struct completion *Completion;
	myrs_sge *sgl;
	dma_addr_t sgl_addr;
	unsigned char *DCDB;
	dma_addr_t DCDB_dma;
	unsigned char *sense;
	dma_addr_t sense_addr;
} myrs_cmdblk;

/*
  Define the DAC960 Driver Controller structure.
*/

typedef struct myrs_hba_s
{
	void __iomem *io_base;
	void __iomem *mmio_base;
	phys_addr_t io_addr;
	phys_addr_t pci_addr;
	unsigned int irq;

	unsigned char model_name[28];
	unsigned char fw_version[12];

	struct Scsi_Host *host;
	struct pci_dev *pdev;

	unsigned int epoch;
	unsigned int next_evseq;
	/* Monitor flags */
	bool needs_update;
	bool disable_enc_msg;

	struct workqueue_struct *work_q;
	char work_q_name[20];
	struct delayed_work monitor_work;
	unsigned long primary_monitor_time;
	unsigned long secondary_monitor_time;

	spinlock_t queue_lock;

	struct dma_pool *sg_pool;
	struct dma_pool *sense_pool;
	struct dma_pool *dcdb_pool;

	void (*write_cmd_mbox)(myrs_cmd_mbox *, myrs_cmd_mbox *);
	void (*get_cmd_mbox)(void __iomem *);
	void (*disable_intr)(void __iomem *);
	void (*reset)(void __iomem *);

	dma_addr_t cmd_mbox_addr;
	size_t cmd_mbox_size;
	myrs_cmd_mbox *first_cmd_mbox;
	myrs_cmd_mbox *last_cmd_mbox;
	myrs_cmd_mbox *next_cmd_mbox;
	myrs_cmd_mbox *prev_cmd_mbox1;
	myrs_cmd_mbox *prev_cmd_mbox2;

	dma_addr_t stat_mbox_addr;
	size_t stat_mbox_size;
	myrs_stat_mbox *first_stat_mbox;
	myrs_stat_mbox *last_stat_mbox;
	myrs_stat_mbox *next_stat_mbox;

	myrs_cmdblk dcmd_blk;
	myrs_cmdblk mcmd_blk;
	struct mutex dcmd_mutex;

	myrs_fwstat *fwstat_buf;
	dma_addr_t fwstat_addr;

	myrs_ctlr_info *ctlr_info;
	struct mutex cinfo_mutex;

	myrs_event *event_buf;
} myrs_hba;

typedef unsigned char (*enable_mbox_t)(void __iomem *base, dma_addr_t addr);
typedef int (*myrs_hwinit_t)(struct pci_dev *pdev,
			     struct myrs_hba_s *c, void __iomem *base);

struct myrs_privdata {
	myrs_hwinit_t		hw_init;
	irq_handler_t		irq_handler;
	unsigned int		io_mem_size;
};

/*
  Define the DAC960 GEM Series Controller Interface Register Offsets.
 */

#define DAC960_GEM_RegisterWindowSize	0x600

typedef enum
{
	DAC960_GEM_InboundDoorBellRegisterReadSetOffset = 0x214,
	DAC960_GEM_InboundDoorBellRegisterClearOffset =	0x218,
	DAC960_GEM_OutboundDoorBellRegisterReadSetOffset = 0x224,
	DAC960_GEM_OutboundDoorBellRegisterClearOffset = 0x228,
	DAC960_GEM_InterruptStatusRegisterOffset =	0x208,
	DAC960_GEM_InterruptMaskRegisterReadSetOffset =	0x22C,
	DAC960_GEM_InterruptMaskRegisterClearOffset =	0x230,
	DAC960_GEM_CommandMailboxBusAddressOffset =	0x510,
	DAC960_GEM_CommandStatusOffset =		0x518,
	DAC960_GEM_ErrorStatusRegisterReadSetOffset =	0x224,
	DAC960_GEM_ErrorStatusRegisterClearOffset =	0x228,
}
DAC960_GEM_RegisterOffsets_T;

/*
  Define the structure of the DAC960 GEM Series Inbound Door Bell
 */

typedef union DAC960_GEM_InboundDoorBellRegister
{
	unsigned int All;
	struct {
		unsigned int :24;
		bool HardwareMailboxNewCommand:1;
		bool AcknowledgeHardwareMailboxStatus:1;
		bool GenerateInterrupt:1;
		bool ControllerReset:1;
		bool MemoryMailboxNewCommand:1;
		unsigned int :3;
	} Write;
	struct {
		unsigned int :24;
		bool HardwareMailboxFull:1;
		bool InitializationInProgress:1;
		unsigned int :6;
	} Read;
}
DAC960_GEM_InboundDoorBellRegister_T;

/*
  Define the structure of the DAC960 GEM Series Outbound Door Bell Register.
 */
typedef union DAC960_GEM_OutboundDoorBellRegister
{
	unsigned int All;
	struct {
		unsigned int :24;
		bool AcknowledgeHardwareMailboxInterrupt:1;
		bool AcknowledgeMemoryMailboxInterrupt:1;
		unsigned int :6;
	} Write;
	struct {
		unsigned int :24;
		bool HardwareMailboxStatusAvailable:1;
		bool MemoryMailboxStatusAvailable:1;
		unsigned int :6;
	} Read;
}
DAC960_GEM_OutboundDoorBellRegister_T;

/*
  Define the structure of the DAC960 GEM Series Interrupt Mask Register.
 */
typedef union DAC960_GEM_InterruptMaskRegister
{
	unsigned int All;
	struct {
		unsigned int :16;
		unsigned int :8;
		unsigned int HardwareMailboxInterrupt:1;
		unsigned int MemoryMailboxInterrupt:1;
		unsigned int :6;
	} Bits;
}
DAC960_GEM_InterruptMaskRegister_T;

/*
  Define the structure of the DAC960 GEM Series Error Status Register.
 */

typedef union DAC960_GEM_ErrorStatusRegister
{
	unsigned int All;
	struct {
		unsigned int :24;
		unsigned int :5;
		bool ErrorStatusPending:1;
		unsigned int :2;
	} Bits;
}
DAC960_GEM_ErrorStatusRegister_T;

/*
 * dma_addr_writeql is provided to write dma_addr_t types
 * to a 64-bit pci address space register.  The controller
 * will accept having the register written as two 32-bit
 * values.
 *
 * In HIGHMEM kernels, dma_addr_t is a 64-bit value.
 * without HIGHMEM,  dma_addr_t is a 32-bit value.
 *
 * The compiler should always fix up the assignment
 * to u.wq appropriately, depending upon the size of
 * dma_addr_t.
 */
static inline
void dma_addr_writeql(dma_addr_t addr, void __iomem *write_address)
{
	union {
		u64 wq;
		uint wl[2];
	} u;

	u.wq = addr;

	writel(u.wl[0], write_address);
	writel(u.wl[1], write_address + 4);
}

/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 GEM Series Controller Interface Registers.
*/

static inline
void DAC960_GEM_HardwareMailboxNewCommand(void __iomem *base)
{
	DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
}

static inline
void DAC960_GEM_AcknowledgeHardwareMailboxStatus(void __iomem *base)
{
	DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_GEM_InboundDoorBellRegisterClearOffset);
}

static inline
void DAC960_GEM_GenerateInterrupt(void __iomem *base)
{
	DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.GenerateInterrupt = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
}

static inline
void DAC960_GEM_ControllerReset(void __iomem *base)
{
	DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.ControllerReset = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
}

static inline
void DAC960_GEM_MemoryMailboxNewCommand(void __iomem *base)
{
	DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
}

static inline
bool DAC960_GEM_HardwareMailboxFullP(void __iomem *base)
{
	DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readl(base + DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
	return InboundDoorBellRegister.Read.HardwareMailboxFull;
}

static inline
bool DAC960_GEM_InitializationInProgressP(void __iomem *base)
{
	DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readl(base +
		      DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
	return InboundDoorBellRegister.Read.InitializationInProgress;
}

static inline
void DAC960_GEM_AcknowledgeHardwareMailboxInterrupt(void __iomem *base)
{
	DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	writel(OutboundDoorBellRegister.All,
	       base + DAC960_GEM_OutboundDoorBellRegisterClearOffset);
}

static inline
void DAC960_GEM_AcknowledgeMemoryMailboxInterrupt(void __iomem *base)
{
	DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writel(OutboundDoorBellRegister.All,
	       base + DAC960_GEM_OutboundDoorBellRegisterClearOffset);
}

static inline
void DAC960_GEM_AcknowledgeInterrupt(void __iomem *base)
{
	DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writel(OutboundDoorBellRegister.All,
	       base + DAC960_GEM_OutboundDoorBellRegisterClearOffset);
}

static inline
bool DAC960_GEM_HardwareMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readl(base + DAC960_GEM_OutboundDoorBellRegisterReadSetOffset);
	return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
bool DAC960_GEM_MemoryMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readl(base + DAC960_GEM_OutboundDoorBellRegisterReadSetOffset);
	return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_GEM_EnableInterrupts(void __iomem *base)
{
	DAC960_GEM_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0;
	InterruptMaskRegister.Bits.HardwareMailboxInterrupt = true;
	InterruptMaskRegister.Bits.MemoryMailboxInterrupt = true;
	writel(InterruptMaskRegister.All,
	       base + DAC960_GEM_InterruptMaskRegisterClearOffset);
}

static inline
void DAC960_GEM_DisableInterrupts(void __iomem *base)
{
	DAC960_GEM_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0;
	InterruptMaskRegister.Bits.HardwareMailboxInterrupt = true;
	InterruptMaskRegister.Bits.MemoryMailboxInterrupt = true;
	writel(InterruptMaskRegister.All,
	       base + DAC960_GEM_InterruptMaskRegisterReadSetOffset);
}

static inline
bool DAC960_GEM_InterruptsEnabledP(void __iomem *base)
{
	DAC960_GEM_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All =
		readl(base + DAC960_GEM_InterruptMaskRegisterReadSetOffset);
	return !(InterruptMaskRegister.Bits.HardwareMailboxInterrupt ||
		 InterruptMaskRegister.Bits.MemoryMailboxInterrupt);
}

static inline
void DAC960_GEM_WriteCommandMailbox(myrs_cmd_mbox *mem_mbox,
				    myrs_cmd_mbox *mbox)
{
	memcpy(&mem_mbox->Words[1], &mbox->Words[1],
	       sizeof(myrs_cmd_mbox) - sizeof(unsigned int));
	wmb();
	mem_mbox->Words[0] = mbox->Words[0];
	mb();
}

static inline
void DAC960_GEM_WriteHardwareMailbox(void __iomem *base,
				     dma_addr_t CommandMailboxDMA)
{
	dma_addr_writeql(CommandMailboxDMA,
			 base + DAC960_GEM_CommandMailboxBusAddressOffset);
}

static inline unsigned short
DAC960_GEM_ReadCommandIdentifier(void __iomem *base)
{
	return readw(base + DAC960_GEM_CommandStatusOffset);
}

static inline unsigned char
DAC960_GEM_ReadCommandStatus(void __iomem *base)
{
	return readw(base + DAC960_GEM_CommandStatusOffset + 2);
}

static inline bool
DAC960_GEM_ReadErrorStatus(void __iomem *base,
			   unsigned char *ErrorStatus,
			   unsigned char *Parameter0,
			   unsigned char *Parameter1)
{
	DAC960_GEM_ErrorStatusRegister_T ErrorStatusRegister;
	ErrorStatusRegister.All =
		readl(base + DAC960_GEM_ErrorStatusRegisterReadSetOffset);
	if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
	ErrorStatusRegister.Bits.ErrorStatusPending = false;
	*ErrorStatus = ErrorStatusRegister.All;
	*Parameter0 =
		readb(base + DAC960_GEM_CommandMailboxBusAddressOffset + 0);
	*Parameter1 =
		readb(base + DAC960_GEM_CommandMailboxBusAddressOffset + 1);
	writel(0x03000000, base +
	       DAC960_GEM_ErrorStatusRegisterClearOffset);
	return true;
}

static inline unsigned char
DAC960_GEM_MailboxInit(void __iomem *base, dma_addr_t mbox_addr)
{
	unsigned char status;

	while (DAC960_GEM_HardwareMailboxFullP(base))
		udelay(1);
	DAC960_GEM_WriteHardwareMailbox(base, mbox_addr);
	DAC960_GEM_HardwareMailboxNewCommand(base);
	while (!DAC960_GEM_HardwareMailboxStatusAvailableP(base))
		udelay(1);
	status = DAC960_GEM_ReadCommandStatus(base);
	DAC960_GEM_AcknowledgeHardwareMailboxInterrupt(base);
	DAC960_GEM_AcknowledgeHardwareMailboxStatus(base);

	return status;
}

/*
  Define the DAC960 BA Series Controller Interface Register Offsets.
*/

#define DAC960_BA_RegisterWindowSize		0x80

typedef enum
{
	DAC960_BA_InterruptStatusRegisterOffset =	0x30,
	DAC960_BA_InterruptMaskRegisterOffset =		0x34,
	DAC960_BA_CommandMailboxBusAddressOffset =	0x50,
	DAC960_BA_CommandStatusOffset =			0x58,
	DAC960_BA_InboundDoorBellRegisterOffset =	0x60,
	DAC960_BA_OutboundDoorBellRegisterOffset =	0x61,
	DAC960_BA_ErrorStatusRegisterOffset =		0x63
}
DAC960_BA_RegisterOffsets_T;


/*
  Define the structure of the DAC960 BA Series Inbound Door Bell Register.
*/

typedef union DAC960_BA_InboundDoorBellRegister
{
	unsigned char All;
	struct {
		bool HardwareMailboxNewCommand:1;			/* Bit 0 */
		bool AcknowledgeHardwareMailboxStatus:1;		/* Bit 1 */
		bool GenerateInterrupt:1;				/* Bit 2 */
		bool ControllerReset:1;				/* Bit 3 */
		bool MemoryMailboxNewCommand:1;			/* Bit 4 */
		unsigned char :3;					/* Bits 5-7 */
	} Write;
	struct {
		bool HardwareMailboxEmpty:1;			/* Bit 0 */
		bool InitializationNotInProgress:1;			/* Bit 1 */
		unsigned char :6;					/* Bits 2-7 */
	} Read;
}
DAC960_BA_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 BA Series Outbound Door Bell Register.
*/

typedef union DAC960_BA_OutboundDoorBellRegister
{
	unsigned char All;
	struct {
		bool AcknowledgeHardwareMailboxInterrupt:1;		/* Bit 0 */
		bool AcknowledgeMemoryMailboxInterrupt:1;		/* Bit 1 */
		unsigned char :6;					/* Bits 2-7 */
	} Write;
	struct {
		bool HardwareMailboxStatusAvailable:1;		/* Bit 0 */
		bool MemoryMailboxStatusAvailable:1;		/* Bit 1 */
		unsigned char :6;					/* Bits 2-7 */
	} Read;
}
DAC960_BA_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 BA Series Interrupt Mask Register.
*/

typedef union DAC960_BA_InterruptMaskRegister
{
	unsigned char All;
	struct {
		unsigned int :2;				/* Bits 0-1 */
		bool DisableInterrupts:1;			/* Bit 2 */
		bool DisableInterruptsI2O:1;			/* Bit 3 */
		unsigned int :4;				/* Bits 4-7 */
	} Bits;
}
DAC960_BA_InterruptMaskRegister_T;


/*
  Define the structure of the DAC960 BA Series Error Status Register.
*/

typedef union DAC960_BA_ErrorStatusRegister
{
	unsigned char All;
	struct {
		unsigned int :2;				/* Bits 0-1 */
		bool ErrorStatusPending:1;			/* Bit 2 */
		unsigned int :5;				/* Bits 3-7 */
	} Bits;
}
DAC960_BA_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 BA Series Controller Interface Registers.
*/

static inline
void DAC960_BA_HardwareMailboxNewCommand(void __iomem *base)
{
	DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_AcknowledgeHardwareMailboxStatus(void __iomem *base)
{
	DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_GenerateInterrupt(void __iomem *base)
{
	DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.GenerateInterrupt = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_ControllerReset(void __iomem *base)
{
	DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.ControllerReset = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_MemoryMailboxNewCommand(void __iomem *base)
{
	DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
bool DAC960_BA_HardwareMailboxFullP(void __iomem *base)
{
	DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readb(base + DAC960_BA_InboundDoorBellRegisterOffset);
	return !InboundDoorBellRegister.Read.HardwareMailboxEmpty;
}

static inline
bool DAC960_BA_InitializationInProgressP(void __iomem *base)
{
	DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readb(base + DAC960_BA_InboundDoorBellRegisterOffset);
	return !InboundDoorBellRegister.Read.InitializationNotInProgress;
}

static inline
void DAC960_BA_AcknowledgeHardwareMailboxInterrupt(void __iomem *base)
{
	DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_BA_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_AcknowledgeMemoryMailboxInterrupt(void __iomem *base)
{
	DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_BA_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_AcknowledgeInterrupt(void __iomem *base)
{
	DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_BA_OutboundDoorBellRegisterOffset);
}

static inline
bool DAC960_BA_HardwareMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readb(base + DAC960_BA_OutboundDoorBellRegisterOffset);
	return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
bool DAC960_BA_MemoryMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readb(base + DAC960_BA_OutboundDoorBellRegisterOffset);
	return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_BA_EnableInterrupts(void __iomem *base)
{
	DAC960_BA_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0xFF;
	InterruptMaskRegister.Bits.DisableInterrupts = false;
	InterruptMaskRegister.Bits.DisableInterruptsI2O = true;
	writeb(InterruptMaskRegister.All,
	       base + DAC960_BA_InterruptMaskRegisterOffset);
}

static inline
void DAC960_BA_DisableInterrupts(void __iomem *base)
{
	DAC960_BA_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0xFF;
	InterruptMaskRegister.Bits.DisableInterrupts = true;
	InterruptMaskRegister.Bits.DisableInterruptsI2O = true;
	writeb(InterruptMaskRegister.All,
	       base + DAC960_BA_InterruptMaskRegisterOffset);
}

static inline
bool DAC960_BA_InterruptsEnabledP(void __iomem *base)
{
	DAC960_BA_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All =
		readb(base + DAC960_BA_InterruptMaskRegisterOffset);
	return !InterruptMaskRegister.Bits.DisableInterrupts;
}

static inline
void DAC960_BA_WriteCommandMailbox(myrs_cmd_mbox *mem_mbox,
				   myrs_cmd_mbox *mbox)
{
	memcpy(&mem_mbox->Words[1], &mbox->Words[1],
	       sizeof(myrs_cmd_mbox) - sizeof(unsigned int));
	wmb();
	mem_mbox->Words[0] = mbox->Words[0];
	mb();
}


static inline
void DAC960_BA_WriteHardwareMailbox(void __iomem *base,
				    dma_addr_t CommandMailboxDMA)
{
	dma_addr_writeql(CommandMailboxDMA,
			 base + DAC960_BA_CommandMailboxBusAddressOffset);
}

static inline unsigned short
DAC960_BA_ReadCommandIdentifier(void __iomem *base)
{
	return readw(base + DAC960_BA_CommandStatusOffset);
}

static inline unsigned char
DAC960_BA_ReadCommandStatus(void __iomem *base)
{
	return readw(base + DAC960_BA_CommandStatusOffset + 2);
}

static inline bool
DAC960_BA_ReadErrorStatus(void __iomem *base,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
	DAC960_BA_ErrorStatusRegister_T ErrorStatusRegister;
	ErrorStatusRegister.All =
		readb(base + DAC960_BA_ErrorStatusRegisterOffset);
	if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
	ErrorStatusRegister.Bits.ErrorStatusPending = false;
	*ErrorStatus = ErrorStatusRegister.All;
	*Parameter0 = readb(base + DAC960_BA_CommandMailboxBusAddressOffset + 0);
	*Parameter1 = readb(base + DAC960_BA_CommandMailboxBusAddressOffset + 1);
	writeb(0xFF, base + DAC960_BA_ErrorStatusRegisterOffset);
	return true;
}

static inline unsigned char
DAC960_BA_MailboxInit(void __iomem *base, dma_addr_t mbox_addr)
{
	unsigned char status;

	while (DAC960_BA_HardwareMailboxFullP(base))
		udelay(1);
	DAC960_BA_WriteHardwareMailbox(base, mbox_addr);
	DAC960_BA_HardwareMailboxNewCommand(base);
	while (!DAC960_BA_HardwareMailboxStatusAvailableP(base))
		udelay(1);
	status = DAC960_BA_ReadCommandStatus(base);
	DAC960_BA_AcknowledgeHardwareMailboxInterrupt(base);
	DAC960_BA_AcknowledgeHardwareMailboxStatus(base);

	return status;
}

/*
  Define the DAC960 LP Series Controller Interface Register Offsets.
*/

#define DAC960_LP_RegisterWindowSize		0x80

typedef enum
{
	DAC960_LP_CommandMailboxBusAddressOffset =	0x10,
	DAC960_LP_CommandStatusOffset =			0x18,
	DAC960_LP_InboundDoorBellRegisterOffset =	0x20,
	DAC960_LP_OutboundDoorBellRegisterOffset =	0x2C,
	DAC960_LP_ErrorStatusRegisterOffset =		0x2E,
	DAC960_LP_InterruptStatusRegisterOffset =	0x30,
	DAC960_LP_InterruptMaskRegisterOffset =		0x34,
}
DAC960_LP_RegisterOffsets_T;


/*
  Define the structure of the DAC960 LP Series Inbound Door Bell Register.
*/

typedef union DAC960_LP_InboundDoorBellRegister
{
	unsigned char All;
	struct {
		bool HardwareMailboxNewCommand:1;			/* Bit 0 */
		bool AcknowledgeHardwareMailboxStatus:1;		/* Bit 1 */
		bool GenerateInterrupt:1;				/* Bit 2 */
		bool ControllerReset:1;				/* Bit 3 */
		bool MemoryMailboxNewCommand:1;			/* Bit 4 */
		unsigned char :3;					/* Bits 5-7 */
	} Write;
	struct {
		bool HardwareMailboxFull:1;				/* Bit 0 */
		bool InitializationInProgress:1;			/* Bit 1 */
		unsigned char :6;					/* Bits 2-7 */
	} Read;
}
DAC960_LP_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 LP Series Outbound Door Bell Register.
*/

typedef union DAC960_LP_OutboundDoorBellRegister
{
	unsigned char All;
	struct {
		bool AcknowledgeHardwareMailboxInterrupt:1;		/* Bit 0 */
		bool AcknowledgeMemoryMailboxInterrupt:1;		/* Bit 1 */
		unsigned char :6;					/* Bits 2-7 */
	} Write;
	struct {
		bool HardwareMailboxStatusAvailable:1;		/* Bit 0 */
		bool MemoryMailboxStatusAvailable:1;		/* Bit 1 */
		unsigned char :6;					/* Bits 2-7 */
	} Read;
}
DAC960_LP_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 LP Series Interrupt Mask Register.
*/

typedef union DAC960_LP_InterruptMaskRegister
{
	unsigned char All;
	struct {
		unsigned int :2;					/* Bits 0-1 */
		bool DisableInterrupts:1;				/* Bit 2 */
		unsigned int :5;					/* Bits 3-7 */
	} Bits;
}
DAC960_LP_InterruptMaskRegister_T;


/*
  Define the structure of the DAC960 LP Series Error Status Register.
*/

typedef union DAC960_LP_ErrorStatusRegister
{
	unsigned char All;
	struct {
		unsigned int :2;					/* Bits 0-1 */
		bool ErrorStatusPending:1;				/* Bit 2 */
		unsigned int :5;					/* Bits 3-7 */
	} Bits;
}
DAC960_LP_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 LP Series Controller Interface Registers.
*/

static inline
void DAC960_LP_HardwareMailboxNewCommand(void __iomem *base)
{
	DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_AcknowledgeHardwareMailboxStatus(void __iomem *base)
{
	DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_GenerateInterrupt(void __iomem *base)
{
	DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.GenerateInterrupt = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_ControllerReset(void __iomem *base)
{
	DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.ControllerReset = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_MemoryMailboxNewCommand(void __iomem *base)
{
	DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
bool DAC960_LP_HardwareMailboxFullP(void __iomem *base)
{
	DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readb(base + DAC960_LP_InboundDoorBellRegisterOffset);
	return InboundDoorBellRegister.Read.HardwareMailboxFull;
}

static inline
bool DAC960_LP_InitializationInProgressP(void __iomem *base)
{
	DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readb(base + DAC960_LP_InboundDoorBellRegisterOffset);
	return InboundDoorBellRegister.Read.InitializationInProgress;
}

static inline
void DAC960_LP_AcknowledgeHardwareMailboxInterrupt(void __iomem *base)
{
	DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_LP_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_AcknowledgeMemoryMailboxInterrupt(void __iomem *base)
{
	DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_LP_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_AcknowledgeInterrupt(void __iomem *base)
{
	DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_LP_OutboundDoorBellRegisterOffset);
}

static inline
bool DAC960_LP_HardwareMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readb(base + DAC960_LP_OutboundDoorBellRegisterOffset);
	return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
bool DAC960_LP_MemoryMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readb(base + DAC960_LP_OutboundDoorBellRegisterOffset);
	return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_LP_EnableInterrupts(void __iomem *base)
{
	DAC960_LP_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0xFF;
	InterruptMaskRegister.Bits.DisableInterrupts = false;
	writeb(InterruptMaskRegister.All,
	       base + DAC960_LP_InterruptMaskRegisterOffset);
}

static inline
void DAC960_LP_DisableInterrupts(void __iomem *base)
{
	DAC960_LP_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0xFF;
	InterruptMaskRegister.Bits.DisableInterrupts = true;
	writeb(InterruptMaskRegister.All,
	       base + DAC960_LP_InterruptMaskRegisterOffset);
}

static inline
bool DAC960_LP_InterruptsEnabledP(void __iomem *base)
{
	DAC960_LP_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All =
		readb(base + DAC960_LP_InterruptMaskRegisterOffset);
	return !InterruptMaskRegister.Bits.DisableInterrupts;
}

static inline
void DAC960_LP_WriteCommandMailbox(myrs_cmd_mbox *mem_mbox,
				   myrs_cmd_mbox *mbox)
{
	memcpy(&mem_mbox->Words[1], &mbox->Words[1],
	       sizeof(myrs_cmd_mbox) - sizeof(unsigned int));
	wmb();
	mem_mbox->Words[0] = mbox->Words[0];
	mb();
}

static inline
void DAC960_LP_WriteHardwareMailbox(void __iomem *base,
				    dma_addr_t CommandMailboxDMA)
{
	dma_addr_writeql(CommandMailboxDMA,
			 base +
			 DAC960_LP_CommandMailboxBusAddressOffset);
}

static inline unsigned short
DAC960_LP_ReadCommandIdentifier(void __iomem *base)
{
	return readw(base + DAC960_LP_CommandStatusOffset);
}

static inline unsigned char
DAC960_LP_ReadCommandStatus(void __iomem *base)
{
	return readw(base + DAC960_LP_CommandStatusOffset + 2);
}

static inline bool
DAC960_LP_ReadErrorStatus(void __iomem *base,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
	DAC960_LP_ErrorStatusRegister_T ErrorStatusRegister;
	ErrorStatusRegister.All =
		readb(base + DAC960_LP_ErrorStatusRegisterOffset);
	if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
	ErrorStatusRegister.Bits.ErrorStatusPending = false;
	*ErrorStatus = ErrorStatusRegister.All;
	*Parameter0 =
		readb(base + DAC960_LP_CommandMailboxBusAddressOffset + 0);
	*Parameter1 =
		readb(base + DAC960_LP_CommandMailboxBusAddressOffset + 1);
	writeb(0xFF, base + DAC960_LP_ErrorStatusRegisterOffset);
	return true;
}

static inline unsigned char
DAC960_LP_MailboxInit(void __iomem *base, dma_addr_t mbox_addr)
{
	unsigned char status;

	while (DAC960_LP_HardwareMailboxFullP(base))
		udelay(1);
	DAC960_LP_WriteHardwareMailbox(base, mbox_addr);
	DAC960_LP_HardwareMailboxNewCommand(base);
	while (!DAC960_LP_HardwareMailboxStatusAvailableP(base))
		udelay(1);
	status = DAC960_LP_ReadCommandStatus(base);
	DAC960_LP_AcknowledgeHardwareMailboxInterrupt(base);
	DAC960_LP_AcknowledgeHardwareMailboxStatus(base);

	return status;
}

#endif /* _MYRS_H */
