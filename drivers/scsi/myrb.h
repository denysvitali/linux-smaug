/*
 * Linux Driver for Mylex DAC960/AcceleRAID/eXtremeRAID PCI RAID Controllers
 *
 * Copyright 2017 Hannes Reinecke, SUSE Linux GmbH <hare@suse.com>
 *
 * Based on the original DAC960 driver,
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
 *
 */

#ifndef MYRB_H
#define MYRB_H

#define MYRB_MAX_LDEVS				32
#define DAC960_V1_MaxChannels			3
#define DAC960_V1_MaxTargets			16
#define DAC960_V1_MaxPhysicalDevices		45
#define DAC960_V1_ScatterGatherLimit		32
#define DAC960_V1_CommandMailboxCount		256
#define DAC960_V1_StatusMailboxCount		1024

#define MYRB_BLKSIZE_BITS			9
#define MYRB_MAILBOX_TIMEOUT 1000000

#define MYRB_DCMD_TAG 1
#define MYRB_MCMD_TAG 2

#define MYRB_PRIMARY_MONITOR_INTERVAL (10 * HZ)
#define MYRB_SECONDARY_MONITOR_INTERVAL (60 * HZ)

/*
  Define the DAC960 V1 Firmware Command Opcodes.
*/

typedef enum
{
	/* I/O Commands */
	DAC960_V1_ReadExtended =			0x33,
	DAC960_V1_WriteExtended =			0x34,
	DAC960_V1_ReadAheadExtended =			0x35,
	DAC960_V1_ReadExtendedWithScatterGather =	0xB3,
	DAC960_V1_WriteExtendedWithScatterGather =	0xB4,
	DAC960_V1_Read =				0x36,
	DAC960_V1_ReadWithScatterGather =		0xB6,
	DAC960_V1_Write =				0x37,
	DAC960_V1_WriteWithScatterGather =		0xB7,
	DAC960_V1_DCDB =				0x04,
	DAC960_V1_DCDBWithScatterGather =		0x84,
	DAC960_V1_Flush =				0x0A,
	/* Controller Status Related Commands */
	DAC960_V1_Enquiry =				0x53,
	DAC960_V1_Enquiry2 =				0x1C,
	DAC960_V1_GetLogicalDriveElement =		0x55,
	DAC960_V1_GetLogicalDeviceInfo =		0x19,
	DAC960_V1_IOPortRead =				0x39,
	DAC960_V1_IOPortWrite =				0x3A,
	DAC960_V1_GetSDStats =				0x3E,
	DAC960_V1_GetPDStats =				0x3F,
	DAC960_V1_PerformEventLogOperation =		0x72,
	/* Device Related Commands */
	DAC960_V1_StartDevice =				0x10,
	DAC960_V1_GetDeviceState =			0x50,
	DAC960_V1_StopChannel =				0x13,
	DAC960_V1_StartChannel =			0x12,
	DAC960_V1_ResetChannel =			0x1A,
	/* Commands Associated with Data Consistency and Errors */
	DAC960_V1_Rebuild =				0x09,
	DAC960_V1_RebuildAsync =			0x16,
	DAC960_V1_CheckConsistency =			0x0F,
	DAC960_V1_CheckConsistencyAsync =		0x1E,
	DAC960_V1_RebuildStat =				0x0C,
	DAC960_V1_GetRebuildProgress =			0x27,
	DAC960_V1_RebuildControl =			0x1F,
	DAC960_V1_ReadBadBlockTable =			0x0B,
	DAC960_V1_ReadBadDataTable =			0x25,
	DAC960_V1_ClearBadDataTable =			0x26,
	DAC960_V1_GetErrorTable =			0x17,
	DAC960_V1_AddCapacityAsync =			0x2A,
	DAC960_V1_BackgroundInitializationControl =	0x2B,
	/* Configuration Related Commands */
	DAC960_V1_ReadConfig2 =				0x3D,
	DAC960_V1_WriteConfig2 =			0x3C,
	DAC960_V1_ReadConfigurationOnDisk =		0x4A,
	DAC960_V1_WriteConfigurationOnDisk =		0x4B,
	DAC960_V1_ReadConfiguration =			0x4E,
	DAC960_V1_ReadBackupConfiguration =		0x4D,
	DAC960_V1_WriteConfiguration =			0x4F,
	DAC960_V1_AddConfiguration =			0x4C,
	DAC960_V1_ReadConfigurationLabel =		0x48,
	DAC960_V1_WriteConfigurationLabel =		0x49,
	/* Firmware Upgrade Related Commands */
	DAC960_V1_LoadImage =				0x20,
	DAC960_V1_StoreImage =				0x21,
	DAC960_V1_ProgramImage =			0x22,
	/* Diagnostic Commands */
	DAC960_V1_SetDiagnosticMode =			0x31,
	DAC960_V1_RunDiagnostic =			0x32,
	/* Subsystem Service Commands */
	DAC960_V1_GetSubsystemData =			0x70,
	DAC960_V1_SetSubsystemParameters =		0x71,
	/* Version 2.xx Firmware Commands */
	DAC960_V1_Enquiry_Old =				0x05,
	DAC960_V1_GetDeviceState_Old =			0x14,
	DAC960_V1_Read_Old =				0x02,
	DAC960_V1_Write_Old =				0x03,
	DAC960_V1_ReadWithScatterGather_Old =		0x82,
	DAC960_V1_WriteWithScatterGather_Old =		0x83
}
__attribute__ ((packed))
myrb_cmd_opcode;


/*
  Define the DAC960 V1 Firmware Command Status Codes.
*/

#define DAC960_V1_NormalCompletion		0x0000	/* Common */
#define DAC960_V1_CheckConditionReceived	0x0002	/* Common */
#define DAC960_V1_NoDeviceAtAddress		0x0102	/* Common */
#define DAC960_V1_InvalidDeviceAddress		0x0105	/* Common */
#define DAC960_V1_InvalidParameter		0x0105	/* Common */
#define DAC960_V1_IrrecoverableDataError	0x0001	/* I/O */
#define DAC960_V1_LogicalDriveNonexistentOrOffline 0x0002 /* I/O */
#define DAC960_V1_AccessBeyondEndOfLogicalDrive	0x0105	/* I/O */
#define DAC960_V1_BadDataEncountered		0x010C	/* I/O */
#define DAC960_V1_DeviceBusy			0x0008	/* DCDB */
#define DAC960_V1_DeviceNonresponsive		0x000E	/* DCDB */
#define DAC960_V1_CommandTerminatedAbnormally	0x000F	/* DCDB */
#define DAC960_V1_UnableToStartDevice		0x0002	/* Device */
#define DAC960_V1_InvalidChannelOrTargetOrModifier 0x0105 /* Device */
#define DAC960_V1_ChannelBusy			0x0106	/* Device */
#define DAC960_V1_OutOfMemory			0x0107	/* Device */
#define DAC960_V1_ChannelNotStopped		0x0002	/* Device */
#define DAC960_V1_AttemptToRebuildOnlineDrive	0x0002	/* Consistency */
#define DAC960_V1_RebuildBadBlocksEncountered	0x0003	/* Consistency */
#define DAC960_V1_NewDiskFailedDuringRebuild	0x0004	/* Consistency */
#define DAC960_V1_RebuildOrCheckAlreadyInProgress 0x0106 /* Consistency */
#define DAC960_V1_DependentDiskIsDead		0x0002	/* Consistency */
#define DAC960_V1_InconsistentBlocksFound	0x0003	/* Consistency */
#define DAC960_V1_InvalidOrNonredundantLogicalDrive 0x0105 /* Consistency */
#define DAC960_V1_NoRebuildOrCheckInProgress	0x0105	/* Consistency */
#define DAC960_V1_RebuildInProgress_DataValid	0x0000	/* Consistency */
#define DAC960_V1_RebuildFailed_LogicalDriveFailure 0x0002 /* Consistency */
#define DAC960_V1_RebuildFailed_BadBlocksOnOther 0x0003	/* Consistency */
#define DAC960_V1_RebuildFailed_NewDriveFailed	0x0004	/* Consistency */
#define DAC960_V1_RebuildSuccessful		0x0100	/* Consistency */
#define DAC960_V1_RebuildSuccessfullyTerminated	0x0107	/* Consistency */
#define DAC960_V1_RebuildNotChecked		0x0108	/* Consistency */
#define DAC960_V1_BackgroundInitSuccessful	0x0100	/* Consistency */
#define DAC960_V1_BackgroundInitAborted		0x0005	/* Consistency */
#define DAC960_V1_NoBackgroundInitInProgress	0x0105	/* Consistency */
#define DAC960_V1_AddCapacityInProgress		0x0004	/* Consistency */
#define DAC960_V1_AddCapacityFailedOrSuspended	0x00F4	/* Consistency */
#define DAC960_V1_Config2ChecksumError		0x0002	/* Configuration */
#define DAC960_V1_ConfigurationSuspended	0x0106	/* Configuration */
#define DAC960_V1_FailedToConfigureNVRAM	0x0105	/* Configuration */
#define DAC960_V1_ConfigurationNotSavedStateChange 0x0106 /* Configuration */
#define DAC960_V1_SubsystemNotInstalled		0x0001	/* Subsystem */
#define DAC960_V1_SubsystemFailed		0x0002	/* Subsystem */
#define DAC960_V1_SubsystemBusy			0x0106	/* Subsystem */
#define DAC960_V1_SubsystemTimeout		0x0108  /* Subsystem */

