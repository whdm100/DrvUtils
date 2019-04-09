#include "RamDisk.h"

NTSTATUS
rdCreateClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 1;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}