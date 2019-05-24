#ifndef _SHADOW_VOLUME_H
#define _SHADOW_VOLUME_H

#include <ntddk.h>
#include <ntddvol.h>
#include <ntdddisk.h>
#include <ntifs.h>
#include <ntstrsafe.h>

#include "DiskBitmap.h"
#include "DeviceUitls.h"

#define MAX_PROTECT_VOLUMNE 26      // 最多保护的磁盘卷数

#define DEFAULT_BYTE_SIZE   8       // 每个字节可用BIT
#define DEFAULT_SECTOR_SIZE 512     // 默认扇区字节大小
#define DEFAULT_REGION_SIZE 256*1024// 默认磁盘位图每块大小

typedef struct  _BOOT_SECTOR_FAT
{
    UCHAR       bsJump[3];                  // 跳转指令，EB 58 90
    CCHAR       bsOemName[8];               // 文件系统标志和版本号
    USHORT      bsBytesPerSec;              // 每扇区字节数
    UCHAR       bsSecPerClus;               // 每簇扇区数
    USHORT      bsResSectors;               // 保留扇区数
    UCHAR       bsFATs;                     // FAT表个数
    USHORT      bsRootDirEnts;              // 根目录中目录的个数
    USHORT      bsSectors;                  // 扇区总数
    UCHAR       bsMedia;                    // 存储介质，0xF8标准值
    USHORT      bsFATsecs;                  // FAT表所占的扇区数
    USHORT      bsSecPerTrack;              // 每磁道扇区数
    USHORT      bsHeads;                    // 磁头数
    ULONG       bsHiddenSecs;               // EBR分区之前所隐藏的扇区数
    ULONG       bsHugeSectors;              // 文件系统总扇区数
    UCHAR       bsDriveNumber;              // Drive Number - not used
    UCHAR       bsReserved1;                // Reserved
    UCHAR       bsBootSignature;            // 扩展引导标志
    ULONG       bsVolumeID;                 // 卷序列号，通常为一个随机值，0x12345678
    CCHAR       bsLabel[11];                // 卷标（ASCII码）
    CCHAR       bsFileSystemType[8];        // 文件系统格式的ASCII码，FAT12 或 FAT16
    CCHAR       bsReserved2[448];           // Reserved
    UCHAR       bsSig2[2];                  // 签名标志 - 0x55, 0xAA
} BOOT_SECTOR_FAT, *PBOOT_SECTOR_FAT;

typedef struct  _BOOT_SECTOR_FAT32 {
    UCHAR       bsJump[3];                  // 跳转指令，EB 58 90
    CCHAR       bsOemName[8];               // 文件系统标志和版本号，RMDK1.0
    USHORT      bsBytesPerSec;              // 每扇区字节数，512
    UCHAR       bsSecPerClus;               // 每簇扇区数，8
    USHORT      bsResSectors;               // 保留扇区数，32
    UCHAR       bsFATs;                     // FAT表个数，1
    USHORT      bsRootDirEnts;              // FAT32必须等于0,FAT12/FAT16为根目录中目录的个数
    USHORT      bsSectors;                  // FAT32必须等于0,FAT12/FAT16为扇区总数
    UCHAR       bsMedia;                    // 哪种存储介质，0xF8标准值，可移动存储介质，常用的 0xF0
    USHORT      bsFATsecs;                  // FAT32必须为0，FAT12/FAT16为一个FAT表所占的扇区数
    USHORT      bsSecPerTrack;              // 每磁道扇区数，32
    USHORT      bsHeads;                    // 磁头数，1
    ULONG       bsHiddenSecs;               // EBR分区之前所隐藏的扇区数
    ULONG       bsHugeSectors;              // 文件系统总扇区数 0x24
    //----------------------------------------------------------------
    //此部分FAT32特有（区别于FAT12/16）
    ULONG       bsSecPerFAT;                // 每个FAT表占用扇区数
    USHORT      bsFAT32Mark;                // 标记，此域FAT32 特有
    USHORT      bsFAT32Ver;                 // FAT32版本号0.0，FAT32特有
    ULONG       bsRootDirClus;              // 根目录所在第一个簇的簇号，2
    USHORT      bsFsInfoSec;                // FSINFO（文件系统信息扇区）扇区号1
    USHORT      bsBackBootSec;              // 备份引导扇区的位置。备份引导扇区总是位于文件系统 的6号扇区
    UCHAR       bsFAT32Ext[12];             // 12字节，用于以后FAT 扩展使用 0x1c
    //----------------------------------------------------------------
    UCHAR       bsDriveNumber;              // Drive Number - not used
    UCHAR       bsReserved1;                // Reserved
    UCHAR       bsBootSignature;            // 扩展引导标志，0x29
    ULONG       bsVolumeID;                 // 卷序列号，通常为一个随机值，0x12345678
    CCHAR       bsLabel[11];                // 卷标（ASCII码），如果建立文件系统的时候指定了卷 标，会保存在此
    CCHAR       bsFileSystemType[8];        // 文件系统格式的ASCII码，FAT32
    CCHAR       bsReserved2[420];           // 保留字段
    UCHAR       bsSig2[2];                  // 签名标志 - 0x55, 0xAA
} BOOT_SECTOR_FAT32, *PBOOT_SECTOR_FAT32;