/*
  Define the DAC960 V1 Firmware Enquiry Command reply structure.
*/

typedef struct myrb_enquiry_s
{
	unsigned char ldev_count;			/* Byte 0 */
	unsigned int rsvd1:24;				/* Bytes 1-3 */
	unsigned int ldev_sizes[32];			/* Bytes 4-131 */
	unsigned short flash_age;			/* Bytes 132-133 */
	struct {
		bool deferred:1;			/* Byte 134 Bit 0 */
		bool low_bat:1;				/* Byte 134 Bit 1 */
		unsigned char rsvd2:6;			/* Byte 134 Bits 2-7 */
	} status;
	unsigned char rsvd3:8;				/* Byte 135 */
	unsigned char fw_minor_version;			/* Byte 136 */
	unsigned char fw_major_version;			/* Byte 137 */
	enum {
		DAC960_V1_NoStandbyRebuildOrCheckInProgress =		    0x00,
		DAC960_V1_StandbyRebuildInProgress =			    0x01,
		DAC960_V1_BackgroundRebuildInProgress =			    0x02,
		DAC960_V1_BackgroundCheckInProgress =			    0x03,
		DAC960_V1_StandbyRebuildCompletedWithError =		    0xFF,
		DAC960_V1_BackgroundRebuildOrCheckFailed_DriveFailed =	    0xF0,
		DAC960_V1_BackgroundRebuildOrCheckFailed_LogicalDriveFailed =   0xF1,
		DAC960_V1_BackgroundRebuildOrCheckFailed_OtherCauses =	    0xF2,
		DAC960_V1_BackgroundRebuildOrCheckSuccessfullyTerminated =	    0xF3
	} __attribute__ ((packed)) rbld;		/* Byte 138 */
	unsigned char max_tcq;				/* Byte 139 */
	unsigned char ldev_offline;			/* Byte 140 */
	unsigned char rsvd4:8;				/* Byte 141 */
	unsigned short ev_seq;				/* Bytes 142-143 */
	unsigned char ldev_critical;			/* Byte 144 */
	unsigned int rsvd5:24;				/* Bytes 145-147 */
	unsigned char pdev_dead;			/* Byte 148 */
	unsigned char rsvd6:8;				/* Byte 149 */
	unsigned char rbld_count;			/* Byte 150 */
	struct {
		unsigned char rsvd7:3;			/* Byte 151 Bits 0-2 */
		bool bbu_present:1;			/* Byte 151 Bit 3 */
		unsigned char rsvd8:4;			/* Byte 151 Bits 4-7 */
	} misc;
	struct {
		unsigned char target;
		unsigned char channel;
	} dead_drives[21];				/* Bytes 152-194 */
	unsigned char rsvd9[62];			/* Bytes 195-255 */
}
__attribute__ ((packed))
myrb_enquiry;

/*
  Define the DAC960 V1 Firmware Enquiry2 Command reply structure.
*/

typedef struct myrb_enquiry2_s
{
	struct {
		enum {
			DAC960_V1_P_PD_PU =			0x01,
			DAC960_V1_PL =				0x02,
			DAC960_V1_PG =				0x10,
			DAC960_V1_PJ =				0x11,
			DAC960_V1_PR =				0x12,
			DAC960_V1_PT =				0x13,
			DAC960_V1_PTL0 =			0x14,
			DAC960_V1_PRL =				0x15,
			DAC960_V1_PTL1 =			0x16,
			DAC960_V1_1164P =			0x20
		} __attribute__ ((packed)) SubModel;		/* Byte 0 */
		unsigned char ActualChannels;			/* Byte 1 */
		enum {
			DAC960_V1_FiveChannelBoard =		0x01,
			DAC960_V1_ThreeChannelBoard =		0x02,
			DAC960_V1_TwoChannelBoard =		0x03,
			DAC960_V1_ThreeChannelASIC_DAC =	0x04
		} __attribute__ ((packed)) Model;		/* Byte 2 */
		enum {
			DAC960_V1_EISA_Controller =		0x01,
			DAC960_V1_MicroChannel_Controller =	0x02,
			DAC960_V1_PCI_Controller =		0x03,
			DAC960_V1_SCSItoSCSI_Controller =	0x08
		} __attribute__ ((packed)) ProductFamily;	/* Byte 3 */
	} hw;						/* Bytes 0-3 */
	/* MajorVersion.MinorVersion-FirmwareType-TurnID */
	struct {
		unsigned char MajorVersion;		/* Byte 4 */
		unsigned char MinorVersion;		/* Byte 5 */
		unsigned char TurnID;			/* Byte 6 */
		char FirmwareType;			/* Byte 7 */
	} fw;						/* Bytes 4-7 */
	unsigned int rsvd1;				/* Byte 8-11 */
	unsigned char cfg_chan;				/* Byte 12 */
	unsigned char cur_chan;				/* Byte 13 */
	unsigned char max_targets;			/* Byte 14 */
	unsigned char max_tcq;				/* Byte 15 */
	unsigned char max_ldev;				/* Byte 16 */
	unsigned char max_arms;				/* Byte 17 */
	unsigned char max_spans;			/* Byte 18 */
	unsigned char rsvd2;				/* Byte 19 */
	unsigned int rsvd3;				/* Bytes 20-23 */
	unsigned int mem_size;				/* Bytes 24-27 */
	unsigned int cache_size;			/* Bytes 28-31 */
	unsigned int flash_size;			/* Bytes 32-35 */
	unsigned int nvram_size;			/* Bytes 36-39 */
	struct {
		enum {
			DAC960_V1_RamType_DRAM =		0x0,
			DAC960_V1_RamType_EDO =			0x1,
			DAC960_V1_RamType_SDRAM =		0x2,
			DAC960_V1_RamType_Last =		0x7
		} __attribute__ ((packed)) ram:3;	/* Byte 40 Bits 0-2 */
		enum {
			DAC960_V1_ErrorCorrection_None =	0x0,
			DAC960_V1_ErrorCorrection_Parity =	0x1,
			DAC960_V1_ErrorCorrection_ECC =		0x2,
			DAC960_V1_ErrorCorrection_Last =	0x7
		} __attribute__ ((packed)) ec:3;	/* Byte 40 Bits 3-5 */
		bool fast_page:1;			/* Byte 40 Bit 6 */
		bool low_power:1;			/* Byte 40 Bit 7 */
		unsigned char rsvd4;			/* Bytes 41 */
	} mem_type;
	unsigned short ClockSpeed;			/* Bytes 42-43 */
	unsigned short MemorySpeed;			/* Bytes 44-45 */
	unsigned short HardwareSpeed;			/* Bytes 46-47 */
	unsigned char rsvd5[12];			/* Bytes 48-59 */
	unsigned short max_cmds;			/* Bytes 60-61 */
	unsigned short max_sge;				/* Bytes 62-63 */
	unsigned short max_drv_cmds;			/* Bytes 64-65 */
	unsigned short max_io_desc;			/* Bytes 66-67 */
	unsigned short max_sectors;			/* Bytes 68-69 */
	unsigned char latency;				/* Byte 70 */
	unsigned char rsvd6;				/* Byte 71 */
	unsigned char scsi_tmo;				/* Byte 72 */
	unsigned char rsvd7;				/* Byte 73 */
	unsigned short min_freelines;			/* Bytes 74-75 */
	unsigned char rsvd8[8];				/* Bytes 76-83 */
	unsigned char rbld_rate_const;			/* Byte 84 */
	unsigned char rsvd9[11];			/* Byte 85-95 */
	unsigned short pdrv_block_size;			/* Bytes 96-97 */
	unsigned short ldev_block_size;			/* Bytes 98-99 */
	unsigned short max_blocks_per_cmd;		/* Bytes 100-101 */
	unsigned short block_factor;			/* Bytes 102-103 */
	unsigned short cacheline_size;			/* Bytes 104-105 */
	struct {
		enum {
			DAC960_V1_Narrow_8bit =			0x0,
			DAC960_V1_Wide_16bit =			0x1,
			DAC960_V1_Wide_32bit =			0x2
		} __attribute__ ((packed)) bus_width:2;	/* Byte 106 Bits 0-1 */
		enum {
			DAC960_V1_Fast =			0x0,
			DAC960_V1_Ultra =			0x1,
			DAC960_V1_Ultra2 =			0x2
		} __attribute__ ((packed)) bus_speed:2;	/* Byte 106 Bits 2-3 */
		bool Differential:1;			/* Byte 106 Bit 4 */
		unsigned char rsvd10:3;			/* Byte 106 Bits 5-7 */
	} scsi_cap;
	unsigned char rsvd11[5];			/* Byte 107-111 */
	unsigned short fw_build;			/* Bytes 112-113 */
	enum {
		DAC960_V1_AEMI =				0x01,
		DAC960_V1_OEM1 =				0x02,
		DAC960_V1_OEM2 =				0x04,
		DAC960_V1_OEM3 =				0x08,
		DAC960_V1_Conner =				0x10,
		DAC960_V1_SAFTE =				0x20
	} __attribute__ ((packed)) fault_mgmt;		/* Byte 114 */
	unsigned char rsvd12;				/* Byte 115 */
	struct {
		bool Clustering:1;			/* Byte 116 Bit 0 */
		bool MylexOnlineRAIDExpansion:1;	/* Byte 116 Bit 1 */
		bool ReadAhead:1;			/* Byte 116 Bit 2 */
		bool BackgroundInitialization:1;	/* Byte 116 Bit 3 */
		unsigned int rsvd13:28;			/* Bytes 116-119 */
	} fw_features;
	unsigned char rsvd14[8];			/* Bytes 120-127 */
}
__attribute__((packed))
myrb_enquiry2;


