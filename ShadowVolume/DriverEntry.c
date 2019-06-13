#include "ShadowVolume.h"
#include "DeviceUitls.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, svAddDevice)
#pragma alloc_text(PAGE, svReinitializationRoutine)
#pragma alloc_text(PAGE, svDiskFilterDriverReinit)
#pragma alloc_text(PAGE, svDiskFilterDeviceInit)
#pragma alloc_text(PAGE, svUnload)
#endif //ALLOC_PRAGMA

// 存放
PDEVICE_EXTENSION g_devExt[MAX_PROTECT_VOLUMNE];

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
)
{
    ULONG i;

    UNREFERENCED_PARAMETER(RegistryPath);

    for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        DriverObject->MajorFunction[i] = svDispatchGeneral;
    }

    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]  = svDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_READ]            = svDispatchReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE]           = svDispatchReadWrite;
    DriverObject->MajorFunction[IRP_MJ_PNP]             = svDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER]           = svDispatchPower;

    DriverObject->DriverUnload                          = svUnload;
    DriverObject->DriverExtension->AddDevice            = svAddDevice;

    RtlZeroMemory(g_devExt, sizeof(g_devExt));

    //注册Boot驱动结束回调，在所有Boot驱动运行完成后再执行
    IoRegisterBootDriverReinitialization(
        DriverObject,
        svReinitializationRoutine,
        NULL
    );

    return STATUS_SUCCESS;
}

NTSTATUS 
svDiskFilterQueryVolumeInfo(
    IN OUT PDEVICE_EXTENSION DeviceExtension
)
{
    NTSTATUS                status;
    HANDLE                  volumeHandle = NULL;
    OBJECT_ATTRIBUTES       objAttr;
    IO_STATUS_BLOCK         statusBlock;
    UNICODE_STRING          volumeDosName;
    WCHAR                   dosNameBuffer[10];
    PUCHAR                  bootSectorBuff = NULL;
    PBOOT_SECTOR_FAT        bootSectorFat1x;
    PBOOT_SECTOR_FAT32      bootSectorFat32;
    PBOOT_SECTOR_NTFS       bootSectorNtfs;
    LARGE_INTEGER           startOffset;
    ULONG                   rootDirSectors = 0;
    PARTITION_INFORMATION   partitionInfo;
    
    RtlStringCbPrintfW(dosNameBuffer, sizeof(dosNameBuffer), L"\\??\\%c:", DeviceExtension->VolumeLetter);
    RtlInitUnicodeString(&volumeDosName, dosNameBuffer);

    InitializeObjectAttributes(
        &objAttr,
        &volumeDosName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
    );

    do 
    {
        // 打开磁盘文件对象
        status = ZwCreateFile(
            &volumeHandle,
            GENERIC_ALL | SYNCHRONIZE,
            &objAttr,
            &statusBlock,
            NULL,
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            FILE_OPEN,
            FILE_SYNCHRONOUS_IO_NONALERT,
            NULL,
            0
        );
        VERIFY_STATUS_BREAK(status);

        // 查询磁盘分区信息
        status = ZwDeviceIoControlFile(volumeHandle,
            NULL,
            NULL,
            NULL,
            &statusBlock,
            IOCTL_DISK_GET_PARTITION_INFO,
            NULL,
            0,
            &partitionInfo,
            sizeof(partitionInfo)
        );
        VERIFY_STATUS_BREAK(status);

        RtlCopyMemory(&DeviceExtension->PartitionInfo, &partitionInfo, sizeof(partitionInfo));

        // 读取磁盘首扇区，获取磁盘大小，扇区，数据偏移等信息（FAT12/16, FAT32, NTFS）
        bootSectorBuff = ExAllocatePool(NonPagedPool, DEFAULT_SECTOR_SIZE);
        if (!bootSectorBuff)
        {
            break;
        }

        startOffset.QuadPart = 0;
        status = ZwReadFile(
            volumeHandle, 
            NULL, 
            NULL, 
            NULL, 
            &statusBlock, 
            bootSectorBuff,
            (ULONG)DEFAULT_SECTOR_SIZE,
            &startOffset, 
            NULL
        );
        VERIFY_STATUS_BREAK(status);

        bootSectorFat1x = (PBOOT_SECTOR_FAT)bootSectorBuff;
        bootSectorFat32 = (PBOOT_SECTOR_FAT32)bootSectorBuff;
        bootSectorNtfs  = (PBOOT_SECTOR_NTFS)bootSectorBuff;

        if (*(ULONG*)bootSectorFat1x->bsFileSystemType == '1TAF')
        {            
            DeviceExtension->SectorsPerCluster  = bootSectorFat1x->bsSecPerClus;
            DeviceExtension->BytesPerSector     = bootSectorFat1x->bsBytesPerSec;
            DeviceExtension->VolumeTotalSize    = (ULONGLONG)bootSectorFat1x->bsHugeSectors * (ULONGLONG)bootSectorFat1x->bsBytesPerSec;

            rootDirSectors = (bootSectorFat1x->bsRootDirEnts * 32 + (bootSectorFat1x->bsBytesPerSec - 1)) / bootSectorFat1x->bsBytesPerSec;
            DeviceExtension->FirstDataSector    = (ULONGLONG)(bootSectorFat1x->bsResSectors + bootSectorFat1x->bsFATs * bootSectorFat1x->bsFATsecs +  rootDirSectors);
        }
        else if (*(ULONG*)bootSectorFat32->bsFileSystemType == '3TAF')
        {
            DeviceExtension->SectorsPerCluster  = bootSectorFat32->bsSecPerClus;
            DeviceExtension->BytesPerSector     = bootSectorFat32->bsBytesPerSec;
            DeviceExtension->VolumeTotalSize    = (ULONGLONG)bootSectorFat32->bsHugeSectors * (ULONGLONG)bootSectorFat32->bsBytesPerSec;
            DeviceExtension->FirstDataSector    = (ULONGLONG)(bootSectorFat32->bsResSectors + bootSectorFat32->bsFATs * bootSectorFat32->bsFATsecs);
        }
        else if (*(ULONG*)bootSectorNtfs->bsFSID == 'SFTN')
        {
            DeviceExtension->SectorsPerCluster  = bootSectorNtfs->bsSectorsPerCluster;
            DeviceExtension->BytesPerSector     = bootSectorNtfs->bsBytesPerSector;
            DeviceExtension->VolumeTotalSize    = (ULONGLONG)bootSectorNtfs->bsTotalSectors * (ULONGLONG)bootSectorFat32->bsBytesPerSec;
            DeviceExtension->FirstDataSector    = bootSectorNtfs->bsReservedSectors;
        }
        else
        {
            break;
        }

    } while (FALSE);

    if (NULL != bootSectorBuff)
    {
        ExFreePool(bootSectorBuff);
    }

    if (NULL != volumeHandle)
    {
        ZwClose(volumeHandle);
    }
    
    return status;
}

