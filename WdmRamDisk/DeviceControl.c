#include "RamDisk.h"
#include <mountdev.h>

NTSTATUS
rdDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
)
{
    NTSTATUS status;
    ULONG_PTR information;
    PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);

    status = IoAcquireRemoveLock(&deviceExtension->DeviceRemoveLock, Irp);
    if (!NT_SUCCESS(status))
    {
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = NULL;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
 
    switch (irpsp->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_DISK_GET_PARTITION_INFO:
    {
        PPARTITION_INFORMATION partInfo = (PPARTITION_INFORMATION*)Irp->AssociatedIrp.SystemBuffer;
        ASSERT(partInfo);

        if (irpsp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PARTITION_INFORMATION))
        {
            information = 0;
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            partInfo->StartingOffset.QuadPart = 0;
            partInfo->PartitionLength.QuadPart = deviceExtension->DiskRegInfo.DiskSize;
            partInfo->HiddenSectors = (ULONG)(1L);
            partInfo->PartitionNumber = (ULONG)(-1L);
            partInfo->PartitionType = deviceExtension->DiskRegInfo.PartitionType;
            partInfo->BootIndicator = FALSE;
            partInfo->RecognizedPartition = TRUE;
            partInfo->RewritePartition = FALSE;

            information = (ULONG_PTR)sizeof(PARTITION_INFORMATION);
            status = STATUS_SUCCESS;
        }

    }
        break;

    case IOCTL_DISK_SET_PARTITION_INFO:
    {
        PSET_PARTITION_INFORMATION setPartInfo = (PSET_PARTITION_INFORMATION)Irp->AssociatedIrp.SystemBuffer;
        ASSERT(setPartInfo);
        deviceExtension->DiskRegInfo.PartitionType = setPartInfo->PartitionType;
    }
        break;

    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    {
        PDISK_GEOMETRY diskGeometry = (PDISK_GEOMETRY)Irp->AssociatedIrp.SystemBuffer;
        ASSERT(diskGeometry);
        RtlCopyMemory(diskGeometry, &deviceExtension->DiskGeometry, sizeof(DISK_GEOMETRY));

        information = (ULONG_PTR)sizeof(DISK_GEOMETRY);
    }
        break;

    case IOCTL_DISK_GET_MEDIA_TYPES:
    case IOCTL_STORAGE_GET_MEDIA_TYPES:
        status = STATUS_SUCCESS;
        information = 0;
        break;

    case IOCTL_DISK_CHECK_VERIFY:
    case IOCTL_STORAGE_CHECK_VERIFY:
    case IOCTL_DISK_IS_WRITABLE:
        status = STATUS_SUCCESS;
        information = 0;
        break;

    case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
    {
        PMOUNTDEV_NAME mountName = (PMOUNTDEV_NAME)Irp->AssociatedIrp.SystemBuffer;
        DECLARE_CONST_UNICODE_STRING(deviceName, NT_DEVICE_NAME);
        ASSERT(mountName);

        if (irpsp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(USHORT) + deviceName.Length)
        {
            information = 0;
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            mountName->NameLength = deviceName.Length;
            RtlCopyMemory(mountName->Name, deviceName.Buffer, deviceName.Length);
        }
    }
        break;

    case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
    {
        PMOUNTDEV_UNIQUE_ID devUniqueId = (PMOUNTDEV_UNIQUE_ID)Irp->AssociatedIrp.SystemBuffer;
        DECLARE_CONST_UNICODE_STRING(deviceName, NT_DEVICE_NAME);
        ASSERT(devUniqueId);

        if (irpsp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(USHORT) + deviceName.Length)
        {
            information = 0;
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            devUniqueId->UniqueIdLength = deviceName.Length;
            RtlCopyMemory(devUniqueId->UniqueId, deviceName.Buffer, deviceName.Length);
        }
    }
        break;

    case IOCTL_DISK_MEDIA_REMOVAL:
    case IOCTL_STORAGE_MEDIA_REMOVAL:
        status = STATUS_SUCCESS;
        information = 0;
        break;

    case IOCTL_DISK_GET_LENGTH_INFO:
    {
        PGET_LENGTH_INFORMATION lengthInfo = (PGET_LENGTH_INFORMATION)Irp->AssociatedIrp.SystemBuffer;
        ASSERT(lengthInfo);

        if (irpsp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(GET_LENGTH_INFORMATION))
        {
            information = 0;
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            lengthInfo->Length.QuadPart = deviceExtension->DiskRegInfo.DiskSize;
            information = sizeof(GET_LENGTH_INFORMATION);
            status = STATUS_SUCCESS;
        }        
    }
        break;

    case IOCTL_STORAGE_GET_HOTPLUG_INFO:
    {
        PSTORAGE_HOTPLUG_INFO storageInfo = (PSTORAGE_HOTPLUG_INFO)Irp->AssociatedIrp.SystemBuffer;
        ASSERT(storageInfo);

        if (irpsp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(STORAGE_HOTPLUG_INFO))
        {
            information = 0;
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            storageInfo->Size = sizeof(STORAGE_HOTPLUG_INFO);
            storageInfo->MediaRemovable = FALSE;
            storageInfo->MediaHotplug = FALSE;
            storageInfo->DeviceHotplug = FALSE;
            storageInfo->WriteCacheEnableOverride = 0;

            information = sizeof(STORAGE_HOTPLUG_INFO);
            status = STATUS_SUCCESS;
        }
    }
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        information = 0;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoReleaseRemoveLock(&deviceExtension->DeviceRemoveLock, Irp);
    return status;
}