/*
  Define the DAC960 V1 Firmware Logical Drive State type.
*/

typedef enum
{
	DAC960_V1_Device_Dead =			0x00,
	DAC960_V1_Device_WriteOnly =		0x02,
	DAC960_V1_Device_Online =		0x03,
	DAC960_V1_Device_Critical =		0x04,
	DAC960_V1_Device_Standby =		0x10,
	DAC960_V1_Device_Offline =		0xFF
}
__attribute__ ((packed))
myrb_devstate;


/*
 * Define the DAC960 V1 RAID Levels
 */
typedef enum {
	DAC960_V1_RAID_Level0 =		0x0,     /* RAID 0 */
	DAC960_V1_RAID_Level1 =		0x1,     /* RAID 1 */
	DAC960_V1_RAID_Level3 =		0x3,     /* RAID 3 */
	DAC960_V1_RAID_Level5 =		0x5,     /* RAID 5 */
	DAC960_V1_RAID_Level6 =		0x6,     /* RAID 6 */
	DAC960_V1_RAID_JBOD =		0x7,     /* RAID 7 (JBOD) */
}
__attribute__ ((packed))
myrb_raidlevel;

/*
  Define the DAC960 V1 Firmware Logical Drive Information structure.
*/

typedef struct myrb_ldev_info_s
{
	unsigned int Size;				/* Bytes 0-3 */
	myrb_devstate State;				/* Byte 4 */
	unsigned char RAIDLevel:7;			/* Byte 5 Bits 0-6 */
	bool WriteBack:1;				/* Byte 5 Bit 7 */
	unsigned short :16;				/* Bytes 6-7 */
} myrb_ldev_info;


/*
  Define the DAC960 V1 Firmware Get Logical Drive Information Command
  reply structure.
*/

typedef myrb_ldev_info myrb_ldev_info_arr[MYRB_MAX_LDEVS];


/*
  Define the DAC960 V1 Firmware Perform Event Log Operation Types.
*/

#define DAC960_V1_GetEventLogEntry		0x00


/*
  Define the DAC960 V1 Firmware Get Event Log Entry Command reply structure.
*/

typedef struct myrb_log_entry_s
{
	unsigned char MessageType;			/* Byte 0 */
	unsigned char MessageLength;			/* Byte 1 */
	unsigned char TargetID:5;			/* Byte 2 Bits 0-4 */
	unsigned char Channel:3;			/* Byte 2 Bits 5-7 */
	unsigned char LogicalUnit:6;			/* Byte 3 Bits 0-5 */
	unsigned char rsvd1:2;				/* Byte 3 Bits 6-7 */
	unsigned short SequenceNumber;			/* Bytes 4-5 */
	unsigned char SenseData[26];			/* Bytes 6-31 */
}
myrb_log_entry;


/*
  Define the DAC960 V1 Firmware Get Device State Command reply structure.
  The structure is padded by 2 bytes for compatibility with Version 2.xx
  Firmware.
*/

typedef struct myrb_pdev_state_s
{
	bool Present:1;					/* Byte 0 Bit 0 */
	unsigned char :7;				/* Byte 0 Bits 1-7 */
	enum {
		DAC960_V1_OtherType =			0x0,
		DAC960_V1_DiskType =			0x1,
		DAC960_V1_SequentialType =		0x2,
		DAC960_V1_CDROM_or_WORM_Type =		0x3
	} __attribute__ ((packed)) DeviceType:2;	/* Byte 1 Bits 0-1 */
	bool rsvd1:1;					/* Byte 1 Bit 2 */
	bool Fast20:1;					/* Byte 1 Bit 3 */
	bool Sync:1;					/* Byte 1 Bit 4 */
	bool Fast:1;					/* Byte 1 Bit 5 */
	bool Wide:1;					/* Byte 1 Bit 6 */
	bool TaggedQueuingSupported:1;			/* Byte 1 Bit 7 */
	myrb_devstate State;				/* Byte 2 */
	unsigned char rsvd2:8;				/* Byte 3 */
	unsigned char SynchronousMultiplier;		/* Byte 4 */
	unsigned char SynchronousOffset:5;		/* Byte 5 Bits 0-4 */
	unsigned char rsvd3:3;				/* Byte 5 Bits 5-7 */
	unsigned int Size __attribute__ ((packed));	/* Bytes 6-9 */
	unsigned short rsvd4:16;			/* Bytes 10-11 */
} myrb_pdev_state;


/*
  Define the DAC960 V1 Firmware Get Rebuild Progress Command reply structure.
*/

typedef struct myrb_rbld_progress_s
{
	unsigned int ldev_num;				/* Bytes 0-3 */
	unsigned int ldev_size;				/* Bytes 4-7 */
	unsigned int blocks_left;			/* Bytes 8-11 */
}
myrb_rbld_progress;


/*
  Define the DAC960 V1 Firmware Background Initialization Status Command
  reply structure.
*/

typedef struct myrb_bgi_status_s
{
	unsigned int ldev_size;				/* Bytes 0-3 */
	unsigned int blocks_done;			/* Bytes 4-7 */
	unsigned char rsvd1[12];			/* Bytes 8-19 */
	unsigned int ldev_num;				/* Bytes 20-23 */
	unsigned char RAIDLevel;			/* Byte 24 */
	enum {
		MYRB_BGI_INVALID =	0x00,
		MYRB_BGI_STARTED =	0x02,
		MYRB_BGI_INPROGRESS =	0x04,
		MYRB_BGI_SUSPENDED =	0x05,
		MYRB_BGI_CANCELLED =	0x06
	} __attribute__ ((packed)) Status;		/* Byte 25 */
	unsigned char rsvd2[6];				/* Bytes 26-31 */
} myrb_bgi_status;


/*
  Define the DAC960 V1 Firmware Error Table Entry structure.
*/

typedef struct myrb_error_entry_s
{
	unsigned char parity_err;			/* Byte 0 */
	unsigned char soft_err;				/* Byte 1 */
	unsigned char hard_err;				/* Byte 2 */
	unsigned char misc_err;				/* Byte 3 */
}
myrb_error_entry;


/*
  Define the DAC960 V1 Firmware Get Error Table Command reply structure.
*/

typedef struct myrb_error_table_s
{
	myrb_error_entry entries[DAC960_V1_MaxChannels][DAC960_V1_MaxTargets];
}
myrb_error_table;


/*
  Define the DAC960 V1 Firmware Read Config2 Command reply structure.
*/