NTSTATUS
svDiskFilterInitVolumeBitmap(
    IN WCHAR VolumeLetter,
    IN ULONGLONG SectorCount,
    IN ULONG SecsPerClus,
    IN ULONGLONG StartLcn,
    OUT PDP_BITMAP VolumeBitmap
)
{
    NTSTATUS                    status;
    HANDLE                      volumeHandle = NULL;
    OBJECT_ATTRIBUTES           objAttr;
    IO_STATUS_BLOCK             statusBlock;
    UNICODE_STRING              volumeDosName;
    WCHAR                       dosNameBuffer[10];
    STARTING_LCN_INPUT_BUFFER   volumeStartLcn;
    PVOID                       bitmapBuffer = NULL;
    ULONG                       ClusterCount = (ULONG)(SectorCount / SecsPerClus + 1);
    ULONG                       bitmapSize = ClusterCount / DEFAULT_BYTE_SIZE + 1;
    ULONG                       i;
    ULONGLONG                   offset;

    RtlStringCbPrintfW(dosNameBuffer, 10, L"\\??\\%c:", VolumeLetter);
    RtlInitUnicodeString(&volumeDosName, dosNameBuffer);

    InitializeObjectAttributes(
        &objAttr,
        &volumeDosName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
    );

    do
    {
        status = ZwCreateFile(
            &volumeHandle,
            GENERIC_ALL | SYNCHRONIZE,
            &objAttr,
            &statusBlock,
            NULL,
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            FILE_OPEN,
            FILE_SYNCHRONOUS_IO_NONALERT,
            NULL,
            0
        );
        VERIFY_STATUS_BREAK(status);

        bitmapBuffer = ExAllocatePool(NonPagedPool, bitmapSize);
        if (!bitmapBuffer)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        volumeStartLcn.StartingLcn.QuadPart = 0;

        //查询磁盘占用位图，以簇为单位，fat32从数据区开始算起，ntfs从bpb开始算起
        status = ZwFsControlFile(
            volumeHandle,
            NULL,
            NULL,
            NULL,
            &statusBlock,
            FSCTL_GET_VOLUME_BITMAP,
            &volumeStartLcn,
            sizeof(volumeStartLcn),
            bitmapBuffer,
            bitmapSize
        );
        VERIFY_STATUS_BREAK(status);

        //磁盘数据区前的扇区标记已占用
        offset = 0;
        DPBitmapSet(VolumeBitmap, offset, (ULONG)StartLcn);    

        for (i = 0; i < bitmapSize; i++)
        {
            if (BitTest(bitmapBuffer, i))
            {
                offset = StartLcn + (i * SecsPerClus);
                DPBitmapSet(VolumeBitmap, offset, SecsPerClus);
            }
        }

    } while (FALSE);

    if (NULL != bitmapBuffer)
    {
        ExFreePool(bitmapBuffer);
    }

    if (NULL != volumeHandle)
    {
        ZwClose(volumeHandle);
    }

    return status;
}

