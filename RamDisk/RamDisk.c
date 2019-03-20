#include "RamDisk.h"


NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING  RegistryPath
)
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status = STATUS_SUCCESS;

    WDF_DRIVER_CONFIG_INIT(&config, rdAddDevice);
    config.EvtDriverUnload = rdUnload;

    status = WdfDriverCreate(DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES, 
        &config,
        WDF_NO_HANDLE);

    if (!NT_SUCCESS(status))
    {
        MyDbgPrint("[RamDisk]WdfDriverCreate failed.");
    }

    return status;
}

NTSTATUS
rdAddDevice(
    IN WDFDRIVER Driver,
    IN OUT PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS                status;
    WDF_OBJECT_ATTRIBUTES   devAttr;
    WDFDEVICE               device;
    WDF_OBJECT_ATTRIBUTES   queueAttr;
    WDF_IO_QUEUE_CONFIG     queueConfig;
    PDEVICE_EXTENSION       pDevExt;
    PQUEUE_EXTENSION        pQueueExt;
    WDFQUEUE                queue;
    DECLARE_CONST_UNICODE_STRING(devName, RAMDISK_DEVICE_NAME);

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Driver);

    //创建WDF磁盘设备
    status = WdfDeviceInitAssignName(DeviceInit, &devName);
    if (!NT_SUCCESS(status))
    {
        MyDbgPrint("[RamDisk]WdfDeviceInitAssignName failed.");
        return status;
    }

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_DISK);
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&devAttr, DEVICE_EXTENSION);

    devAttr.EvtCleanupCallback = rdEvtCleanup;

    status = WdfDeviceCreate(&DeviceInit, &devAttr, &device);
    if (!NT_SUCCESS(status))
    {
        MyDbgPrint("[RamDisk]WdfDeviceCreate failed.");
        return status;
    }

    pDevExt = DeviceGetExtention(device);

    //初始化设备IO队列
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);

    queueConfig.EvtIoDeviceControl = rdEvtIoDeviceControl;
    queueConfig.EvtIoRead = rdEvtIoRead;
    queueConfig.EvtIoWrite = rdEvtIoWrite;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&queueAttr, QUEUE_EXTENSION);

    status = WdfIoQueueCreate(device,
        &queueConfig,
        &queueAttr,
        &queue);
    if (!NT_SUCCESS(status))
    {
        MyDbgPrint("[RamDisk]WdfIoQueueCreate failed.");
        return status;
    }

    pQueueExt = QueueGetExtension(queue);

    pQueueExt->DeviceExtension = pDevExt;

    //初始化磁盘

    return status;
}

VOID
rdUnload(
    IN WDFDRIVER Driver
)
{

}

VOID
rdEvtCleanup(
    IN WDFOBJECT Device
)
{

}