typedef struct myrb_config2_s
{
	unsigned char :1;				/* Byte 0 Bit 0 */
	bool ActiveNegationEnabled:1;			/* Byte 0 Bit 1 */
	unsigned char :5;				/* Byte 0 Bits 2-6 */
	bool NoRescanIfResetReceivedDuringScan:1;	/* Byte 0 Bit 7 */
	bool StorageWorksSupportEnabled:1;		/* Byte 1 Bit 0 */
	bool HewlettPackardSupportEnabled:1;		/* Byte 1 Bit 1 */
	bool NoDisconnectOnFirstCommand:1;		/* Byte 1 Bit 2 */
	unsigned char :2;				/* Byte 1 Bits 3-4 */
	bool AEMI_ARM:1;				/* Byte 1 Bit 5 */
	bool AEMI_OFM:1;				/* Byte 1 Bit 6 */
	unsigned char :1;				/* Byte 1 Bit 7 */
	enum {
		DAC960_V1_OEMID_Mylex =			0x00,
		DAC960_V1_OEMID_IBM =			0x08,
		DAC960_V1_OEMID_HP =			0x0A,
		DAC960_V1_OEMID_DEC =			0x0C,
		DAC960_V1_OEMID_Siemens =		0x10,
		DAC960_V1_OEMID_Intel =			0x12
	} __attribute__ ((packed)) OEMID;		/* Byte 2 */
	unsigned char OEMModelNumber;			/* Byte 3 */
	unsigned char PhysicalSector;			/* Byte 4 */
	unsigned char LogicalSector;			/* Byte 5 */
	unsigned char BlockFactor;			/* Byte 6 */
	bool ReadAheadEnabled:1;			/* Byte 7 Bit 0 */
	bool LowBIOSDelay:1;				/* Byte 7 Bit 1 */
	unsigned char :2;				/* Byte 7 Bits 2-3 */
	bool ReassignRestrictedToOneSector:1;		/* Byte 7 Bit 4 */
	unsigned char :1;				/* Byte 7 Bit 5 */
	bool ForceUnitAccessDuringWriteRecovery:1;	/* Byte 7 Bit 6 */
	bool EnableLeftSymmetricRAID5Algorithm:1;	/* Byte 7 Bit 7 */
	unsigned char DefaultRebuildRate;		/* Byte 8 */
	unsigned char :8;				/* Byte 9 */
	unsigned char BlocksPerCacheLine;		/* Byte 10 */
	unsigned char BlocksPerStripe;			/* Byte 11 */
	struct {
		enum {
			DAC960_V1_Async =		0x0,
			DAC960_V1_Sync_8MHz =		0x1,
			DAC960_V1_Sync_5MHz =		0x2,
			DAC960_V1_Sync_10or20MHz =	0x3
		} __attribute__ ((packed)) Speed:2;	/* Byte 11 Bits 0-1 */
		bool Force8Bit:1;			/* Byte 11 Bit 2 */
		bool DisableFast20:1;			/* Byte 11 Bit 3 */
		unsigned char :3;			/* Byte 11 Bits 4-6 */
		bool EnableTaggedQueuing:1;		/* Byte 11 Bit 7 */
	} __attribute__ ((packed)) ChannelParameters[6]; /* Bytes 12-17 */
	unsigned char SCSIInitiatorID;			/* Byte 18 */
	unsigned char :8;				/* Byte 19 */
	enum {
		DAC960_V1_StartupMode_ControllerSpinUp =	0x00,
		DAC960_V1_StartupMode_PowerOnSpinUp =	0x01
	} __attribute__ ((packed)) StartupMode;		/* Byte 20 */
	unsigned char SimultaneousDeviceSpinUpCount;	/* Byte 21 */
	unsigned char SecondsDelayBetweenSpinUps;	/* Byte 22 */
	unsigned char Reserved1[29];			/* Bytes 23-51 */
	bool BIOSDisabled:1;				/* Byte 52 Bit 0 */
	bool CDROMBootEnabled:1;			/* Byte 52 Bit 1 */
	unsigned char :3;				/* Byte 52 Bits 2-4 */
	enum {
		DAC960_V1_Geometry_128_32 =		0x0,
		DAC960_V1_Geometry_255_63 =		0x1,
		DAC960_V1_Geometry_Reserved1 =		0x2,
		DAC960_V1_Geometry_Reserved2 =		0x3
	} __attribute__ ((packed)) DriveGeometry:2;	/* Byte 52 Bits 5-6 */
	unsigned char :1;				/* Byte 52 Bit 7 */
	unsigned char Reserved2[9];			/* Bytes 53-61 */
	unsigned short Checksum;			/* Bytes 62-63 */
}
myrb_config2;


/*
  Define the DAC960 V1 Firmware DCDB request structure.
*/

typedef struct myrb_dcdb_s
{
	unsigned char TargetID:4;			 /* Byte 0 Bits 0-3 */
	unsigned char Channel:4;			 /* Byte 0 Bits 4-7 */
	enum {
		DAC960_V1_DCDB_NoDataTransfer =		0,
		DAC960_V1_DCDB_DataTransferDeviceToSystem = 1,
		DAC960_V1_DCDB_DataTransferSystemToDevice = 2,
		DAC960_V1_DCDB_IllegalDataTransfer =	3
	} __attribute__ ((packed)) Direction:2;		/* Byte 1 Bits 0-1 */
	bool EarlyStatus:1;				/* Byte 1 Bit 2 */
	unsigned char :1;				/* Byte 1 Bit 3 */
	enum {
		DAC960_V1_DCDB_Timeout_24_hours =	0,
		DAC960_V1_DCDB_Timeout_10_seconds =	1,
		DAC960_V1_DCDB_Timeout_60_seconds =	2,
		DAC960_V1_DCDB_Timeout_10_minutes =	3
	} __attribute__ ((packed)) Timeout:2;		/* Byte 1 Bits 4-5 */
	bool NoAutomaticRequestSense:1;			/* Byte 1 Bit 6 */
	bool DisconnectPermitted:1;			/* Byte 1 Bit 7 */
	unsigned short xfer_len_lo;			/* Bytes 2-3 */
	u32 BusAddress;					/* Bytes 4-7 */
	unsigned char CDBLength:4;			/* Byte 8 Bits 0-3 */
	unsigned char xfer_len_hi4:4;			/* Byte 8 Bits 4-7 */
	unsigned char SenseLength;			/* Byte 9 */
	unsigned char CDB[12];				/* Bytes 10-21 */
	unsigned char SenseData[64];			/* Bytes 22-85 */
	unsigned char Status;				/* Byte 86 */
	unsigned char :8;				/* Byte 87 */
} myrb_dcdb;


/*
  Define the DAC960 V1 Firmware Scatter/Gather List Type 1 32 Bit Address
  32 Bit Byte Count structure.
*/

typedef struct myrb_sge_s
{
	u32 sge_addr;		/* Bytes 0-3 */
	u32 sge_count;		/* Bytes 4-7 */
} myrb_sge;


/*
  Define the 13 Byte DAC960 V1 Firmware Command Mailbox structure.  Bytes 13-15
  are not used.  The Command Mailbox structure is padded to 16 bytes for
  efficient access.
*/

typedef union myrb_cmd_mbox_s
{
	unsigned int Words[4];				/* Words 0-3 */
	unsigned char Bytes[16];			/* Bytes 0-15 */
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char rsvd[14];			/* Bytes 2-15 */
	} __attribute__ ((packed)) Common;
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char rsvd1[6];			/* Bytes 2-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __attribute__ ((packed)) Type3;
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char optype;			/* Byte 2 */
		unsigned char rsvd1[5];			/* Bytes 3-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __attribute__ ((packed)) Type3B;
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char rsvd1[5];			/* Bytes 2-6 */
		unsigned char ldev_num:6;		/* Byte 7 Bits 0-6 */
		bool AutoRestore:1;			/* Byte 7 Bit 7 */
		unsigned char rsvd2[8];			/* Bytes 8-15 */
	} __attribute__ ((packed)) Type3C;
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char Channel;			/* Byte 2 */
		unsigned char TargetID;			/* Byte 3 */
		myrb_devstate State;			/* Byte 4 */
		unsigned char rsvd1[3];			/* Bytes 5-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __attribute__ ((packed)) Type3D;
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char optype;			/* Byte 2 */
		unsigned char opqual;			/* Byte 3 */
		unsigned short ev_seq;			/* Bytes 4-5 */
		unsigned char rsvd1[2];			/* Bytes 6-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __attribute__ ((packed)) Type3E;
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char rsvd1[2];			/* Bytes 2-3 */
		unsigned char rbld_rate;		/* Byte 4 */
		unsigned char rsvd2[3];			/* Bytes 5-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd3[4];			/* Bytes 12-15 */
	} __attribute__ ((packed)) Type3R;
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned short xfer_len;		/* Bytes 2-3 */
		unsigned int lba;			/* Bytes 4-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char ldev_num;			/* Byte 12 */
		unsigned char rsvd[3];			/* Bytes 13-15 */
	} __attribute__ ((packed)) Type4;
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		struct {
			unsigned short xfer_len:11;	/* Bytes 2-3 */
			unsigned char ldev_num:5;	/* Byte 3 Bits 3-7 */
		} __attribute__ ((packed)) LD;
		unsigned int lba;			/* Bytes 4-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char sg_count:6;		/* Byte 12 Bits 0-5 */
		enum {
			DAC960_V1_ScatterGather_32BitAddress_32BitByteCount = 0x0,
			DAC960_V1_ScatterGather_32BitAddress_16BitByteCount = 0x1,
			DAC960_V1_ScatterGather_32BitByteCount_32BitAddress = 0x2,
			DAC960_V1_ScatterGather_16BitByteCount_32BitAddress = 0x3
		} __attribute__ ((packed)) sg_type:2;	/* Byte 12 Bits 6-7 */
		unsigned char rsvd[3];			/* Bytes 13-15 */
	} __attribute__ ((packed)) Type5;
	struct {
		myrb_cmd_opcode opcode;			/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char CommandOpcode2;		/* Byte 2 */
		unsigned char rsvd1:8;			/* Byte 3 */
		u32 CommandMailboxesBusAddress;		/* Bytes 4-7 */
		u32 StatusMailboxesBusAddress;		/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __attribute__ ((packed)) TypeX;
} myrb_cmd_mbox;


