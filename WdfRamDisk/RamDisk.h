#pragma once

#include <wdm.h>
#include <wdf.h>
#include <ntdddisk.h>

#define NT_DEVICE_NAME                  L"\\Device\\RamDisk"
#define DOS_DEVICE_NAME                 L"\\DosDevices\\"
#define RAMDISK_TAG                     'DmaR'  // "RamD"
#define DOS_DEVNAME_LENGTH              (sizeof(DOS_DEVICE_NAME)+sizeof(WCHAR)*10)
#define DRIVE_LETTER_LENGTH             (sizeof(WCHAR)*10)
#define DRIVE_LETTER_BUFFER_SIZE        10
#define DOS_DEVNAME_BUFFER_SIZE         (sizeof(DOS_DEVICE_NAME) / 2) + 10
#define RAMDISK_MEDIA_TYPE              0xF8
#define DEFAULT_SECTOR_SIZE             512
#define DEFAULT_REVERSED_SECTOR         32
#define DEFAULT_DISK_SIZE               (500*1024*1024)     // 500 MB
#define DEFAULT_ROOT_DIR_ENTRIES        512
#define DEFAULT_SECTORS_PER_CLUSTER     2
#define DEFAULT_DRIVE_LETTER            L"R:"

#define DBG_END_STRING  " File:%s, Line:%d"
#define MyDbgPrint(s)   DbgPrint((s##DBG_END_STRING), __FILE__, __LINE__)

typedef struct _DISK_INFO {    
    ULONG           RootDirEntries;     // 根目录入口簇号
    ULONG           SectorsPerCluster;  // 每簇的扇区数
    LONGLONG        DiskSize;           // Ramdisk磁盘总大小
    UNICODE_STRING  DriveLetter;        // 驱动器号, "C:"
} DISK_INFO, *PDISK_INFO;

typedef struct _DEVICE_EXTENSION {
    PUCHAR              DiskImage;                  // 磁盘映像的起始地址
    DISK_GEOMETRY       DiskGeometry;               // Ramdisk磁盘参数
    DISK_INFO           DiskRegInfo;                // Ramdisk注册表参数
    UNICODE_STRING      SymbolicLink;               // DOS符号名
    WCHAR               DriveLetterBuffer[DRIVE_LETTER_BUFFER_SIZE];
    WCHAR               DosDeviceNameBuffer[DOS_DEVNAME_BUFFER_SIZE];
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, DeviceGetExtension)

typedef struct _QUEUE_EXTENSION {
    PDEVICE_EXTENSION DeviceExtension;
} QUEUE_EXTENSION, *PQUEUE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_EXTENSION, QueueGetExtension)

#pragma pack(1)

//★说明：引导代码
//FAT文件系统将引导代码与文件形同数据结构融合在一起，FAT32文件系统引导扇区的512字节中，
//90~509字节为引导代码，而FAT12 / 16则是62~509字节为引导代码。
//同时，FAT32还可以利用引导扇区后的扇区空间存放附加的引导代码。
//一个FAT卷即使不是可引导文件文件系统，也会存在引导代码。
typedef struct  _BOOT_SECTOR_FAT32
{
    UCHAR       bsJump[3];          // 跳转指令，EB 58 90
    CCHAR       bsOemName[8];       // 文件系统标志和版本号，RMDK1.0
    USHORT      bsBytesPerSec;      // 每扇区字节数，512
    UCHAR       bsSecPerClus;       // 每簇扇区数，8
    USHORT      bsResSectors;       // 保留扇区数，32
    UCHAR       bsFATs;             // FAT表个数，1
    USHORT      bsRootDirEnts;      // FAT32必须等于0,FAT12/FAT16为根目录中目录的个数
    USHORT      bsSectors;          // FAT32必须等于0,FAT12/FAT16为扇区总数
    UCHAR       bsMedia;            // 哪种存储介质，0xF8标准值，可移动存储介质，常用的 0xF0
    USHORT      bsFATsecs;          // FAT32必须为0，FAT12/FAT16为一个FAT表所占的扇区数
    USHORT      bsSecPerTrack;      // 每磁道扇区数，32
    USHORT      bsHeads;            // 磁头数，1
    ULONG       bsHiddenSecs;       // EBR分区之前所隐藏的扇区数
    ULONG       bsHugeSectors;      // 文件系统总扇区数 0x24
    //----------------------------------------------------------------
    //此部分FAT32特有（区别于FAT12/16）
    ULONG       bsSecPerFAT;        // 每个FAT表占用扇区数
    USHORT      bsFAT32Mark;        // 标记，此域FAT32 特有
    USHORT      bsFAT32Ver;         // FAT32版本号0.0，FAT32特有
    ULONG       bsRootDirClus;      // 根目录所在第一个簇的簇号，2
    USHORT      bsFsInfoSec;        // FSINFO（文件系统信息扇区）扇区号1
    USHORT      bsBackBootSec;      // 备份引导扇区的位置。备份引导扇区总是位于文件系统 的6号扇区
    UCHAR       bsFAT32Ext[12];     // 12字节，用于以后FAT 扩展使用 0x1c
    //----------------------------------------------------------------
    UCHAR       bsDriveNumber;      // Drive Number - not used
    UCHAR       bsReserved1;        // Reserved
    UCHAR       bsBootSignature;    // 扩展引导标志，0x29
    ULONG       bsVolumeID;         // 卷序列号，通常为一个随机值，0x12345678
    CCHAR       bsLabel[11];        // 卷标（ASCII码），如果建立文件系统的时候指定了卷 标，会保存在此
    CCHAR       bsFileSystemType[8];// 文件系统格式的ASCII码，FAT32
    CCHAR       bsReserved2[420];   // 保留字段
    UCHAR       bsSig2[2];          // 签名标志 - 0x55, 0xAA
} BOOT_SECTOR_FAT32, *PBOOT_SECTOR_FAT32;

typedef struct _FSINFO_SECTOR
{
    ULONG       fsExtMark;          // 扩展引导标志，0x52526141
    CCHAR       fsReserved[480];    // 保留字段，全部置0
    ULONG       fsSign;             // FSINFO签名，0x72724161
    ULONG       fsEmptyClus;        // 文件系统的空簇数，0x000EB772
    ULONG       fsNextValClus;      // 下一可用簇号，0x00000015
    CCHAR       fsReserved2[14];    // 14个字节，未使用
    UCHAR       bsSig2[2];          // 签名标志 - 0x55, 0xAA
} FSINFO_SECTOR, *PFSINFO_SECTOR;

typedef struct  _DIR_ENTRY
{
    UCHAR       deName[8];          // 文件名
    UCHAR       deExtension[3];     // 文件扩展名
    UCHAR       deAttributes;       // 文件属性
    UCHAR       deReserved;         // 保留字段
    USHORT      deTime;             // 文件时间
    USHORT      deDate;             // 文件日期
    USHORT      deStartCluster;     // 文件起始簇
    ULONG       deFileSize;         // 文件大小
} DIR_ENTRY, *PDIR_ENTRY;

#pragma pack()

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
rdAddDevice(
    IN WDFDRIVER Driver,
    IN OUT PWDFDEVICE_INIT DeviceInit
);

VOID
rdEvtCleanup(
    IN WDFOBJECT Device
);

VOID
rdEvtIoRead(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
);

VOID
rdEvtIoWrite(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
);

VOID
rdEvtIoDeviceControl(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode
);

VOID
rdQueryDiskParameter(
    IN PWSTR RegPath,
    IN PDISK_INFO DiskInfo
);

VOID
rdFormatDisk(
    IN PDEVICE_EXTENSION DevExt
);