NTSTATUS
svDiskFilterInitBitmap(
    IN PDEVICE_EXTENSION DeviceExtension
)
{
    NTSTATUS status;
    ULONGLONG sectorCount = DeviceExtension->VolumeTotalSize / DeviceExtension->BytesPerSector + 1;
    ULONG regionNumber = (ULONG)(sectorCount / (DEFAULT_BYTE_SIZE * DEFAULT_REGION_SIZE) + 1);

    do
    {
        status = DPBitmapInit(
            &DeviceExtension->BitmapOrigin,
            DeviceExtension->BytesPerSector,
            DEFAULT_BYTE_SIZE,
            DEFAULT_REGION_SIZE,
            regionNumber
        );
        VERIFY_STATUS_BREAK(status);

        status = DPBitmapInit(
            &DeviceExtension->BitmapPassthru,
            DeviceExtension->BytesPerSector,
            DEFAULT_BYTE_SIZE,
            DEFAULT_REGION_SIZE,
            regionNumber
        );
        VERIFY_STATUS_BREAK(status);

        status = DPBitmapInit(
            &DeviceExtension->BitmapPassthru,
            DeviceExtension->BytesPerSector,
            DEFAULT_BYTE_SIZE,
            DEFAULT_REGION_SIZE,
            regionNumber
        );
        VERIFY_STATUS_BREAK(status);

        //初始化磁盘扇区占用位图
        svDiskFilterInitVolumeBitmap(
            DeviceExtension->VolumeLetter, 
            sectorCount,
            DeviceExtension->SectorsPerCluster,
            DeviceExtension->FirstDataSector,
            DeviceExtension->BitmapOrigin
        );

        //不作过滤的磁盘扇区
        SetBitmapPassFile(
            DeviceExtension->VolumeLetter,
            L"\\Windows\\bootstat.dat",
            DeviceExtension->BitmapPassthru
        );

        SetBitmapPassFile(
            DeviceExtension->VolumeLetter,
            L"\\pagefile.sys",
            DeviceExtension->BitmapPassthru
        );

        SetBitmapPassFile(
            DeviceExtension->VolumeLetter,
            L"\\hiberfil.sys",
            DeviceExtension->BitmapPassthru
        );

        //过滤后的磁盘扇区占用位图
        RtlInitializeGenericTable(
            &DeviceExtension->MapRedirect,
            svCompareRoutine,
            svAllocateRoutine,
            svFreeRoutine,
            NULL
        );

    } while (FALSE);

    return status;
}

NTSTATUS
svDiskFilterDeviceInit(
    IN PDEVICE_EXTENSION DeviceExtension
)
{
    NTSTATUS status;

    do 
    {
        status = svDiskFilterQueryVolumeInfo(DeviceExtension);
        VERIFY_STATUS_BREAK(status);

        status = svDiskFilterInitBitmap(DeviceExtension);
        VERIFY_STATUS_BREAK(status);

        DeviceExtension->EnableProtect = TRUE;
    } while (FALSE);

    return status;
}

VOID
svDiskFilterDriverReinit(
)
{
    ULONG i;
    for (i = 0; i < MAX_PROTECT_VOLUMNE; i++)
    {
        if (NULL != g_devExt[i])
        {
            svDiskFilterDeviceInit(g_devExt[i]);
        }
    }
}

VOID
svReinitializationRoutine(
    IN PDRIVER_OBJECT DriverObject,
    IN OUT PVOID Context,
    IN ULONG Count
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Count);

    svDiskFilterDriverReinit();
}

