#include "RamDisk.h"

NTSTATUS
rdDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
)
{
    NTSTATUS status;
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
    case IOCTL_VIRTUAL_DISK_CREATE_DISK:
        break;
    default:
        break;
    }
}