typedef struct _BOOT_SECTOR_NTFS {
    UCHAR		bsJump[3];					// jump code
    UCHAR		bsFSID[8];					// 'NTFS '
    USHORT		bsBytesPerSector;			// Bytes Per Sector
    UCHAR		bsSectorsPerCluster;		// Sectors Per Cluster
    USHORT		bsReservedSectors;			// Reserved Sectors
    UCHAR		bsMbz1;						// always 0	
    USHORT		bsMbz2;						// always 0
    USHORT		bsReserved1;				// not used by NTFS
    UCHAR		bsMediaDesc;			    // Media Descriptor
    USHORT		bsMbz3;						// always 0
    USHORT		bsSectorsPerTrack;			// Sectors Per Track
    USHORT		bsHeads;					// Number Of Heads
    ULONG		bsHiddenSectors;			// Hidden Sectors
    ULONG		bsReserved2[2];				// not used by NTFS
    ULONGLONG	bsTotalSectors;				// Total Sectors
    ULONGLONG	bsMftStartLcn;				// Logical Cluster Number for the file $MFT
    ULONGLONG	bsMft2StartLcn;				// Logical Cluster Number for the file $MFTMirr
    ULONG       bsClustersPerFileSeg;       // Clusters Per File Record Segment
    UCHAR       bsClustersPerIndexBuf;      // Clusters Per Index Buffer
    UCHAR       bsReserved3[3];             // not used by NTFS
    ULONGLONG   bsVolumeSerialNum;          // Volume Serial Number
    ULONG       bsCheckSum;                 // Checksum
} BOOT_SECTOR_NTFS, *PBOOT_SECTOR_NTFS;