/*
  Define the DAC960 V1 Firmware Controller Status Mailbox structure.
*/

typedef struct myrb_stat_mbox_s
{
	unsigned char id;		/* Byte 0 */
	unsigned char rsvd:7;		/* Byte 1 Bits 0-6 */
	bool valid:1;			/* Byte 1 Bit 7 */
	unsigned short status;		/* Bytes 2-3 */
} myrb_stat_mbox;

typedef struct myrb_cmdblk_s
{
	myrb_cmd_mbox mbox;
	unsigned short status;
	struct completion *Completion;
	myrb_dcdb *dcdb;
	dma_addr_t dcdb_addr;
	myrb_sge *sgl;
	dma_addr_t sgl_addr;
} myrb_cmdblk;

typedef struct myrb_hba_s
{
	unsigned int ldev_block_size;
	unsigned char ldev_geom_heads;
	unsigned char ldev_geom_sectors;
	unsigned char BusWidth;
	unsigned short StripeSize;
	unsigned short SegmentSize;
	unsigned short new_ev_seq;
	unsigned short old_ev_seq;
	bool dual_mode_interface;
	bool bgi_status_supported;
	bool safte_enabled;
	bool need_ldev_info;
	bool need_err_info;
	bool need_rbld;
	bool need_cc_status;
	bool need_bgi_status;
	bool rbld_first;

	struct pci_dev *pdev;
	struct Scsi_Host *host;

	struct workqueue_struct *work_q;
	char work_q_name[20];
	struct delayed_work monitor_work;
	unsigned long primary_monitor_time;
	unsigned long secondary_monitor_time;

	struct dma_pool *sg_pool;
	struct dma_pool *dcdb_pool;

	spinlock_t queue_lock;

	void (*qcmd)(struct myrb_hba_s *, myrb_cmdblk *);
	void (*write_cmd_mbox)(myrb_cmd_mbox *, myrb_cmd_mbox *);
	void (*get_cmd_mbox)(void __iomem *);
	void (*disable_intr)(void __iomem *);
	void (*reset)(void __iomem *);

	unsigned int ctlr_num;
	unsigned char ModelName[20];
	unsigned char FirmwareVersion[12];

	unsigned int irq;
	phys_addr_t io_addr;
	phys_addr_t pci_addr;
	void __iomem *io_base;
	void __iomem *mmio_base;

	size_t cmd_mbox_size;
	dma_addr_t cmd_mbox_addr;
	myrb_cmd_mbox *first_cmd_mbox;
	myrb_cmd_mbox *last_cmd_mbox;
	myrb_cmd_mbox *next_cmd_mbox;
	myrb_cmd_mbox *prev_cmd_mbox1;
	myrb_cmd_mbox *prev_cmd_mbox2;

	size_t stat_mbox_size;
	dma_addr_t stat_mbox_addr;
	myrb_stat_mbox *first_stat_mbox;
	myrb_stat_mbox *last_stat_mbox;
	myrb_stat_mbox *next_stat_mbox;

	myrb_cmdblk dcmd_blk;
	myrb_cmdblk mcmd_blk;
	struct mutex dcmd_mutex;

	myrb_enquiry *enquiry;
	dma_addr_t enquiry_addr;

	myrb_error_table *err_table;
	dma_addr_t err_table_addr;

	unsigned short last_rbld_status;

	myrb_ldev_info_arr *ldev_info_buf;
	dma_addr_t ldev_info_addr;

	myrb_bgi_status bgi_status;

	struct mutex dma_mutex;
} myrb_hba;


/*
  Define the DAC960 LA Series Controller Interface Register Offsets.
*/

#define DAC960_LA_RegisterWindowSize		0x80

typedef enum
{
	DAC960_LA_InterruptMaskRegisterOffset =		0x34,
	DAC960_LA_CommandOpcodeRegisterOffset =		0x50,
	DAC960_LA_CommandIdentifierRegisterOffset =	0x51,
	DAC960_LA_MailboxRegister2Offset =		0x52,
	DAC960_LA_MailboxRegister3Offset =		0x53,
	DAC960_LA_MailboxRegister4Offset =		0x54,
	DAC960_LA_MailboxRegister5Offset =		0x55,
	DAC960_LA_MailboxRegister6Offset =		0x56,
	DAC960_LA_MailboxRegister7Offset =		0x57,
	DAC960_LA_MailboxRegister8Offset =		0x58,
	DAC960_LA_MailboxRegister9Offset =		0x59,
	DAC960_LA_MailboxRegister10Offset =		0x5A,
	DAC960_LA_MailboxRegister11Offset =		0x5B,
	DAC960_LA_MailboxRegister12Offset =		0x5C,
	DAC960_LA_StatusCommandIdentifierRegOffset =	0x5D,
	DAC960_LA_StatusRegisterOffset =		0x5E,
	DAC960_LA_InboundDoorBellRegisterOffset =	0x60,
	DAC960_LA_OutboundDoorBellRegisterOffset =	0x61,
	DAC960_LA_ErrorStatusRegisterOffset =		0x63
}
DAC960_LA_RegisterOffsets_T;


/*
  Define the structure of the DAC960 LA Series Inbound Door Bell Register.
*/

typedef union DAC960_LA_InboundDoorBellRegister
{
	unsigned char All;
	struct {
		bool HardwareMailboxNewCommand:1;		/* Bit 0 */
		bool AcknowledgeHardwareMailboxStatus:1;	/* Bit 1 */
		bool GenerateInterrupt:1;			/* Bit 2 */
		bool ControllerReset:1;				/* Bit 3 */
		bool MemoryMailboxNewCommand:1;			/* Bit 4 */
		unsigned char rsvd1:3;				/* Bits 5-7 */
	} Write;
	struct {
		bool HardwareMailboxEmpty:1;			/* Bit 0 */
		bool InitializationNotInProgress:1;		/* Bit 1 */
		unsigned char rsvd1:6;				/* Bits 2-7 */
	} Read;
}
DAC960_LA_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 LA Series Outbound Door Bell Register.
*/

typedef union DAC960_LA_OutboundDoorBellRegister
{
	unsigned char All;
	struct {
		bool AcknowledgeHardwareMailboxInterrupt:1;	/* Bit 0 */
		bool AcknowledgeMemoryMailboxInterrupt:1;	/* Bit 1 */
		unsigned char rsvd1:6;				/* Bits 2-7 */
	} Write;
	struct {
		bool HardwareMailboxStatusAvailable:1;		/* Bit 0 */
		bool MemoryMailboxStatusAvailable:1;		/* Bit 1 */
		unsigned char rsvd1:6;				/* Bits 2-7 */
	} Read;
}
DAC960_LA_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 LA Series Interrupt Mask Register.
*/

typedef union DAC960_LA_InterruptMaskRegister
{
	unsigned char All;
	struct {
		unsigned char rsvd1:2;				/* Bits 0-1 */
		bool DisableInterrupts:1;			/* Bit 2 */
		unsigned char rsvd2:5;				/* Bits 3-7 */
	} Bits;
}
DAC960_LA_InterruptMaskRegister_T;


/*
  Define the structure of the DAC960 LA Series Error Status Register.
*/

typedef union DAC960_LA_ErrorStatusRegister
{
	unsigned char All;
	struct {
		unsigned int rsvd1:2;				/* Bits 0-1 */
		bool ErrorStatusPending:1;			/* Bit 2 */
		unsigned int rsvd2:5;				/* Bits 3-7 */
	} Bits;
}
DAC960_LA_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 LA Series Controller Interface Registers.
*/

