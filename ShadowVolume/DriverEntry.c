#include "ShadowVolume.h"


#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, rdAddDevice)
#pragma alloc_text(PAGE, svReinitializationRoutine)
#pragma alloc_text(PAGE, rdUnload)
#endif //ALLOC_PRAGMA

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

    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = svDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_READ] = svDispatchRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = svDispatchWrite;
    DriverObject->MajorFunction[IRP_MJ_PNP] = svDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = svDispatchPower;

    DriverObject->DriverUnload = svUnload;
    DriverObject->DriverExtension->AddDevice = svAddDevice;

    IoRegisterBootDriverReinitialization(
        DriverObject,
        svReinitializationRoutine,
        NULL
    );

    return STATUS_SUCCESS;
}

VOID
svReinitializationRoutine(
    IN PDRIVER_OBJECT DriverObject,
    IN OUT PVOID Context,
    IN ULONG Count
)
{

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
        deviceExtension->LowerDevice = lowerDevice;

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
        ZwClose(threadHandle);

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