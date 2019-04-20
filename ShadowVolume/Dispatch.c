#include "ShadowVolume.h"

NTSTATUS
svDispatchPnp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);

    PAGED_CODE();

    switch (irpsp->MinorFunction)
    {
    case IRP_MN_REMOVE_DEVICE:
    {
        if (NULL != deviceExtension->ThreadHandle && FALSE != deviceExtension->ThreadTerminate)
        {
            deviceExtension->ThreadTerminate = TRUE;
            KeSetEvent(&deviceExtension->RequestEvent, (KPRIORITY)0L, FALSE);

            KeWaitForSingleObject(
                deviceExtension->ThreadHandle,
                Executive,
                KernelMode,
                FALSE,
                NULL
            );

            ObDereferenceObject(deviceExtension->ThreadHandle);
        }

        if (NULL != deviceExtension->LowerDevice && NULL != deviceExtension->FilterDevice)
        {
            IoCallDriver(deviceExtension->LowerDevice, Irp);
            IoDetachDevice(deviceExtension->LowerDevice);
            IoDeleteDevice(deviceExtension->FilterDevice);
        }

        if (NULL != deviceExtension->DiskBitmap)
        {
            svBitmapFree(deviceExtension->DiskBitmap);
        }

        break;
    }

    case IRP_MN_DEVICE_USAGE_NOTIFICATION:
    {
        BOOLEAN setPagable = FALSE;

        if (DeviceUsageTypePaging != irpsp->Parameters.UsageNotification.Type)
        {
            return SendToLowerDevice(deviceExtension->LowerDevice, Irp);
        }

        KeWaitForSingleObject(
            &deviceExtension->DiskPagingEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );

        if (!irpsp->Parameters.UsageNotification.InPath && deviceExtension->DiskPagingCount == 1)
        {
            if (!(DeviceObject->Flags & DO_POWER_INRUSH))
            {
                DeviceObject->Flags |= DO_POWER_PAGABLE;
                setPagable = TRUE;
            }
        }

        status = WaitOnLowerDevice(deviceExtension->LowerDevice, Irp);

        if (NT_SUCCESS(status))
        {
            IoAdjustPagingCount(
                &deviceExtension->DiskPagingCount, 
                irpsp->Parameters.UsageNotification.InPath
            );

            if (irpsp->Parameters.UsageNotification.InPath)
            {
                if (deviceExtension->DiskPagingCount == 1)
                {
                    DeviceObject->Flags &= ~DO_POWER_PAGABLE;
                }
            }
        }
        else
        {
            if (setPagable)
            {
                DeviceObject->Flags &= ~DO_POWER_PAGABLE;
                setPagable = FALSE;
            }
        }

        KeSetEvent(
            &deviceExtension->DiskPagingEvent,
            IO_NO_INCREMENT,
            FALSE
        );
        
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        break;
    }

    default:
    {
        return SendToLowerDevice(deviceExtension->LowerDevice, Irp);
    }

    }

    return status;
}

NTSTATUS
SendToLowerDevice(
    IN PDEVICE_OBJECT LowerDevice,
    IN PIRP Irp
)
{
    NTSTATUS status;

    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(LowerDevice, Irp);

    return status;
}

NTSTATUS
WaitOnLowerDevice(
    IN PDEVICE_OBJECT LowerDevice,
    IN PIRP Irp
)
{
    NTSTATUS status;
    KEVENT waitEvent;

    KeInitializeEvent(&waitEvent, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, svDispatchPnpCompleteRoutine, (PVOID)&waitEvent, TRUE, TRUE, TRUE);

    IoCallDriver(LowerDevice, Irp);

    status = KeWaitForSingleObject(
        &waitEvent,
        Executive,
        KernelMode,
        FALSE,
        NULL
    );
    
    return status;
}

NTSTATUS
svDispatchPnpCompleteRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context
)
{
    PKEVENT waitEvent = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }

    KeSetEvent(waitEvent, IO_NO_INCREMENT, FALSE);

    return STATUS_SUCCESS;
}

VOID
IoAdjustPagingCount(
    IN volatile PULONG PagingCount,
    IN BOOLEAN InPath
)
{
    if (InPath == TRUE)
    {
        InterlockedIncrement(PagingCount);
    }
    else
    {
        InterlockedDecrement(PagingCount);
    }
}

