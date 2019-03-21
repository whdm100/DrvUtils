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

    status = WdfDriverCreate(DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES, 
        &config,
        WDF_NO_HANDLE
        );

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
    PDEVICE_EXTENSION       devExt;
    PQUEUE_EXTENSION        queueExt;
    WDFQUEUE                queue;
    DECLARE_CONST_UNICODE_STRING(devName, NT_DEVICE_NAME);

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

    devExt = DeviceGetExtension(device);

    //初始化设备IO队列
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);

    queueConfig.EvtIoDeviceControl = rdEvtIoDeviceControl;
    queueConfig.EvtIoRead = rdEvtIoRead;
    queueConfig.EvtIoWrite = rdEvtIoWrite;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&queueAttr, QUEUE_EXTENSION);

    status = WdfIoQueueCreate(device,
        &queueConfig,
        &queueAttr,
        &queue
        );
    if (!NT_SUCCESS(status))
    {
        MyDbgPrint("[RamDisk]WdfIoQueueCreate failed.");
        return status;
    }

    queueExt = QueueGetExtension(queue);
    queueExt->DeviceExtension = devExt;

    //初始化磁盘
    devExt->DiskRegInfo.DriveLetter.Buffer = (PWSTR)&devExt->DriveLetterBuffer;
    devExt->DiskRegInfo.DriveLetter.MaximumLength = sizeof(devExt->DriveLetterBuffer);

    //查询RamDisk磁盘配置参数
    rdQueryDiskParameter(
        WdfDriverGetRegistryPath(WdfDeviceGetDriver(device)),
        &devExt->DiskRegInfo
        );

    devExt->DiskImage = ExAllocatePoolWithTag(
        NonPagedPool,
        devExt->DiskRegInfo.DiskSize,
        RAMDISK_TAG
        );

    if (devExt->DiskImage) 
    {

        UNICODE_STRING deviceName;
        UNICODE_STRING win32Name;

        rdFormatDisk(devExt);

        status = STATUS_SUCCESS;

        //创建RamDisk磁盘符号链接
        RtlInitUnicodeString(&win32Name, DOS_DEVICE_NAME);
        RtlInitUnicodeString(&deviceName, NT_DEVICE_NAME);

        devExt->SymbolicLink.Buffer         = (PWSTR)&devExt->DosDeviceNameBuffer;
        devExt->SymbolicLink.MaximumLength  = sizeof(devExt->DosDeviceNameBuffer);
        devExt->SymbolicLink.Length         = win32Name.Length;

        RtlCopyUnicodeString(&devExt->SymbolicLink, &win32Name);
        RtlAppendUnicodeStringToString(&devExt->SymbolicLink, &devExt->DiskRegInfo.DriveLetter);

        status = WdfDeviceCreateSymbolicLink(device, &devExt->SymbolicLink);
    }

    return status;
}

VOID
rdEvtCleanup(
    IN WDFOBJECT Device
)
{
    PDEVICE_EXTENSION devExt = (PDEVICE_EXTENSION)DeviceGetExtension(Device);

    if (devExt->DiskImage)
    {
        ExFreePool(devExt->DiskImage);
    }
}

VOID
rdEvtIoRead(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{
    NTSTATUS                status = STATUS_INVALID_PARAMETER;
    PDEVICE_EXTENSION       devExt = ((PQUEUE_EXTENSION)QueueGetExtension(Queue))->DeviceExtension;
    WDF_REQUEST_PARAMETERS  params;
    LARGE_INTEGER           byteOffset;
    WDFMEMORY               wdfMem;

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(Request, &params);

    byteOffset.QuadPart = params.Parameters.Read.DeviceOffset;

    if (byteOffset.QuadPart + (LONGLONG)Length <= devExt->DiskRegInfo.DiskSize)
    {
        status = WdfRequestRetrieveInputMemory(Request, &wdfMem);
        if (NT_SUCCESS(status))
        {
            status = WdfMemoryCopyFromBuffer(wdfMem,
                0, 
                devExt->DiskImage + byteOffset.QuadPart,
                Length
                );
        }
    }
    WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)Length);
}

VOID
rdEvtIoWrite(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{
    NTSTATUS                status = STATUS_INVALID_PARAMETER;
    PDEVICE_EXTENSION       devExt = ((PQUEUE_EXTENSION)QueueGetExtension(Queue))->DeviceExtension;
    WDF_REQUEST_PARAMETERS  params;
    LARGE_INTEGER           byteOffset;
    WDFMEMORY               wdfMem;

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(Request, &params);

    byteOffset.QuadPart = params.Parameters.Write.DeviceOffset;

    if (byteOffset.QuadPart + (LONGLONG)Length <= devExt->DiskRegInfo.DiskSize)
    {
        status = WdfRequestRetrieveInputMemory(Request, &wdfMem);
        if (NT_SUCCESS(status))
        {
            status = WdfMemoryCopyToBuffer(wdfMem,
                0,
                devExt->DiskImage + byteOffset.QuadPart,
                Length
            );
        }
    }
    WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)Length);
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
    NTSTATUS          status        = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR         information   = 0;
    size_t            bufSize;
    PDEVICE_EXTENSION devExt        = QueueGetExtension(Queue)->DeviceExtension;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) 
    {
    case IOCTL_DISK_GET_PARTITION_INFO:
    {

        PPARTITION_INFORMATION outputBuffer;

        information = sizeof(PARTITION_INFORMATION);

        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(PARTITION_INFORMATION), &outputBuffer, &bufSize);
        if (NT_SUCCESS(status)) 
        {
            outputBuffer->PartitionType             = PARTITION_FAT32;
            outputBuffer->BootIndicator             = FALSE;
            outputBuffer->RecognizedPartition       = TRUE;
            outputBuffer->RewritePartition          = FALSE;
            outputBuffer->StartingOffset.QuadPart   = 0;
            outputBuffer->PartitionLength.QuadPart  = devExt->DiskRegInfo.DiskSize;
            outputBuffer->HiddenSectors             = (ULONG)(1L);
            outputBuffer->PartitionNumber           = (ULONG)(-1L);

            status = STATUS_SUCCESS;
        }
    }
        break;

    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    {

        PDISK_GEOMETRY outputBuffer;

        information = sizeof(DISK_GEOMETRY);

        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(DISK_GEOMETRY), &outputBuffer, &bufSize);
        if (NT_SUCCESS(status)) 
        {
            RtlCopyMemory(outputBuffer, &(devExt->DiskGeometry), sizeof(DISK_GEOMETRY));
            status = STATUS_SUCCESS;
        }
    }
        break;

    case IOCTL_DISK_CHECK_VERIFY:
    case IOCTL_DISK_IS_WRITABLE:
        status = STATUS_SUCCESS;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, information);
}