VOID
rdEvtIoRead(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{

}

VOID
rdEvtIoWrite(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{

}

VOID
rdEvtIoDeviceControl(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode
)
{

}

VOID
rdFormatDisk(
    IN PDEVICE_EXTENSION DevExt
)
{
    PBOOT_SECTOR_FAT32  bootSector = (PBOOT_SECTOR_FAT32)DevExt->DiskImage;
    PFSINFO_SECTOR      fsInfoSector = (PFSINFO_SECTOR)(DevExt->DiskImage + DEFAULT_SECTOR_SIZE);
    PUCHAR              firstFatSector; //FAT表起始扇区
    PDIR_ENTRY          rootDir;        //根目录所在扇区
    ULONG               fatSecCnt;      //扇区总数

    PAGED_CODE();
    ASSERT(sizeof(BOOT_SECTOR_FAT32) != DEFAULT_SECTOR_SIZE);
    ASSERT(sizeof(FSINFO_SECTOR) != DEFAULT_SECTOR_SIZE);
    ASSERT(bootSector != NULL);

    //格式化保留扇区
    RtlZeroMemory(DevExt->DiskRegInfo.DiskSize, DEFAULT_SECTOR_SIZE * DEFAULT_REVERSED_SECTOR);

    //初始化磁盘所有参数
    //硬盘容量 ＝ 柱面/磁道数 * 扇区数 * 每扇区字节数 * 磁头数
    DevExt->DiskGeometry.BytesPerSector     = DEFAULT_SECTOR_SIZE; //每个扇区字节数
    DevExt->DiskGeometry.SectorsPerTrack    = 32; //每个磁道扇区数
    DevExt->DiskGeometry.TracksPerCylinder  = 1; //每个柱面磁道数
    DevExt->DiskGeometry.MediaType          = RAMDISK_MEDIA_TYPE;
    DevExt->DiskGeometry.Cylinders.QuadPart = DevExt->DiskRegInfo.DiskSize / (DEFAULT_SECTOR_SIZE * 32 * 1); //柱面数

    MyDbgPrint(
        "[RamDisk]Cylinders: %I64d\n TracksPerCylinder: %lu\n SectorsPerTrack: %lu\n BytesPerSector: %lu\n",
        DevExt->DiskGeometry.Cylinders.QuadPart, DevExt->DiskGeometry.TracksPerCylinder,
        DevExt->DiskGeometry.SectorsPerTrack, DevExt->DiskGeometry.BytesPerSector
        );

    //初始化DBR扇区
    bootSector->bsJump[0] = 0xEB;
    bootSector->bsJump[1] = 0x58;
    bootSector->bsJump[2] = 0x90;

    bootSector->bsOemName[0] = 'R';
    bootSector->bsOemName[1] = 'M';
    bootSector->bsOemName[2] = 'D';
    bootSector->bsOemName[3] = 'K';
    bootSector->bsOemName[4] = '1';
    bootSector->bsOemName[5] = '.';
    bootSector->bsOemName[6] = '0';
    bootSector->bsOemName[7] = '\0';

    bootSector->bsBytesPerSec   = (USHORT)DevExt->DiskGeometry.BytesPerSector;
    bootSector->bsSecPerClus    = 8;
    bootSector->bsResSectors    = (USHORT)DEFAULT_REVERSED_SECTOR;
    bootSector->bsFATs          = 1;
    bootSector->bsMedia         = (UCHAR)DevExt->DiskGeometry.MediaType;
    bootSector->bsSecPerTrack   = (USHORT)DevExt->DiskGeometry.SectorsPerTrack;
    bootSector->bsHeads         = 1;
    bootSector->bsHugeSectors   = (ULONG)(DevExt->DiskRegInfo.DiskSize / DevExt->DiskGeometry.BytesPerSector);
    bootSector->bsSecPerFAT     = (bootSector->bsHugeSectors - bootSector->bsResSectors) / bootSector->bsSecPerClus + 2;
    bootSector->bsRootDirClus   = 2;
    bootSector->bsFsInfoSec     = 1;
    bootSector->bsBackBootSec   = 6;
    bootSector->bsBootSignature = 0x29;
    bootSector->bsVolumeID      = 0x12345678;

    bootSector->bsLabel[0]  = 'R';
    bootSector->bsLabel[1]  = 'a';
    bootSector->bsLabel[2]  = 'm';
    bootSector->bsLabel[3]  = 'D';
    bootSector->bsLabel[4]  = 'i';
    bootSector->bsLabel[5]  = 's';
    bootSector->bsLabel[6]  = 'k';
    bootSector->bsLabel[7]  = '\0';
    bootSector->bsLabel[8]  = '\0';
    bootSector->bsLabel[9]  = '\0';
    bootSector->bsLabel[10] = '\0';

    bootSector->bsFileSystemType[0] = 'F';
    bootSector->bsFileSystemType[1] = 'A';
    bootSector->bsFileSystemType[2] = 'T';
    bootSector->bsFileSystemType[3] = '3';
    bootSector->bsFileSystemType[4] = '2';
    bootSector->bsFileSystemType[5] = '\0';
    bootSector->bsFileSystemType[6] = '\0';
    bootSector->bsFileSystemType[7] = '\0';

    bootSector->bsSig2[0] = 0x55;
    bootSector->bsSig2[1] = 0xAA;

    //初始化FSINFO分区
    fsInfoSector->fsExtMark     = 0x52526141;
    fsInfoSector->fsSign        = 0x72724161;
    fsInfoSector->fsEmptyClus   = 0xFFFFFFFF;
    fsInfoSector->fsNextValClus = 0x2;

    fsInfoSector->bsSig2[0] = 0x55;
    fsInfoSector->bsSig2[1] = 0xAA;

    //初始化备份分区
    RtlCopyMemory((bootSector + bootSector->bsBackBootSec), bootSector, DevExt->DiskGeometry.BytesPerSector);
    RtlCopyMemory((bootSector + bootSector->bsBackBootSec + 1), bootSector + 1, DevExt->DiskGeometry.BytesPerSector);

    //初始化文件目录表    
    firstFatSector = (PUCHAR)(bootSector + bootSector->bsResSectors);

    RtlZeroMemory(firstFatSector, bootSector->bsSecPerFAT * DevExt->DiskGeometry.BytesPerSector);

    firstFatSector[0] = (UCHAR)DevExt->DiskGeometry.MediaType;
    firstFatSector[1] = 0xFF;
    firstFatSector[2] = 0xFF;
    firstFatSector[3] = 0xFF;

    //初始化根目录（一个根目录）
    rootDir = (PDIR_ENTRY)(bootSector + bootSector->bsResSectors + bootSector->bsSecPerFAT);

    rootDir->deName[0] = 'R';
    rootDir->deName[1] = 'A';
    rootDir->deName[2] = 'M';
    rootDir->deName[3] = 'D';
    rootDir->deName[4] = 'I';
    rootDir->deName[5] = 'S';
    rootDir->deName[6] = 'K';
    rootDir->deName[7] = '1';

    rootDir->deExtension[0] = 'I';
    rootDir->deExtension[1] = 'V';
    rootDir->deExtension[2] = 'E';

    rootDir->deAttributes = 0x08;
}