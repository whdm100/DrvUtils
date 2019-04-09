#ifndef _RAM_DISK_H
#define _RAM_DISK_H

#include <ntddk.h>
#include <ntdddisk.h>

#include <wdm.h>

#define NT_DEVICE_NAME                  L"\\Device\\WdmRamDisk"
#define DOS_DEVICE_NAME                 L"\\DosDevices\\"
#define SDDL_ALL                        L"D:P(A;;GA;;;WD)"
#define RAMDISK_TAG                     'DmaR'  // "RamD"
#define DOS_DEVNAME_LENGTH              (sizeof(DOS_DEVICE_NAME)+sizeof(WCHAR)*10)
#define DRIVE_LETTER_LENGTH             (sizeof(WCHAR)*10)
#define DRIVE_LETTER_BUFFER_SIZE        10
#define DOS_DEVNAME_BUFFER_SIZE         (sizeof(DOS_DEVICE_NAME) / 2) + 10
#define RAMDISK_MEDIA_TYPE              0xF8
#define DEFAULT_SECTOR_SIZE             512
#define DEFAULT_REVERSED_SECTOR         32
#define DEFAULT_DISK_SIZE               (100*1024*1024)     // 100 MB
#define DEFAULT_ROOT_DIR_ENTRIES        512
#define DEFAULT_SECTORS_PER_CLUSTER     2
#define DEFAULT_DRIVE_LETTER            L"Z:"

const GUID DECLSPEC_SELECTANY GUID_CLASS_WDMRAMDISK =
{ 0x2145ea58, 0x1213, 0x86a2, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88} };


typedef struct _DISK_INFO {
    UCHAR           PartitionType;      // 分区类型
    ULONG           SectorsPerCluster;  // 每簇的扇区数
    LONGLONG        DiskSize;           // Ramdisk磁盘总大小
    UNICODE_STRING  DriveLetter;        // 驱动器号, "Z:"
} DISK_INFO, *PDISK_INFO;

typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT      SourceDevice;               // RamDisk设备
    PDEVICE_OBJECT      TargetDevice;               // 底层PDO设备
    PDEVICE_OBJECT      TopDevice;                  // 设备栈顶层设备  
    UNICODE_STRING      SymbolicLink;               // DOS符号名
    KEVENT              SyncEvent;                  // 同步IO请求
    IO_REMOVE_LOCK      DeviceRemoveLock;           // 设备删除锁
    PUCHAR              DiskImage;                  // 磁盘映像的起始地址
    DISK_GEOMETRY       DiskGeometry;               // Ramdisk磁盘参数
    DISK_INFO           DiskRegInfo;                // Ramdisk注册表参数
    WCHAR               DriveLetterBuffer[DRIVE_LETTER_BUFFER_SIZE];
    WCHAR               DosDeviceNameBuffer[DOS_DEVNAME_BUFFER_SIZE];
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

DRIVER_ADD_DEVICE rdAddDevice;

DRIVER_UNLOAD rdUnload;

DRIVER_DISPATCH rdReadWrite;

DRIVER_DISPATCH rdFlushBuffers;

DRIVER_DISPATCH rdDeviceControl;

DRIVER_DISPATCH rdCreateClose;

DRIVER_DISPATCH rdPnp;

DRIVER_DISPATCH rdPower;

DRIVER_DISPATCH rdSystemControl;

DRIVER_DISPATCH rdScsi;

NTSTATUS rdCreateRamDisk(
    IN PDEVICE_EXTENSION deviceExtension
);

NTSTATUS
rdQueryDiskParameter(
    IN PWSTR RegistryPath,
    IN PDISK_INFO DiskInfo
);

#endif // _RAM_DISK_H