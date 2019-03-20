#include "DriverDef.h"

void ddDriverUnload(PDRIVER_OBJECT drvObj)
{
    UNREFERENCED_PARAMETER(drvObj);
    UNICODE_STRING sybName = RTL_CONSTANT_STRING(DEMO_SYB_NAME);
    IoDeleteSymbolicLink(&sybName);
    IoDeleteDevice(g_ddDevObj);
}

NTSTATUS ddDriverDispatch(PDEVICE_OBJECT devObj, PIRP irp)
{
    ULONG retLen = 0;
    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(irp);
    PUCHAR pBuffer = NULL;

    if (devObj == g_ddDevObj)
    {
        switch (irpsp->MajorFunction)
        {
        case IRP_MJ_CREATE:
        case IRP_MJ_CLOSE:
            //简单的返回成功
            break;

        case IRP_MJ_DEVICE_CONTROL:
        {
            if (irp->MdlAddress != NULL)
                pBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
            else
                pBuffer = (PUCHAR)irp->UserBuffer;
            if (!pBuffer)
                pBuffer = (PUCHAR)irp->AssociatedIrp.SystemBuffer;

            switch (irpsp->Parameters.DeviceIoControl.IoControlCode)
            {
            case CTRL_CODE_SEND_DATA:
                DbgPrint((char*)pBuffer);
                break;
            case CTRL_CODE_RECV_DATA:
            default:
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            break;
        }
        default:
            break;
        }
    }

    irp->IoStatus.Information = retLen;
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);

    return status;
}

void initFilterDevExt(
    PKBDFLT_DEV_EXT devExt,
    PDEVICE_OBJECT oldObj,
    PDEVICE_OBJECT fltObj,
    PDEVICE_OBJECT topObj
    )
{
    devExt->devExtSize = sizeof(KBDFLT_DEV_EXT);
    devExt->oldDevObj = oldObj;
    devExt->fltDevObj = fltObj;
    devExt->topDevObj = topObj;
    KeInitializeSpinLock(&devExt->iorSpinLock);
    KeInitializeEvent(&devExt->ioSyncEvent, SynchronizationEvent, FALSE);
}

void DeinitFilterDevExt(PKBDFLT_DEV_EXT devExt)
{
    devExt->oldDevObj = NULL;
    devExt->fltDevObj = NULL;
    devExt->topDevObj = NULL;
    KeClearEvent(&devExt->ioSyncEvent);
}

NTSTATUS devAttachDevice(
    PDRIVER_OBJECT drvObj,
    PDEVICE_OBJECT oldObj,
    PDEVICE_OBJECT *fltObj,
    PDEVICE_OBJECT *topObj
)
{
    NTSTATUS status;

    status = IoCreateDevice(drvObj, sizeof(KBDFLT_DEV_EXT), NULL, oldObj->DeviceType, 0, FALSE, fltObj);
    if (!NT_SUCCESS(status))
    {
        return status;
    }
 
    if (oldObj->Flags & DO_BUFFERED_IO)
        (*fltObj)->Flags |= DO_BUFFERED_IO;
    if (oldObj->Flags & DO_DIRECT_IO)
        (*fltObj)->Flags |= DO_DIRECT_IO;
    
    if (oldObj->Characteristics & FILE_DEVICE_SECURE_OPEN)
        (*fltObj)->Characteristics |= FILE_DEVICE_SECURE_OPEN;

    (*fltObj)->Flags |= DO_POWER_PAGABLE;

    (*topObj) = IoAttachDeviceToDeviceStack((*fltObj), oldObj);
    if (!(*topObj))
    {
        IoDeleteDevice((*fltObj));
        (*fltObj) = NULL;
        status = STATUS_UNSUCCESSFUL;
        return status;
    }

    initFilterDevExt((PKBDFLT_DEV_EXT)((*fltObj)->DeviceExtension), oldObj, (*fltObj), (*topObj));

    (*fltObj)->Flags &= ~DO_DEVICE_INITIALIZING;
    return status;
}