NTSTATUS
svDispatchPnp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
)
{
    PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
#if (NTDDI_VERSION < NTDDI_VISTA)
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(deviceExtension->LowerDevice, Irp);
#else
    return SendToLowerDevice(deviceExtension->LowerDevice, Irp);
#endif
}

NTSTATUS
svDispatchDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
)
{
    NTSTATUS status;
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    COMPLETION_CONTEXT completionContext;
    KEVENT waitEvent;

    switch (irpsp->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_VOLUME_ONLINE:
    {
        KeInitializeEvent(&waitEvent, SynchronizationEvent, FALSE);
        completionContext.DeviceExtension = deviceExtension;
        completionContext.SyncEvent = &waitEvent;

        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(
            Irp,
            svDispatchDeviceControlCompleteRoutine,
            &completionContext,
            TRUE,
            TRUE,
            TRUE
        );

        status = IoCallDriver(deviceExtension->LowerDevice, Irp);

        KeWaitForSingleObject(
            &waitEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    default:
        break;
    }

    return SendToLowerDevice(deviceExtension->LowerDevice, Irp);
}

WCHAR
ToUpperLetter(
    IN WCHAR Letter
)
{
    WCHAR upperLetter;
    if (Letter >= L'a' && Letter <= L'z')
    {
        upperLetter = Letter & (~0x20);
    }
    else
    {
        upperLetter = Letter;
    }

    return upperLetter;
}

NTSTATUS
QueryVolumeInformation(
    IN PDEVICE_OBJECT DeviceObject,
    OUT PULONG SizePerCluster,
    OUT PULONG SizePerSector,
    OUT PLARGE_INTEGER VolumeTotalSize
)
{
    NTSTATUS            status          = STATUS_SUCCESS;
    PIRP                irp             = NULL;
    PUCHAR              bootSectorBuffer= NULL;
    PBOOT_SECTOR_FAT    bootSectorFat;      
    PBOOT_SECTOR_FAT32  bootSectorFat32;
    PBOOT_SECTOR_NTFS   bootSectorNTFS;
    LARGE_INTEGER       startOffset;
    IO_STATUS_BLOCK     ioStatusBlock;
    KEVENT              waitEvent;

    do 
    {
        bootSectorBuffer = ExAllocatePool(NonPagedPool, DEFAULT_SECTOR_SIZE);
        if (!bootSectorBuffer)
            break;

        startOffset.QuadPart = 0;
        irp = IoBuildAsynchronousFsdRequest(
            IRP_MJ_READ,
            DeviceObject,
            bootSectorBuffer,
            DEFAULT_SECTOR_SIZE,
            &startOffset,
            &ioStatusBlock
        );
        if (!irp)
            break;

        KeInitializeEvent(&waitEvent, NotificationEvent, FALSE);
        IoSetCompletionRoutine(
            irp,
            svDispatchPnpCompleteRoutine,
            &waitEvent,
            TRUE,
            TRUE,
            TRUE
        );

        status = IoCallDriver(DeviceObject, irp);        
        KeWaitForSingleObject(
            &waitEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );

        if (!NT_SUCCESS(irp->IoStatus.Status))
            break;

    } while (FALSE);


    return status;
}

NTSTATUS
svDispatchDeviceControlCompleteRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context
)
{
    NTSTATUS status;
    PCOMPLETION_CONTEXT context = (PCOMPLETION_CONTEXT)Context;
    PDEVICE_EXTENSION deviceExtension = context->DeviceExtension;
    UNICODE_STRING VolumeDosName;

    status = IoVolumeDeviceToDosName(
        deviceExtension->TargetDevice,
        &VolumeDosName
    );

    if (NT_SUCCESS(status))
    {
        deviceExtension->VolumeLetter = ToUpperLetter(VolumeDosName.Buffer[0]);

        status = QueryVolumeInformation(
            deviceExtension->TargetDevice,
            &deviceExtension->SizePerCluster,
            &deviceExtension->SizePerSector,
            &deviceExtension->VolumeTotalSize
        );

        if (NT_SUCCESS(status))
        {

        }

        ExFreePool(VolumeDosName.Buffer);
    }
    
    KeWaitForSingleObject(
        context->SyncEvent,
        Executive,
        KernelMode,
        FALSE,
        NULL
    );

    return status;
}