VOID
rdQueryDiskParameter(
    IN PWSTR RegistryPath,
    IN PDISK_INFO DiskInfo
)
{
    NTSTATUS status;
    RTL_QUERY_REGISTRY_TABLE queryRegTable[4 + 1];

    queryRegTable[0].Flags          = RTL_QUERY_REGISTRY_SUBKEY;
    queryRegTable[0].Name           = L"Parameters";
    queryRegTable[0].EntryContext   = NULL;
    queryRegTable[0].DefaultType    = (ULONG)0;
    queryRegTable[0].DefaultData    = NULL;
    queryRegTable[0].DefaultLength  = (ULONG)0;

    queryRegTable[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
    queryRegTable[1].Name = L"DiskSize";
    queryRegTable[1].EntryContext = &DiskInfo->DiskSize;
    queryRegTable[1].DefaultType = REG_QWORD;
    queryRegTable[1].DefaultData = &DiskInfo->DiskSize;
    queryRegTable[1].DefaultLength = sizeof(LONGLONG);

    queryRegTable[2].Flags = RTL_QUERY_REGISTRY_DIRECT;
    queryRegTable[2].Name = L"SectorsPerCluster";
    queryRegTable[2].EntryContext = &DiskInfo->SectorsPerCluster;
    queryRegTable[2].DefaultType = REG_DWORD;
    queryRegTable[2].DefaultData = &DiskInfo->SectorsPerCluster;
    queryRegTable[2].DefaultLength = sizeof(ULONG);

    queryRegTable[3].Flags = RTL_QUERY_REGISTRY_DIRECT;
    queryRegTable[3].Name = L"DriveLetter";
    queryRegTable[3].EntryContext = &DiskInfo->DriveLetter;
    queryRegTable[3].DefaultType = REG_SZ;
    queryRegTable[3].DefaultData = DiskInfo->DriveLetter.Buffer;
    queryRegTable[3].DefaultLength = 0;

    status = RtlQueryRegistryValues(
        RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
        RegistryPath,
        queryRegTable,
        NULL,
        NULL
    );

    if (!NT_SUCCESS(status))
    {
        DiskInfo->DiskSize          = DEFAULT_DISK_SIZE;
        DiskInfo->SectorsPerCluster = DEFAULT_SECTORS_PER_CLUSTER;
        RtlInitUnicodeString(&DiskInfo->DriveLetter, DEFAULT_DRIVE_LETTER);
    }

    KdPrint(("DiskSize          = 0x%lx\n", DiskInfo->DiskSize));
    KdPrint(("SectorsPerCluster = 0x%lx\n", DiskInfo->SectorsPerCluster));
    KdPrint(("DriveLetter       = %wZ\n"  , &(DiskInfo->DriveLetter)));
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

    PAGED_CODE();

    ASSERT(sizeof(BOOT_SECTOR_FAT32) != DEFAULT_SECTOR_SIZE);
    ASSERT(sizeof(FSINFO_SECTOR) != DEFAULT_SECTOR_SIZE);
    ASSERT(bootSector != NULL);

    //格式化保留扇区
    RtlZeroMemory(DevExt->DiskImage, DEFAULT_SECTOR_SIZE * DEFAULT_REVERSED_SECTOR);

    //初始化磁盘所有参数
    //硬盘容量 ＝ 柱面/磁道数 * 扇区数 * 每扇区字节数 * 磁头数
    DevExt->DiskGeometry.BytesPerSector     = DEFAULT_SECTOR_SIZE; //每个扇区字节数
    DevExt->DiskGeometry.SectorsPerTrack    = 32; //每个磁道扇区数
    DevExt->DiskGeometry.TracksPerCylinder  = 1; //每个柱面磁道数
    DevExt->DiskGeometry.MediaType          = RAMDISK_MEDIA_TYPE;
    DevExt->DiskGeometry.Cylinders.QuadPart = DevExt->DiskRegInfo.DiskSize / (DEFAULT_SECTOR_SIZE * 32 * 1); //柱面数

    KdPrint((
        "[RamDisk]Cylinders: %I64d\n TracksPerCylinder: %lu\n SectorsPerTrack: %lu\n BytesPerSector: %lu\n",
        DevExt->DiskGeometry.Cylinders.QuadPart, DevExt->DiskGeometry.TracksPerCylinder,
        DevExt->DiskGeometry.SectorsPerTrack, DevExt->DiskGeometry.BytesPerSector
        ));

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