NTSTATUS
SetBitmapPassFile(
    IN WCHAR VolumeLetter,
    IN PWCHAR FilePath,
    IN OUT PDP_BITMAP VolumeBitmap
)
{
    NTSTATUS                    status;
    HANDLE                      fileHandle = (HANDLE)-1;
    OBJECT_ATTRIBUTES           objAttr;
    IO_STATUS_BLOCK             statusBlock;
    PEPROCESS	                systemProcess = NULL;
    ULONG                       systemProcessId = 0;
    PRETRIEVAL_POINTERS_BUFFER  retrievalPointer = NULL;
    UNICODE_STRING              symbolName;
    UNICODE_STRING              deviceName;
    PWCHAR                      nameBuffer = NULL;
    LARGE_INTEGER               prevVcn;
    ULONG                       i;

    RtlZeroMemory(&deviceName, sizeof(deviceName));

    do 
    {
        symbolName.MaximumLength = MAX_PATH * sizeof(WCHAR);
        symbolName.Length = 0;
        symbolName.Buffer = nameBuffer;

        nameBuffer = ExAllocatePool(PagedPool, symbolName.MaximumLength);
        if (NULL == nameBuffer)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlStringCbPrintfW(nameBuffer, MAX_PATH, L"\\??\\%c", VolumeLetter);
        RtlInitUnicodeString(&symbolName, nameBuffer);

        // \\??\\c => \\Device\\Harddiskvolume
        status = QuerySymbolicLink(&symbolName, &deviceName);
        VERIFY_STATUS_BREAK(status);

        RtlAppendUnicodeToString(&deviceName, FilePath);

        InitializeObjectAttributes(
            &objAttr,
            &deviceName,
            OBJ_CASE_INSENSITIVE,
            NULL,
            NULL
        );

        // 切换到SYSTEM进程
        status = PsLookupProcessByProcessId((PVOID)systemProcessId, &systemProcess);
        VERIFY_STATUS_BREAK(status);

        KeAttachProcess(systemProcess);

        status = ZwCreateFile(
            &fileHandle,
            GENERIC_READ | SYNCHRONIZE,
            &objAttr,
            &statusBlock,
            NULL,
            0,
            FILE_SHARE_READ,
            FILE_OPEN,
            FILE_SYNCHRONOUS_IO_NONALERT,
            NULL,
            0
        );

        // 打开失败，在SYSTEM进程中查找文件句柄
        if (!NT_SUCCESS(status))
        {
            status = SearchFileHandle(&deviceName, &fileHandle);
        }
        VERIFY_STATUS_BREAK(status);

        // 查询文件占用的磁盘簇，标记磁盘位图已占用
        status = QueryClusterUsage(fileHandle, &retrievalPointer);
        VERIFY_STATUS_BREAK(status);

        prevVcn = retrievalPointer->StartingVcn;
        for (i = 0; i < retrievalPointer->ExtentCount; i++)
        {
            DPBitmapSet(
                VolumeBitmap,
                retrievalPointer->Extents[i].Lcn.QuadPart, 
                (ULONG)(retrievalPointer->Extents[i].NextVcn.QuadPart - prevVcn.QuadPart)
            );

            prevVcn = retrievalPointer->Extents[i].NextVcn;
        }

    } while (FALSE);

    if (NULL != nameBuffer)
    {
        ExFreePool(nameBuffer);
    }

    if (NULL != deviceName.Buffer)
    {
        ExFreePool(deviceName.Buffer);
    }

    if (NULL != systemProcess)
    {
        KeDetachProcess();
        ObDereferenceObject(systemProcess);
    }

    if ((HANDLE)-1 != fileHandle)
    {
        ZwClose(fileHandle);
    }

    if (NULL != retrievalPointer)
    {
        ExFreePool(retrievalPointer);
    }

    return status;
}

