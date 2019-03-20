#include "DriverDef.h"
#include <ntddkbd.h>

ULONG g_kbdIrpCount;
extern POBJECT_TYPE *IoDriverObjectType;

#define KBD_DRV_NAME L"\\Driver\\kbdclass"
#define PS2KBD_DRV_NAME L"\\Driver\\i8042prt"
#define USBKBD_DRV_NAME L"\\Driver\\Kbdhid"

NTSTATUS ObReferenceObjectByName(
    PUNICODE_STRING ObjectName,
    ULONG Attributes,
    PACCESS_STATE AccessState,
    ACCESS_MASK DesiredAccess,
    POBJECT_TYPE ObjectType,
    KPROCESSOR_MODE AccessMode,
    PVOID ParseContext,
    PVOID *Object
    );

NTSTATUS kbdDriverInit(PDRIVER_OBJECT drvObj)
{
    NTSTATUS status;
    UNICODE_STRING kbdDriverName;
    PDRIVER_OBJECT kbdDriverObject = NULL;
    PDEVICE_OBJECT fltObj = NULL;
    PDEVICE_OBJECT olbObj = NULL;
    PDEVICE_OBJECT topObj = NULL;

    g_kbdIrpCount = 0;

    RtlInitUnicodeString(&kbdDriverName, KBD_DRV_NAME);
    status = ObReferenceObjectByName(
        &kbdDriverName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        FILE_ALL_ACCESS,
        *IoDriverObjectType,
        KernelMode,
        NULL,
        &kbdDriverObject
        );
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[DriverKbdFlt]kbdDriverInit: ObReferenceObjectByName failed.");
        return status;
    }

    olbObj = kbdDriverObject->DeviceObject;
    while (olbObj)
    {
        status = devAttachDevice(drvObj, olbObj, &fltObj, &topObj);
        if (!NT_SUCCESS(status))
        {
            DbgPrint("[DriverKbdFlt]kbdDriverInit: devAttachDevice failed.");
            break;
        }

        olbObj = olbObj->NextDevice;
    }

    ObDereferenceObject(kbdDriverObject);

    return status;
}

void kbdDriverUnload(PDRIVER_OBJECT drvObj)
{
    PDEVICE_OBJECT devObj = NULL;
    PDEVICE_OBJECT nxtObj = NULL;
    LARGE_INTEGER delayTime;
    delayTime = RtlConvertUlongToLargeInteger(100 * 1000);
    devObj = drvObj->DeviceObject;
    while (devObj)
    {
        nxtObj = devObj->NextDevice;
        IoDetachDevice(((PKBDFLT_DEV_EXT)devObj->DeviceExtension)->topDevObj);
        IoDeleteDevice(devObj);
        devObj = nxtObj;
    }

    while (g_kbdIrpCount)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &delayTime);
    }

    DbgPrint("[DriverKbdFlt]kbdDriverUnload: Driver unload end.");
}

NTSTATUS kbdDriverDispatchGeneral(PDEVICE_OBJECT devObj, PIRP irp)
{
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(((PKBDFLT_DEV_EXT)devObj->DeviceExtension)->topDevObj, irp);
}

NTSTATUS kbdDriverDispatchRead(PDEVICE_OBJECT devObj, PIRP irp)
{
    NTSTATUS status;

    //判断是否在设备栈底
    if (irp->CurrentLocation == 1)
    {
        status = STATUS_INVALID_DEVICE_REQUEST;
        irp->IoStatus.Status = status;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return status;
    }

    InterlockedIncrement((volatile LONG*)&g_kbdIrpCount);
    IoCopyCurrentIrpStackLocationToNext(irp);
    IoSetCompletionRoutine(irp, kbdDriverReadComplete, devObj, TRUE, TRUE, TRUE);
    return IoCallDriver(((PKBDFLT_DEV_EXT)devObj->DeviceExtension)->topDevObj, irp);
}

void DisplayKeyboardInputData(PUCHAR pBuffer, ULONG bufLen)
{
    ULONG i;
    ULONG dataCount = 0;
    PKEYBOARD_INPUT_DATA pInputData = NULL;

    if (!pBuffer || !bufLen)
    {
        return ;
    }

    dataCount = bufLen / sizeof(KEYBOARD_INPUT_DATA);
    for (i = 0; i < dataCount; i++)
    {
        pInputData = (PKEYBOARD_INPUT_DATA)(pBuffer + sizeof(KEYBOARD_INPUT_DATA)*i);
        DbgPrint("[DriverKbdFlt]KbdId:%2d, MakeCode:%4d Flag:%s.", 
            pInputData->UnitId, 
            pInputData->MakeCode,
            (pInputData->Flags & KEY_BREAK) ? "KeyUp" : "KeyDown"
        );
    }
}

NTSTATUS kbdDriverReadComplete(PDEVICE_OBJECT devObj, PIRP irp, PVOID context)
{
    PUCHAR pBuffer = NULL;
    ULONG bufLen = 0;
    UNREFERENCED_PARAMETER(devObj);
    UNREFERENCED_PARAMETER(context);

    if (NT_SUCCESS(irp->IoStatus.Status))
    {
        pBuffer = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
        bufLen = PtrToUlong((const void*)irp->IoStatus.Information);
        //打印按键信息
        DisplayKeyboardInputData(pBuffer, bufLen);
    }

    if (irp->PendingReturned)
    {
        IoMarkIrpPending(irp);
    }

    InterlockedDecrement((volatile LONG*)&g_kbdIrpCount);

    return irp->IoStatus.Status;
}

NTSTATUS kbdDriverPnp(PDEVICE_OBJECT devObj, PIRP irp)
{
    NTSTATUS status = STATUS_SUCCESS;
    PKBDFLT_DEV_EXT devExt;
    PIO_STACK_LOCATION irpsp;

    devExt = (PKBDFLT_DEV_EXT)devObj->DeviceExtension;
    irpsp = IoGetCurrentIrpStackLocation(irp);

    switch (irpsp->MajorFunction)
    {
    case IRP_MN_REMOVE_DEVICE:
        IoSkipCurrentIrpStackLocation(irp);
        IoCallDriver(devExt->topDevObj, irp);
        IoDetachDevice(devExt->topDevObj);
        IoDeleteDevice(devObj);
        break;

    default:
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(devExt->topDevObj, irp);
        break;
    }

    return status;
}