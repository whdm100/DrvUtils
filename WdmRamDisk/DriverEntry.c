#include "RamDisk.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, rdQueryDiskParameter)
#pragma alloc_text(PAGE, rdAddDevice)
#pragma alloc_text(PAGE, rdUnload)
#endif //ALLOC_PRAGMA

UNICODE_STRING DriverRegistryPath;
PDEVICE_OBJECT RamDiskBusFdo;

const GUID RamDiskBusInterface = { 0x5dc52df0, 0x2f8a, 0x410f, 0x80, 0xe4, 0x05, 0xf8, 0x10, 0xe7, 0xab, 0x8a };

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING  RegistryPath
)
{
    NTSTATUS status;
    PDEVICE_OBJECT targetObject;

    DriverRegistryPath.Length = RegistryPath->Length;
    DriverRegistryPath.MaximumLength = RegistryPath->Length + 2;
    DriverRegistryPath.Buffer = (PWSTR)ExAllocatePoolWithTag(PagedPool, RegistryPath->MaximumLength, RAMDISK_TAG);
    if (!DriverRegistryPath.Buffer) 
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyUnicodeString(&DriverRegistryPath, &RegistryPath);
    
    DriverObject->MajorFunction[IRP_MJ_CREATE] = rdCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = rdCreateClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = rdReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = rdReadWrite;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = rdFlushBuffers;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = rdDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = rdPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = rdPower;
    DriverObject->MajorFunction[IRP_MJ_SCSI] = rdScsi;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = rdSystemControl;

    DriverObject->DriverUnload = rdUnload;
    DriverObject->DriverExtension->AddDevice = rdAddDevice;

    RamDiskBusFdo = NULL;

    status = IoReportDetectedDevice(
        DriverObject, 
        InterfaceTypeUndefined,
        -1, 
        -1, 
        NULL, 
        NULL, 
        FALSE, 
        &targetObject
    );
    if (NT_SUCCESS(status))
    {
        status = rdAddDevice(DriverObject, targetObject);
        if (NT_SUCCESS(status))
        {
            targetObject->Flags &= ~DO_DEVICE_INITIALIZING;
            return STATUS_SUCCESS;
        }
    }

    return status;
}

NTSTATUS
rdQueryDiskParameter(
    IN PWSTR RegistryPath,
    IN PDISK_INFO DiskInfo
)
{
    NTSTATUS status;
    RTL_QUERY_REGISTRY_TABLE queryRegTable[4 + 1];

    PAGED_CODE();

    RtlZeroMemory(queryRegTable, sizeof(queryRegTable));

    queryRegTable[0].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    queryRegTable[0].Name = L"Parameters";
    queryRegTable[0].EntryContext = NULL;
    queryRegTable[0].DefaultType = (ULONG)0;
    queryRegTable[0].DefaultData = NULL;
    queryRegTable[0].DefaultLength = (ULONG)0;

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
        DiskInfo->DiskSize = DEFAULT_DISK_SIZE;
        DiskInfo->SectorsPerCluster = DEFAULT_SECTORS_PER_CLUSTER;
        RtlInitUnicodeString(&DiskInfo->DriveLetter, DEFAULT_DRIVE_LETTER);
    }

    KdPrint(("DiskSize          = 0x%lx\n", DiskInfo->DiskSize));
    KdPrint(("SectorsPerCluster = 0x%lx\n", DiskInfo->SectorsPerCluster));
    KdPrint(("DriveLetter       = %wZ\n", &(DiskInfo->DriveLetter)));
}

NTSTATUS
rdAddDevice(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT TargetObject
)
{
    NTSTATUS status;
    PDEVICE_OBJECT sourceObject;
    PDEVICE_OBJECT topObject;
    PDEVICE_EXTENSION deviceExtension;
    DECLARE_CONST_UNICODE_STRING(deviceName, NT_DEVICE_NAME);

    PAGED_CODE();

    if (RamDiskBusFdo != NULL)
    {
        return STATUS_DEVICE_ALREADY_ATTACHED;
    }

    status = IoCreateDevice(
        DriverObject, 
        sizeof(DEVICE_EXTENSION), 
        &deviceName, 
        FILE_DEVICE_BUS_EXTENDER, 
        FILE_DEVICE_SECURE_OPEN,
        FALSE, 
        &sourceObject
    );
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    deviceExtension = (PDEVICE_EXTENSION)sourceObject->DeviceExtension;
    RtlZeroMemory(deviceExtension, sizeof(DEVICE_EXTENSION));

    KeInitializeEvent(&deviceExtension->SyncEvent, SynchronizationEvent, FALSE);
    IoInitializeRemoveLock(&deviceExtension->DeviceRemoveLock, RAMDISK_TAG, 1, 0);

    status = IoRegisterDeviceInterface(
        TargetObject, 
        &RamDiskBusInterface, 
        NULL, 
        &deviceExtension->SymbolicLink
    );

    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(sourceObject);
        return status;
    }

    topObject = IoAttachDeviceToDeviceStack(sourceObject, TargetObject);
    if (!topObject)
    {
        IoSetDeviceInterfaceState(&deviceExtension->SymbolicLink, FALSE);
        RtlFreeUnicodeString(&deviceExtension->SymbolicLink);
        status = STATUS_NO_SUCH_DEVICE;
        return status;
    }

    deviceExtension->SourceDevice = sourceObject;
    deviceExtension->TargetDevice = TargetObject;
    deviceExtension->TopDevice = topObject;

    sourceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    RamDiskBusFdo = sourceObject;

    status = STATUS_SUCCESS;
    return status;
}