NTSTATUS
QueryClusterUsage(
    IN HANDLE FileHandle,
    OUT PRETRIEVAL_POINTERS_BUFFER* RetrievalPointer
)
{
    NTSTATUS status;
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER StartVcn;
    PRETRIEVAL_POINTERS_BUFFER pVcnPairs;
    ULONG ulOutPutSize = 0;
    ULONG uCounts = 200;

    StartVcn.QuadPart = 0;
    ulOutPutSize = sizeof(RETRIEVAL_POINTERS_BUFFER) + uCounts * sizeof(pVcnPairs->Extents) + sizeof(LARGE_INTEGER);
    pVcnPairs = (PRETRIEVAL_POINTERS_BUFFER)ExAllocatePool(NonPagedPool, ulOutPutSize);
    if (NULL == pVcnPairs)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    while ((status = ZwFsControlFile(FileHandle, NULL, NULL, 0, &iosb,
        FSCTL_GET_RETRIEVAL_POINTERS,
        &StartVcn, sizeof(LARGE_INTEGER),
        pVcnPairs, ulOutPutSize)) == STATUS_BUFFER_OVERFLOW)
    {
        uCounts += 200;
        ulOutPutSize = sizeof(RETRIEVAL_POINTERS_BUFFER) + uCounts * sizeof(pVcnPairs->Extents) + sizeof(LARGE_INTEGER);
        ExFreePool(pVcnPairs);

        pVcnPairs = (PRETRIEVAL_POINTERS_BUFFER)ExAllocatePool(NonPagedPool, ulOutPutSize);
        if (NULL == pVcnPairs)
        {
            STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (!NT_SUCCESS(status))
    {
        ExFreePool(pVcnPairs);
        return status;
    }

    *RetrievalPointer = pVcnPairs;
    return status;
}

NTSTATUS
svAddDevice(
    PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT PhysicalDeviceObject
)
{
    NTSTATUS status;
    PDEVICE_OBJECT filterDevice = NULL;
    PDEVICE_OBJECT lowerDevice = NULL;
    PDEVICE_EXTENSION deviceExtension = NULL;
    HANDLE threadHandle = NULL;

    PAGED_CODE();

    do 
    {
        status = IoCreateDevice(
            DriverObject,
            sizeof(DEVICE_EXTENSION),
            NULL,
            FILE_DEVICE_DISK,
            FILE_DEVICE_SECURE_OPEN,
            FALSE,
            &filterDevice
        );
        if (!NT_SUCCESS(status))
            break;        

        status = IoAttachDeviceToDeviceStackSafe(
            filterDevice, 
            PhysicalDeviceObject, 
            &lowerDevice
        );
        if (!NT_SUCCESS(status))
            break;

        filterDevice->Flags = lowerDevice->Flags;
        filterDevice->Flags |= DO_POWER_PAGABLE;
        filterDevice->Flags &= ~DO_DEVICE_INITIALIZING;

        deviceExtension = (PDEVICE_EXTENSION)filterDevice->DeviceExtension;
        RtlZeroMemory(deviceExtension, sizeof(DEVICE_EXTENSION));

        KeInitializeEvent(&deviceExtension->DiskPagingEvent, NotificationEvent, TRUE);

        deviceExtension->FilterDevice = filterDevice;
        deviceExtension->TargetDevice = PhysicalDeviceObject;
        deviceExtension->LowerDevice  = lowerDevice;

        InitializeListHead(&deviceExtension->RequestList);
        KeInitializeSpinLock(&deviceExtension->RequestLock);
        KeInitializeEvent(&deviceExtension->RequestEvent, SynchronizationEvent, FALSE);

        deviceExtension->ThreadTerminate = FALSE;

        status = PsCreateSystemThread(
            &threadHandle,
            (ACCESS_MASK)0L,
            NULL,
            NULL,
            NULL,
            svReadWriteThread,
            deviceExtension
        );
        if (!NT_SUCCESS(status))
            break;

        status = ObReferenceObjectByHandle(
            threadHandle,
            THREAD_ALL_ACCESS,
            NULL,
            KernelMode,
            &deviceExtension->ThreadHandle,
            NULL
        );
        if (!NT_SUCCESS(status))
        {
            deviceExtension->ThreadTerminate = TRUE;
            KeSetEvent(&deviceExtension->RequestEvent, (KPRIORITY)0, FALSE);
            break;
        }
        
        status = STATUS_SUCCESS;
    } while (FALSE);

    if (status != STATUS_SUCCESS)
    {
        if (NULL != lowerDevice)
        {
            IoDetachDevice(lowerDevice);
            deviceExtension->LowerDevice = NULL;
        }

        if (NULL != filterDevice)
        {
            IoDeleteDevice(filterDevice);
            deviceExtension->FilterDevice = NULL;
        }
    }

    if (NULL != threadHandle)
    {
        ZwClose(threadHandle);
    }
        
    return status;
}

VOID
svUnload(
    IN	PDRIVER_OBJECT	DriverObject
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    return;
}