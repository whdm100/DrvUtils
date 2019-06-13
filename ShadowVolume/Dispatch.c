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

    status = IoCallDriver(LowerDevice, Irp);

    if (STATUS_PENDING == status)
    {
        KeWaitForSingleObject(&waitEvent, Executive, KernelMode, FALSE, NULL);
        status = Irp->IoStatus.Status;
    }
    
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
    IN volatile PLONG PagingCount,
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
svDispatchPower(
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
svDispatchReadWrite(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
)
{
    PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (deviceExtension->EnableProtect)
    {
        if (Irp->PendingReturned)
        {
            IoMarkIrpPending(Irp);
        }

        ExInterlockedInsertTailList(
            &deviceExtension->RequestList,
            &Irp->Tail.Overlay.ListEntry,
            &deviceExtension->RequestLock
        );

        KeSetEvent(
            &deviceExtension->RequestEvent,
            IO_NO_INCREMENT,
            FALSE
        );

        return STATUS_PENDING;
    }
    else
    {
        return SendToLowerDevice(deviceExtension->LowerDevice, Irp);
    }
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

        if (STATUS_PENDING == status)
        {
            KeWaitForSingleObject(&waitEvent, Executive, KernelMode, FALSE, NULL);
            status = Irp->IoStatus.Status;
        }

        if (NT_SUCCESS(status))
        {            
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

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

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

RTL_GENERIC_COMPARE_RESULTS 
NTAPI
svCompareRoutine(
    IN PRTL_GENERIC_TABLE Table,
    IN PVOID FirstStruct,
    IN PVOID SecondStruct
)
{
    PSECTOR_MAP_ITEM firstItem = (PSECTOR_MAP_ITEM)FirstStruct;
    PSECTOR_MAP_ITEM secondItem = (PSECTOR_MAP_ITEM)SecondStruct;

    UNREFERENCED_PARAMETER(Table);

    if (firstItem->OriginOffset < secondItem->OriginOffset)
    {
        return GenericLessThan;
    }
    else if (firstItem->OriginOffset > secondItem->OriginOffset)
    {
        return GenericGreaterThan;
    }
    else
    {
        return GenericEqual;
    }
}

PVOID
NTAPI
svAllocateRoutine(
    IN PRTL_GENERIC_TABLE Table,
    IN CLONG ByteSize
)
{
    UNREFERENCED_PARAMETER(Table);

    return ExAllocatePool(NonPagedPool, ByteSize);
}

VOID 
NTAPI
svFreeRoutine(
    IN PRTL_GENERIC_TABLE Table,
    IN PVOID Buffer
)
{
    UNREFERENCED_PARAMETER(Table);

    ExFreePool(Buffer);
}

PVOID
svGetIrpBuffer(
    IN PIRP Irp
)
{
    PVOID buffer;

    if (Irp->MdlAddress)
    {
        buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority); // METHOD_DIRECT
    }
    else if (Irp->UserBuffer)
    {
        buffer = Irp->UserBuffer; // METHOD_NEITHER
    }
    else
    {
        buffer = Irp->AssociatedIrp.SystemBuffer; // METHOD_BUFFERED
    }

    return buffer;
}

VOID
svReadWriteThread(
    IN PVOID StartContext
)
{
    NTSTATUS status;
    PDEVICE_EXTENSION deviceExtension = StartContext;
    PIRP irp;
    PIO_STACK_LOCATION irpsp;
    PLIST_ENTRY reqEntry;
    PVOID buffer;
    ULONGLONG offset;
    ULONG length;
    PVOID newBuffer;

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    for (;;)
    {
        status = KeWaitForSingleObject(
            &deviceExtension->RequestEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );

        if (TRUE == deviceExtension->ThreadTerminate)
        {
            PsTerminateSystemThread(STATUS_SUCCESS);
            break;
        }

        while (NULL != (reqEntry = ExInterlockedRemoveHeadList(
            &deviceExtension->RequestList,
            &deviceExtension->RequestLock
            )))
        {
            irp = CONTAINING_RECORD(reqEntry, IRP, Tail.Overlay.ListEntry);
            irpsp = IoGetCurrentIrpStackLocation(irp);
            buffer = svGetIrpBuffer(irp);

            if (!buffer)
            {
                SendToLowerDevice(deviceExtension->LowerDevice, irp);
                continue;
            }            

            switch (irpsp->MajorFunction)
            {
            case IRP_MJ_READ:
            {
                offset = irpsp->Parameters.Read.ByteOffset.QuadPart;
                length = irpsp->Parameters.Read.Length;

                newBuffer = ExAllocatePool(NonPagedPool, length);

                if (newBuffer)
                {
                    status = svHandleDiskRequest(deviceExtension, IRP_MJ_READ, irp, newBuffer, offset, length);
                    if (NT_SUCCESS(status))
                    {
                        RtlCopyMemory(buffer, newBuffer, length);
                        irp->IoStatus.Information = (ULONG_PTR)length;
                    }
                    else
                    {
                        irp->IoStatus.Information = (ULONG_PTR)0;
                    }

                    ExFreePool(newBuffer);
                    irp->IoStatus.Status = status;
                }
                else
                {
                    irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                }
                
                IoCompleteRequest(irp, IO_NO_INCREMENT);
            }
            break;

            case IRP_MJ_WRITE:
            {
                offset = irpsp->Parameters.Write.ByteOffset.QuadPart;
                length = irpsp->Parameters.Write.Length;

                newBuffer = ExAllocatePool(NonPagedPool, length);

                if (newBuffer)
                {
                    RtlCopyMemory(newBuffer, buffer, length);

                    status = svHandleDiskRequest(deviceExtension, IRP_MJ_WRITE, irp, newBuffer, offset, length);
                    if (NT_SUCCESS(status))
                    {
                        irp->IoStatus.Information = (ULONG_PTR)length;
                    }
                    else
                    {
                        irp->IoStatus.Information = (ULONG_PTR)0;
                    }

                    ExFreePool(newBuffer);
                    irp->IoStatus.Status = status;
                }
                else
                {
                    irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                }

                IoCompleteRequest(irp, IO_NO_INCREMENT);
            }
            break;

            default:
            {
                status = SendToLowerDevice(deviceExtension->LowerDevice, irp);
            }
            break;
            }
        }
    }
}

NTSTATUS
svReadWriteSectorsCompletion(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context
)
{
    if (((DeviceObject->Flags & DO_DIRECT_IO) == DO_DIRECT_IO) &&
        (Irp->MdlAddress != NULL))
    {
        MmUnlockPages(Irp->MdlAddress);
    }
    IoFreeMdl(Irp->MdlAddress);

    if (Irp->PendingReturned && (Context != NULL)) {
        *Irp->UserIosb = Irp->IoStatus;
        KeSetEvent((PKEVENT)Context, IO_DISK_INCREMENT, FALSE);
    }

    IoFreeIrp(Irp);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
svRealDiskRequest(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN ULONG MajorFunction,
    IN PUCHAR Buffer,
    IN ULONGLONG Offset,
    IN ULONG Length
)
{
    PIRP irp;
    IO_STATUS_BLOCK statusBlock;
    KEVENT waitEvent;
    NTSTATUS status;
    LARGE_INTEGER ByteOffset;
    ByteOffset.QuadPart = Offset;

    irp = IoBuildAsynchronousFsdRequest(
        MajorFunction, 
        DeviceExtension->LowerDevice,
        Buffer,
        Length, 
        (PLARGE_INTEGER)&ByteOffset,
        &statusBlock
    );
    if (!irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // vista对直接磁盘写入进行了保护, 驱动操作需要在IRP的FLAGS加上SL_FORCE_DIRECT_WRITE标志
    if (IRP_MJ_WRITE == MajorFunction)
    {
        IoGetNextIrpStackLocation(irp)->Flags |= SL_FORCE_DIRECT_WRITE;
    }

    KeInitializeEvent(&waitEvent, NotificationEvent, FALSE);

    IoSetCompletionRoutine(
        irp, 
        svReadWriteSectorsCompletion,
        &waitEvent,
        TRUE, 
        TRUE, 
        TRUE
    );

    status = IoCallDriver(DeviceExtension->LowerDevice, irp);

    if (STATUS_PENDING == status) 
    {
        KeWaitForSingleObject(&waitEvent, Executive, KernelMode, FALSE, NULL);
        status = statusBlock.Status;
    }

    return status;
}

NTSTATUS
svGetDiskSectorForRead(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN ULONGLONG Offset,
    OUT PULONGLONG RealOffset
)
{
    PSECTOR_MAP_ITEM findItem;
    SECTOR_MAP_ITEM searchItem;

    if (DPBitmapTestBit(DeviceExtension->BitmapPassthru, Offset))
    {
        *RealOffset = Offset;
        return STATUS_SUCCESS;
    }

    if (DPBitmapTestBit(DeviceExtension->BitmapRedirect, Offset))
    {
        searchItem.OriginOffset = Offset;

        findItem = RtlLookupElementGenericTable(&DeviceExtension->MapRedirect, &searchItem);
        if (findItem)
        {
            *RealOffset = findItem->RedirectOffset;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
svGetDiskSectorForWrite(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN ULONGLONG Offset,
    OUT PULONGLONG RealOffset
)
{
    PSECTOR_MAP_ITEM findItem;
    SECTOR_MAP_ITEM searchItem;
    ULONGLONG redirectOffset;

    if (DPBitmapTestBit(DeviceExtension->BitmapPassthru, Offset))
    {
        *RealOffset = Offset;
        return STATUS_SUCCESS;
    }

    //查找扇区是否已经被重定向
    if (DPBitmapTestBit(DeviceExtension->BitmapRedirect, Offset))
    {
        searchItem.OriginOffset = Offset;

        findItem = RtlLookupElementGenericTable(&DeviceExtension->MapRedirect, &searchItem);
        if (findItem)
        {
            *RealOffset = findItem->RedirectOffset;
            return STATUS_SUCCESS;
        }
    }
    else
    {
        //标记扇区为已重定向
        DPBitmapSet(DeviceExtension->BitmapRedirect, Offset, DeviceExtension->BytesPerSector);

        //找到下一个空闲扇区
        redirectOffset = DPBitmapGetNextOffset(DeviceExtension->BitmapOrigin, DeviceExtension->NextFreeSector, FALSE);
        if (0 == redirectOffset)
        {
            return STATUS_DISK_FULL;
        }

        *RealOffset = redirectOffset;

        //下次查找空闲扇区从上一个位置开始
        DeviceExtension->NextFreeSector = redirectOffset + DeviceExtension->BytesPerSector;

        //保存记录到重定向表
        searchItem.OriginOffset = Offset;
        searchItem.RedirectOffset = redirectOffset;

        RtlInsertElementGenericTable(&DeviceExtension->MapRedirect, &searchItem, sizeof(SECTOR_MAP_ITEM), NULL);
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
svHandleDiskRequest(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN ULONG MajorFunction,
    IN PIRP Irp,
    IN PUCHAR Buffer,
    IN ULONGLONG Offset,
    IN ULONG Length
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    ULONGLONG realOffset;
    ULONGLONG prevOffset = Offset;
    ULONG i;

    UNREFERENCED_PARAMETER(Irp);

    if (Offset + Length >= DeviceExtension->VolumeLetter)
    {
        return STATUS_INVALID_PARAMETER;
    }

    //检测磁盘的实际读写偏移，连续空间的磁盘读写一次完成
    for (i = 0; i < Length; i += DeviceExtension->BytesPerSector)
    {
        if (IRP_MJ_READ == MajorFunction)
        {
            status = svGetDiskSectorForRead(DeviceExtension, Offset + i, &realOffset);
        }
        else
        {
            status = svGetDiskSectorForWrite(DeviceExtension, Offset + i, &realOffset);
        }

        //磁盘满了或异常数据
        if (!NT_SUCCESS(status))
        {
            return status;
        }
        
        if (realOffset != Offset + i)
        {
            status = svRealDiskRequest(
                DeviceExtension,
                MajorFunction,
                Buffer,
                prevOffset,
                (ULONG)(prevOffset - Offset)
            );

            if (!NT_SUCCESS(status))
            {
                return status;
            }

            Buffer += i;
            Offset = prevOffset;
            prevOffset = Offset + i;
        }
    }

    if (prevOffset < Offset + Length)
    {
        status = svRealDiskRequest(
            DeviceExtension,
            MajorFunction,
            Buffer,
            prevOffset,
            (ULONG)(prevOffset - Offset)
        );
    }

    return status;
}