typedef struct _DEVICE_EXTENSION {
    BOOLEAN             EnableProtect;      // 开启磁盘卷保护
    WCHAR               VolumeLetter;       // 磁盘卷盘符
    ULONG               SectorsPerCluster;  // 每簇扇区数
    ULONG               BytesPerSector;     // 每扇区大小
    ULONGLONG           VolumeTotalSize;    // 磁盘卷总大小

    PDP_BITMAP          BitmapOrigin;       // 扇区占用原始位图
    PDP_BITMAP          BitmapRedirect;     // 重定向后转储的扇区
    PDP_BITMAP          BitmapPassthru;     // 不做过滤的扇区,特殊文件扇区不进行转储，页面文件等

    ULONGLONG           FirstDataSector;    // 数据扇区起始位置
    ULONGLONG           NextFreeSector;     // 磁盘空闲扇区偏移
    RTL_GENERIC_TABLE   MapRedirect;        // 扇区重定向表

    ULONG               DiskPagingCount;    // 磁盘开启分页计数
    KEVENT              DiskPagingEvent;    // 磁盘分页等待事件

    PDEVICE_OBJECT      FilterDevice;       // 磁盘卷过滤设备
    PDEVICE_OBJECT      TargetDevice;       // 磁盘卷设备
    PDEVICE_OBJECT      LowerDevice;        // 转发的下一个设备

    LIST_ENTRY          RequestList;        // 卷请求队列
    KSPIN_LOCK          RequestLock;        // 请求队列访问锁
    KEVENT              RequestEvent;       // 请求队列同步事件
    PVOID               ThreadHandle;       // 处理请求事件线程句柄
    BOOLEAN             ThreadTerminate;    // 线程结束标志
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

typedef struct _COMPLETION_CONTEXT {
    PKEVENT             SyncEvent;
    PDEVICE_EXTENSION   DeviceExtension;
} COMPLETION_CONTEXT, *PCOMPLETION_CONTEXT;

typedef struct _SECTOR_MAP_ITEM {
    ULONGLONG   OriginOffset;
    ULONGLONG   RedirectOffset;
} SECTOR_MAP_ITEM, *PSECTOR_MAP_ITEM;

extern PDEVICE_EXTENSION g_devExt[MAX_PROTECT_VOLUMNE];

DRIVER_INITIALIZE DriverEntry;

DRIVER_REINITIALIZE svReinitializationRoutine;

DRIVER_ADD_DEVICE svAddDevice;

DRIVER_UNLOAD svUnload;

DRIVER_DISPATCH svDispatchGeneral;

DRIVER_DISPATCH svDispatchReadWrite;

DRIVER_DISPATCH svDispatchDeviceControl;

DRIVER_DISPATCH svDispatchPnp;

DRIVER_DISPATCH svDispatchPower;

KSTART_ROUTINE svReadWriteThread;

PRTL_GENERIC_ALLOCATE_ROUTINE svAllocateRoutine;

PRTL_GENERIC_COMPARE_ROUTINE svCompareRoutine;

PRTL_GENERIC_FREE_ROUTINE svFreeRoutine;

NTSTATUS
svDiskFilterQueryVolumeInfo(
    IN OUT PDEVICE_EXTENSION DeviceExtension
);

NTSTATUS
svDiskFilterInitVolumeBitmap(
    IN WCHAR VolumeLetter,
    IN ULONGLONG SectorCount,
    IN ULONG SecsPerClus,
    IN ULONGLONG StartLcn,
    OUT PDP_BITMAP VolumeBitmap
);

NTSTATUS
svDiskFilterInitBitmap(
    IN PDEVICE_EXTENSION DeviceExtension
);

NTSTATUS
svDiskFilterDeviceInit(
    IN PDEVICE_EXTENSION DeviceExtension
);

VOID
svDiskFilterDriverReinit(
);

VOID
svReinitializationRoutine(
    IN PDRIVER_OBJECT DriverObject,
    IN OUT PVOID Context,
    IN ULONG Count
);

NTSTATUS
SetBitmapPassFile(
    IN WCHAR VolumeLetter,
    IN PWCHAR FilePath,
    IN OUT PDP_BITMAP VolumeBitmap
);

NTSTATUS
QueryClusterUsage(
    IN PUNICODE_STRING FileName,
    OUT PRETRIEVAL_POINTERS_BUFFER* RetrievalPointer
);

NTSTATUS
SendToLowerDevice(
    IN PDEVICE_OBJECT LowerDevice,
    IN PIRP Irp
);

NTSTATUS
WaitOnLowerDevice(
    IN PDEVICE_OBJECT LowerDevice,
    IN PIRP Irp
);

NTSTATUS
svDispatchPnpCompleteRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context
);

VOID
IoAdjustPagingCount(
    IN volatile PULONG PagingCount,
    IN BOOLEAN InPath
);

WCHAR
ToUpperLetter(
    IN WCHAR Letter
);

NTSTATUS
svDispatchDeviceControlCompleteRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context
);

#define VERIFY_STATUS_BREAK(s) \
    if (!NT_SUCCESS((s)))   \
    {                       \
        break;              \
    }

#endif // _SHADOW_VOLUME_H
