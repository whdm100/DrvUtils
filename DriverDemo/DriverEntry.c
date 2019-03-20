#include "DriverDef.h"

NTSTATUS DriverEntry(PDRIVER_OBJECT drvObj, PUNICODE_STRING regPath);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#endif // ALLOC_PRAGMA

PDEVICE_OBJECT g_ddDevObj;

#if (DEMO_DRIVER_ENABLE) //²âÊÔÇý¶¯
NTSTATUS DriverEntry(PDRIVER_OBJECT drvObj, PUNICODE_STRING regPath)
{
    ULONG i;
    NTSTATUS status;
    UNICODE_STRING devName = RTL_CONSTANT_STRING(L"DemoMsgDriver");
    UNICODE_STRING sybName = RTL_CONSTANT_STRING(DEMO_SYB_NAME);
    UNREFERENCED_PARAMETER(regPath);
    
    DbgPrint("[DriverDemo]Begin DriverEntry.");

    drvObj->DriverUnload = ddDriverUnload;
    for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        drvObj->MajorFunction[i] = ddDriverDispatch;
    }

    do 
    {
        status = IoCreateDevice(drvObj, 0, &devName, FILE_DEVICE_UNKNOWN,
            FILE_DEVICE_SECURE_OPEN, FALSE, &g_ddDevObj);
        if (!NT_SUCCESS(status))
        {
            DbgPrint("[DriverDemo]Create device failed!");
            break;
        }

        IoDeleteSymbolicLink(&sybName);
        status = IoCreateSymbolicLink(&sybName, &devName);
        if (!NT_SUCCESS(status))
        {   
            DbgPrint("[DriverDemo]Create symbol link failed!");
            break;
        }
    } while (FALSE);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("[DriverDemo]Failed DriverEntry!");
    }

    DbgPrint("[DriverDemo]End DriverEntry.");
    return status;
}
#elif (KEYBOARD_DRIVER_ENABLE) //¼üÅÌ¹ýÂËÇý¶¯
NTSTATUS DriverEntry(PDRIVER_OBJECT drvObj, PUNICODE_STRING regPath)
{
    ULONG i;
    NTSTATUS status;
    //UNICODE_STRING devName = RTL_CONSTANT_STRING(KBDFLT_DEV_NAME);
    UNREFERENCED_PARAMETER(regPath);

    DbgPrint("[DriverKbdFlt]Begin DriverEntry.");

    for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        drvObj->MajorFunction[i] = kbdDriverDispatchGeneral;
    }

    drvObj->DriverUnload = kbdDriverUnload;
    drvObj->MajorFunction[IRP_MJ_READ] = kbdDriverDispatchRead;
    drvObj->MajorFunction[IRP_MJ_PNP] = kbdDriverPnp;

    status = kbdDriverInit(drvObj);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("[DriverKbdFlt]Failed DriverEntry!");
    }

    DbgPrint("[DriverKbdFlt]End DriverEntry.");
    return status;
}
#endif 