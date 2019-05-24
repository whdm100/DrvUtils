#include "ShadowVolume.h"


NTSTATUS
svDispatchGeneral(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
)
{
    PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(deviceExtension->LowerDevice, Irp);
}

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

        if (NULL != deviceExtension->BitmapOrigin)
        {
            DPBitmapFree(deviceExtension->BitmapOrigin);
        }
        if (NULL != deviceExtension->BitmapRedirect)
        {
            DPBitmapFree(deviceExtension->BitmapRedirect);
        }
        if (NULL != deviceExtension->BitmapPassthru)
        {
            DPBitmapFree(deviceExtension->BitmapPassthru);
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
        //设置完成事件，等待磁盘卷设备完成 IOCTL_VOLUME_ONLINE 请求后，读取磁盘卷设备盘符
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

        if (!NT_SUCCESS(Irp->IoStatus.Status))
        {
            //读取盘符失败，忽略该磁盘的过滤
            g_devExt[(ULONG)deviceExtension->VolumeLetter] = deviceExtension;
        }

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
    }
    
    if (VolumeDosName.Buffer != NULL)
    {
        ExFreePool(VolumeDosName.Buffer);
    }
    
    KeSetEvent(context->SyncEvent, IO_NO_INCREMENT, FALSE);

    return status;
}