static inline
void DAC960_LA_HardwareMailboxNewCommand(void __iomem *base)
{
	DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_AcknowledgeHardwareMailboxStatus(void __iomem *base)
{
	DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_GenerateInterrupt(void __iomem *base)
{
	DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.GenerateInterrupt = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_ControllerReset(void __iomem *base)
{
	DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.ControllerReset = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_MemoryMailboxNewCommand(void __iomem *base)
{
	DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
bool DAC960_LA_HardwareMailboxFullP(void __iomem *base)
{
	DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readb(base + DAC960_LA_InboundDoorBellRegisterOffset);
	return !InboundDoorBellRegister.Read.HardwareMailboxEmpty;
}

static inline
bool DAC960_LA_InitializationInProgressP(void __iomem *base)
{
	DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readb(base + DAC960_LA_InboundDoorBellRegisterOffset);
	return !InboundDoorBellRegister.Read.InitializationNotInProgress;
}

static inline
void DAC960_LA_AcknowledgeHardwareMailboxInterrupt(void __iomem *base)
{
	DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_LA_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_AcknowledgeMemoryMailboxInterrupt(void __iomem *base)
{
	DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_LA_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_AcknowledgeInterrupt(void __iomem *base)
{
	DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_LA_OutboundDoorBellRegisterOffset);
}

static inline
bool DAC960_LA_HardwareMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readb(base + DAC960_LA_OutboundDoorBellRegisterOffset);
	return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
bool DAC960_LA_MemoryMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readb(base + DAC960_LA_OutboundDoorBellRegisterOffset);
	return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_LA_EnableInterrupts(void __iomem *base)
{
	DAC960_LA_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0xFF;
	InterruptMaskRegister.Bits.DisableInterrupts = false;
	writeb(InterruptMaskRegister.All,
	       base + DAC960_LA_InterruptMaskRegisterOffset);
}

static inline
void DAC960_LA_DisableInterrupts(void __iomem *base)
{
	DAC960_LA_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0xFF;
	InterruptMaskRegister.Bits.DisableInterrupts = true;
	writeb(InterruptMaskRegister.All,
	       base + DAC960_LA_InterruptMaskRegisterOffset);
}

static inline
bool DAC960_LA_InterruptsEnabledP(void __iomem *base)
{
	DAC960_LA_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All =
		readb(base + DAC960_LA_InterruptMaskRegisterOffset);
	return !InterruptMaskRegister.Bits.DisableInterrupts;
}

static inline
void DAC960_LA_WriteCommandMailbox(myrb_cmd_mbox *mem_mbox,
				   myrb_cmd_mbox *mbox)
{
	mem_mbox->Words[1] = mbox->Words[1];
	mem_mbox->Words[2] = mbox->Words[2];
	mem_mbox->Words[3] = mbox->Words[3];
	wmb();
	mem_mbox->Words[0] = mbox->Words[0];
	mb();
}

static inline
void DAC960_LA_WriteHardwareMailbox(void __iomem *base,
				    myrb_cmd_mbox *mbox)
{
	writel(mbox->Words[0],
	       base + DAC960_LA_CommandOpcodeRegisterOffset);
	writel(mbox->Words[1],
	       base + DAC960_LA_MailboxRegister4Offset);
	writel(mbox->Words[2],
	       base + DAC960_LA_MailboxRegister8Offset);
	writeb(mbox->Bytes[12],
	       base + DAC960_LA_MailboxRegister12Offset);
}

static inline unsigned char
DAC960_LA_ReadStatusCommandIdentifier(void __iomem *base)
{
	return readb(base
		     + DAC960_LA_StatusCommandIdentifierRegOffset);
}

static inline unsigned short
DAC960_LA_ReadStatusRegister(void __iomem *base)
{
	return readw(base + DAC960_LA_StatusRegisterOffset);
}

static inline bool
DAC960_LA_ReadErrorStatus(void __iomem *base,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
	DAC960_LA_ErrorStatusRegister_T ErrorStatusRegister;
	ErrorStatusRegister.All =
		readb(base + DAC960_LA_ErrorStatusRegisterOffset);
	if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
	ErrorStatusRegister.Bits.ErrorStatusPending = false;
	*ErrorStatus = ErrorStatusRegister.All;
	*Parameter0 =
		readb(base + DAC960_LA_CommandOpcodeRegisterOffset);
	*Parameter1 =
		readb(base + DAC960_LA_CommandIdentifierRegisterOffset);
	writeb(0xFF, base + DAC960_LA_ErrorStatusRegisterOffset);
	return true;
}

static inline unsigned short
DAC960_LA_MailboxInit(struct pci_dev *pdev, void __iomem *base,
		      myrb_cmd_mbox *mbox)
{
	unsigned short status;
	int timeout = 0;

	while (timeout < MYRB_MAILBOX_TIMEOUT) {
		if (!DAC960_LA_HardwareMailboxFullP(base))
			break;
		udelay(10);
		timeout++;
	}
	if (DAC960_LA_HardwareMailboxFullP(base)) {
		dev_err(&pdev->dev,
			"Timeout waiting for empty mailbox\n");
		return DAC960_V1_SubsystemTimeout;
	}
	DAC960_LA_WriteHardwareMailbox(base, mbox);
	DAC960_LA_HardwareMailboxNewCommand(base);
	timeout = 0;
	while (timeout < MYRB_MAILBOX_TIMEOUT) {
		if (DAC960_LA_HardwareMailboxStatusAvailableP(base))
			break;
		udelay(10);
		timeout++;
	}
	if (!DAC960_LA_HardwareMailboxStatusAvailableP(base)) {
		dev_err(&pdev->dev, "Timeout waiting for mailbox status\n");
		return DAC960_V1_SubsystemTimeout;
	}
	status = DAC960_LA_ReadStatusRegister(base);
	DAC960_LA_AcknowledgeHardwareMailboxInterrupt(base);
	DAC960_LA_AcknowledgeHardwareMailboxStatus(base);

	return status;
}

/*
  Define the DAC960 PG Series Controller Interface Register Offsets.
*/

#define DAC960_PG_RegisterWindowSize		0x2000

typedef enum
{
	DAC960_PG_InboundDoorBellRegisterOffset =	0x0020,
	DAC960_PG_OutboundDoorBellRegisterOffset =	0x002C,
	DAC960_PG_InterruptMaskRegisterOffset =		0x0034,
	DAC960_PG_CommandOpcodeRegisterOffset =		0x1000,
	DAC960_PG_CommandIdentifierRegisterOffset =	0x1001,
	DAC960_PG_MailboxRegister2Offset =		0x1002,
	DAC960_PG_MailboxRegister3Offset =		0x1003,
	DAC960_PG_MailboxRegister4Offset =		0x1004,
	DAC960_PG_MailboxRegister5Offset =		0x1005,
	DAC960_PG_MailboxRegister6Offset =		0x1006,
	DAC960_PG_MailboxRegister7Offset =		0x1007,
	DAC960_PG_MailboxRegister8Offset =		0x1008,
	DAC960_PG_MailboxRegister9Offset =		0x1009,
	DAC960_PG_MailboxRegister10Offset =		0x100A,
	DAC960_PG_MailboxRegister11Offset =		0x100B,
	DAC960_PG_MailboxRegister12Offset =		0x100C,
	DAC960_PG_StatusCommandIdentifierRegOffset =	0x1018,
	DAC960_PG_StatusRegisterOffset =		0x101A,
	DAC960_PG_ErrorStatusRegisterOffset =		0x103F
}
DAC960_PG_RegisterOffsets_T;


/*
  Define the structure of the DAC960 PG Series Inbound Door Bell Register.
*/

typedef union DAC960_PG_InboundDoorBellRegister
{
	unsigned int All;
	struct {
		bool HardwareMailboxNewCommand:1;		/* Bit 0 */
		bool AcknowledgeHardwareMailboxStatus:1;	/* Bit 1 */
		bool GenerateInterrupt:1;			/* Bit 2 */
		bool ControllerReset:1;				/* Bit 3 */
		bool MemoryMailboxNewCommand:1;			/* Bit 4 */
		unsigned int rsvd1:27;				/* Bits 5-31 */
	} Write;
	struct {
		bool HardwareMailboxFull:1;			/* Bit 0 */
		bool InitializationInProgress:1;		/* Bit 1 */
		unsigned int rsvd1:30;				/* Bits 2-31 */
	} Read;
}
DAC960_PG_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 PG Series Outbound Door Bell Register.
*/

typedef union DAC960_PG_OutboundDoorBellRegister
{
	unsigned int All;
	struct {
		bool AcknowledgeHardwareMailboxInterrupt:1;	/* Bit 0 */
		bool AcknowledgeMemoryMailboxInterrupt:1;	/* Bit 1 */
		unsigned int rsvd1:30;				/* Bits 2-31 */
	} Write;
	struct {
		bool HardwareMailboxStatusAvailable:1;		/* Bit 0 */
		bool MemoryMailboxStatusAvailable:1;		/* Bit 1 */
		unsigned int rsvd1:30;				/* Bits 2-31 */
	} Read;
}
DAC960_PG_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 PG Series Interrupt Mask Register.
*/

typedef union DAC960_PG_InterruptMaskRegister
{
	unsigned int All;
	struct {
		unsigned int MessageUnitInterruptMask1:2;	/* Bits 0-1 */
		bool DisableInterrupts:1;			/* Bit 2 */
		unsigned int MessageUnitInterruptMask2:5;	/* Bits 3-7 */
		unsigned int rsvd1:24;				/* Bits 8-31 */
	} Bits;
}
DAC960_PG_InterruptMaskRegister_T;


/*
  Define the structure of the DAC960 PG Series Error Status Register.
*/

typedef union DAC960_PG_ErrorStatusRegister
{
	unsigned char All;
	struct {
		unsigned int rsvd1:2;				/* Bits 0-1 */
		bool ErrorStatusPending:1;			/* Bit 2 */
		unsigned int rsvd2:5;				/* Bits 3-7 */
	} Bits;
}
DAC960_PG_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 PG Series Controller Interface Registers.
*/

static inline
void DAC960_PG_HardwareMailboxNewCommand(void __iomem *base)
{
	DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_AcknowledgeHardwareMailboxStatus(void __iomem *base)
{
	DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_GenerateInterrupt(void __iomem *base)
{
	DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.GenerateInterrupt = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_ControllerReset(void __iomem *base)
{
	DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.ControllerReset = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_MemoryMailboxNewCommand(void __iomem *base)
{
	DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
	writel(InboundDoorBellRegister.All,
	       base + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
bool DAC960_PG_HardwareMailboxFullP(void __iomem *base)
{
	DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readl(base + DAC960_PG_InboundDoorBellRegisterOffset);
	return InboundDoorBellRegister.Read.HardwareMailboxFull;
}

static inline
bool DAC960_PG_InitializationInProgressP(void __iomem *base)
{
	DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readl(base + DAC960_PG_InboundDoorBellRegisterOffset);
	return InboundDoorBellRegister.Read.InitializationInProgress;
}

static inline
void DAC960_PG_AcknowledgeHardwareMailboxInterrupt(void __iomem *base)
{
	DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	writel(OutboundDoorBellRegister.All,
	       base + DAC960_PG_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_AcknowledgeMemoryMailboxInterrupt(void __iomem *base)
{
	DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writel(OutboundDoorBellRegister.All,
	       base + DAC960_PG_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_AcknowledgeInterrupt(void __iomem *base)
{
	DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
	OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
	writel(OutboundDoorBellRegister.All,
	       base + DAC960_PG_OutboundDoorBellRegisterOffset);
}

static inline
bool DAC960_PG_HardwareMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readl(base + DAC960_PG_OutboundDoorBellRegisterOffset);
	return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
bool DAC960_PG_MemoryMailboxStatusAvailableP(void __iomem *base)
{
	DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readl(base + DAC960_PG_OutboundDoorBellRegisterOffset);
	return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_PG_EnableInterrupts(void __iomem *base)
{
	DAC960_PG_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0;
	InterruptMaskRegister.Bits.MessageUnitInterruptMask1 = 0x3;
	InterruptMaskRegister.Bits.DisableInterrupts = false;
	InterruptMaskRegister.Bits.MessageUnitInterruptMask2 = 0x1F;
	writel(InterruptMaskRegister.All,
	       base + DAC960_PG_InterruptMaskRegisterOffset);
}

static inline
void DAC960_PG_DisableInterrupts(void __iomem *base)
{
	DAC960_PG_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All = 0;
	InterruptMaskRegister.Bits.MessageUnitInterruptMask1 = 0x3;
	InterruptMaskRegister.Bits.DisableInterrupts = true;
	InterruptMaskRegister.Bits.MessageUnitInterruptMask2 = 0x1F;
	writel(InterruptMaskRegister.All,
	       base + DAC960_PG_InterruptMaskRegisterOffset);
}

static inline
bool DAC960_PG_InterruptsEnabledP(void __iomem *base)
{
	DAC960_PG_InterruptMaskRegister_T InterruptMaskRegister;
	InterruptMaskRegister.All =
		readl(base + DAC960_PG_InterruptMaskRegisterOffset);
	return !InterruptMaskRegister.Bits.DisableInterrupts;
}

static inline
void DAC960_PG_WriteCommandMailbox(myrb_cmd_mbox *mem_mbox,
				   myrb_cmd_mbox *mbox)
{
	mem_mbox->Words[1] = mbox->Words[1];
	mem_mbox->Words[2] = mbox->Words[2];
	mem_mbox->Words[3] = mbox->Words[3];
	wmb();
	mem_mbox->Words[0] = mbox->Words[0];
	mb();
}

static inline
void DAC960_PG_WriteHardwareMailbox(void __iomem *base,
				    myrb_cmd_mbox *mbox)
{
	writel(mbox->Words[0],
	       base + DAC960_PG_CommandOpcodeRegisterOffset);
	writel(mbox->Words[1],
	       base + DAC960_PG_MailboxRegister4Offset);
	writel(mbox->Words[2],
	       base + DAC960_PG_MailboxRegister8Offset);
	writeb(mbox->Bytes[12],
	       base + DAC960_PG_MailboxRegister12Offset);
}

static inline unsigned char
DAC960_PG_ReadStatusCommandIdentifier(void __iomem *base)
{
	return readb(base
		     + DAC960_PG_StatusCommandIdentifierRegOffset);
}

static inline unsigned short
DAC960_PG_ReadStatusRegister(void __iomem *base)
{
	return readw(base + DAC960_PG_StatusRegisterOffset);
}

static inline bool
DAC960_PG_ReadErrorStatus(void __iomem *base,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
	DAC960_PG_ErrorStatusRegister_T ErrorStatusRegister;
	ErrorStatusRegister.All =
		readb(base + DAC960_PG_ErrorStatusRegisterOffset);
	if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
	ErrorStatusRegister.Bits.ErrorStatusPending = false;
	*ErrorStatus = ErrorStatusRegister.All;
	*Parameter0 = readb(base + DAC960_PG_CommandOpcodeRegisterOffset);
	*Parameter1 = readb(base + DAC960_PG_CommandIdentifierRegisterOffset);
	writeb(0, base + DAC960_PG_ErrorStatusRegisterOffset);
	return true;
}

static inline unsigned short
DAC960_PG_MailboxInit(struct pci_dev *pdev, void __iomem *base,
		      myrb_cmd_mbox *mbox)
{
	unsigned short status;
	int timeout = 0;

	while (timeout < MYRB_MAILBOX_TIMEOUT) {
		if (!DAC960_PG_HardwareMailboxFullP(base))
			break;
		udelay(10);
		timeout++;
	}
	if (DAC960_PG_HardwareMailboxFullP(base)) {
		dev_err(&pdev->dev,
			"Timeout waiting for empty mailbox\n");
		return DAC960_V1_SubsystemTimeout;
	}
	DAC960_PG_WriteHardwareMailbox(base, mbox);
	DAC960_PG_HardwareMailboxNewCommand(base);

	timeout = 0;
	while (timeout < MYRB_MAILBOX_TIMEOUT) {
		if (DAC960_PG_HardwareMailboxStatusAvailableP(base))
			break;
		udelay(10);
		timeout++;
	}
	if (!DAC960_PG_HardwareMailboxStatusAvailableP(base)) {
		dev_err(&pdev->dev,
			"Timeout waiting for mailbox status\n");
		return DAC960_V1_SubsystemTimeout;
	}
	status = DAC960_PG_ReadStatusRegister(base);
	DAC960_PG_AcknowledgeHardwareMailboxInterrupt(base);
	DAC960_PG_AcknowledgeHardwareMailboxStatus(base);

	return status;
}

/*
  Define the DAC960 PD Series Controller Interface Register Offsets.
*/

#define DAC960_PD_RegisterWindowSize		0x80

typedef enum
{
	DAC960_PD_CommandOpcodeRegisterOffset =		0x00,
	DAC960_PD_CommandIdentifierRegisterOffset =	0x01,
	DAC960_PD_MailboxRegister2Offset =		0x02,
	DAC960_PD_MailboxRegister3Offset =		0x03,
	DAC960_PD_MailboxRegister4Offset =		0x04,
	DAC960_PD_MailboxRegister5Offset =		0x05,
	DAC960_PD_MailboxRegister6Offset =		0x06,
	DAC960_PD_MailboxRegister7Offset =		0x07,
	DAC960_PD_MailboxRegister8Offset =		0x08,
	DAC960_PD_MailboxRegister9Offset =		0x09,
	DAC960_PD_MailboxRegister10Offset =		0x0A,
	DAC960_PD_MailboxRegister11Offset =		0x0B,
	DAC960_PD_MailboxRegister12Offset =		0x0C,
	DAC960_PD_StatusCommandIdentifierRegOffset =	0x0D,
	DAC960_PD_StatusRegisterOffset =		0x0E,
	DAC960_PD_ErrorStatusRegisterOffset =		0x3F,
	DAC960_PD_InboundDoorBellRegisterOffset =	0x40,
	DAC960_PD_OutboundDoorBellRegisterOffset =	0x41,
	DAC960_PD_InterruptEnableRegisterOffset =	0x43
}
DAC960_PD_RegisterOffsets_T;


/*
  Define the structure of the DAC960 PD Series Inbound Door Bell Register.
*/

typedef union DAC960_PD_InboundDoorBellRegister
{
	unsigned char All;
	struct {
		bool NewCommand:1;				/* Bit 0 */
		bool AcknowledgeStatus:1;			/* Bit 1 */
		bool GenerateInterrupt:1;			/* Bit 2 */
		bool ControllerReset:1;				/* Bit 3 */
		unsigned char rsvd1:4;				/* Bits 4-7 */
	} Write;
	struct {
		bool MailboxFull:1;				/* Bit 0 */
		bool InitializationInProgress:1;		/* Bit 1 */
		unsigned char rsvd1:6;				/* Bits 2-7 */
	} Read;
}
DAC960_PD_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 PD Series Outbound Door Bell Register.
*/

typedef union DAC960_PD_OutboundDoorBellRegister
{
	unsigned char All;
	struct {
		bool AcknowledgeInterrupt:1;			/* Bit 0 */
		unsigned char rsvd1:7;				/* Bits 1-7 */
	} Write;
	struct {
		bool StatusAvailable:1;				/* Bit 0 */
		unsigned char rsvd1:7;				/* Bits 1-7 */
	} Read;
}
DAC960_PD_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 PD Series Interrupt Enable Register.
*/

typedef union DAC960_PD_InterruptEnableRegister
{
	unsigned char All;
	struct {
		bool EnableInterrupts:1;			/* Bit 0 */
		unsigned char rsvd1:7;				/* Bits 1-7 */
	} Bits;
}
DAC960_PD_InterruptEnableRegister_T;


/*
  Define the structure of the DAC960 PD Series Error Status Register.
*/

typedef union DAC960_PD_ErrorStatusRegister
{
	unsigned char All;
	struct {
		unsigned int rsvd1:2;				/* Bits 0-1 */
		bool ErrorStatusPending:1;			/* Bit 2 */
		unsigned int rsvd2:5;				/* Bits 3-7 */
	} Bits;
}
DAC960_PD_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 PD Series Controller Interface Registers.
*/

static inline
void DAC960_PD_NewCommand(void __iomem *base)
{
	DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.NewCommand = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_PD_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PD_AcknowledgeStatus(void __iomem *base)
{
	DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.AcknowledgeStatus = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_PD_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PD_GenerateInterrupt(void __iomem *base)
{
	DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.GenerateInterrupt = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_PD_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PD_ControllerReset(void __iomem *base)
{
	DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All = 0;
	InboundDoorBellRegister.Write.ControllerReset = true;
	writeb(InboundDoorBellRegister.All,
	       base + DAC960_PD_InboundDoorBellRegisterOffset);
}

static inline
bool DAC960_PD_MailboxFullP(void __iomem *base)
{
	DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readb(base + DAC960_PD_InboundDoorBellRegisterOffset);
	return InboundDoorBellRegister.Read.MailboxFull;
}

static inline
bool DAC960_PD_InitializationInProgressP(void __iomem *base)
{
	DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
	InboundDoorBellRegister.All =
		readb(base + DAC960_PD_InboundDoorBellRegisterOffset);
	return InboundDoorBellRegister.Read.InitializationInProgress;
}

static inline
void DAC960_PD_AcknowledgeInterrupt(void __iomem *base)
{
	DAC960_PD_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All = 0;
	OutboundDoorBellRegister.Write.AcknowledgeInterrupt = true;
	writeb(OutboundDoorBellRegister.All,
	       base + DAC960_PD_OutboundDoorBellRegisterOffset);
}

static inline
bool DAC960_PD_StatusAvailableP(void __iomem *base)
{
	DAC960_PD_OutboundDoorBellRegister_T OutboundDoorBellRegister;
	OutboundDoorBellRegister.All =
		readb(base + DAC960_PD_OutboundDoorBellRegisterOffset);
	return OutboundDoorBellRegister.Read.StatusAvailable;
}

static inline
void DAC960_PD_EnableInterrupts(void __iomem *base)
{
	DAC960_PD_InterruptEnableRegister_T InterruptEnableRegister;
	InterruptEnableRegister.All = 0;
	InterruptEnableRegister.Bits.EnableInterrupts = true;
	writeb(InterruptEnableRegister.All,
	       base + DAC960_PD_InterruptEnableRegisterOffset);
}

static inline
void DAC960_PD_DisableInterrupts(void __iomem *base)
{
	DAC960_PD_InterruptEnableRegister_T InterruptEnableRegister;
	InterruptEnableRegister.All = 0;
	InterruptEnableRegister.Bits.EnableInterrupts = false;
	writeb(InterruptEnableRegister.All,
	       base + DAC960_PD_InterruptEnableRegisterOffset);
}

static inline
bool DAC960_PD_InterruptsEnabledP(void __iomem *base)
{
	DAC960_PD_InterruptEnableRegister_T InterruptEnableRegister;
	InterruptEnableRegister.All =
		readb(base + DAC960_PD_InterruptEnableRegisterOffset);
	return InterruptEnableRegister.Bits.EnableInterrupts;
}

static inline
void DAC960_PD_WriteCommandMailbox(void __iomem *base,
				   myrb_cmd_mbox *mbox)
{
	writel(mbox->Words[0],
	       base + DAC960_PD_CommandOpcodeRegisterOffset);
	writel(mbox->Words[1],
	       base + DAC960_PD_MailboxRegister4Offset);
	writel(mbox->Words[2],
	       base + DAC960_PD_MailboxRegister8Offset);
	writeb(mbox->Bytes[12],
	       base + DAC960_PD_MailboxRegister12Offset);
}

static inline unsigned char
DAC960_PD_ReadStatusCommandIdentifier(void __iomem *base)
{
	return readb(base
		     + DAC960_PD_StatusCommandIdentifierRegOffset);
}

static inline unsigned short
DAC960_PD_ReadStatusRegister(void __iomem *base)
{
	return readw(base + DAC960_PD_StatusRegisterOffset);
}

static inline bool
DAC960_PD_ReadErrorStatus(void __iomem *base,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
	DAC960_PD_ErrorStatusRegister_T ErrorStatusRegister;
	ErrorStatusRegister.All =
		readb(base + DAC960_PD_ErrorStatusRegisterOffset);
	if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
	ErrorStatusRegister.Bits.ErrorStatusPending = false;
	*ErrorStatus = ErrorStatusRegister.All;
	*Parameter0 = readb(base + DAC960_PD_CommandOpcodeRegisterOffset);
	*Parameter1 = readb(base + DAC960_PD_CommandIdentifierRegisterOffset);
	writeb(0, base + DAC960_PD_ErrorStatusRegisterOffset);
	return true;
}

static inline void DAC960_P_To_PD_TranslateEnquiry(void *Enquiry)
{
	memcpy(Enquiry + 132, Enquiry + 36, 64);
	memset(Enquiry + 36, 0, 96);
}

static inline void DAC960_P_To_PD_TranslateDeviceState(void *DeviceState)
{
	memcpy(DeviceState + 2, DeviceState + 3, 1);
	memmove(DeviceState + 4, DeviceState + 5, 2);
	memmove(DeviceState + 6, DeviceState + 8, 4);
}

static inline
void DAC960_PD_To_P_TranslateReadWriteCommand(myrb_cmdblk *cmd_blk)
{
	myrb_cmd_mbox *mbox = &cmd_blk->mbox;
	int ldev_num = mbox->Type5.LD.ldev_num;

	mbox->Bytes[3] &= 0x7;
	mbox->Bytes[3] |= mbox->Bytes[7] << 6;
	mbox->Bytes[7] = ldev_num;
}

static inline
void DAC960_P_To_PD_TranslateReadWriteCommand(myrb_cmdblk *cmd_blk)
{
	myrb_cmd_mbox *mbox = &cmd_blk->mbox;
	int ldev_num = mbox->Bytes[7];

	mbox->Bytes[7] = mbox->Bytes[3] >> 6;
	mbox->Bytes[3] &= 0x7;
	mbox->Bytes[3] |= ldev_num << 3;
}

typedef int (*myrb_hw_init_t)(struct pci_dev *pdev,
			      struct myrb_hba_s *cb, void __iomem *base);
typedef unsigned short (*mbox_mmio_init_t)(struct pci_dev *pdev,
					   void __iomem *base,
					   myrb_cmd_mbox *mbox);

struct myrb_privdata {
	myrb_hw_init_t		HardwareInit;
	irq_handler_t		InterruptHandler;
	unsigned int		MemoryWindowSize;
};


#endif /* MYRB_H */
