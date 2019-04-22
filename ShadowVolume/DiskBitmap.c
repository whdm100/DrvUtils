#include "DiskBitmap.h"

PDISK_BITMAP
CreateDiskBitmap(
    IN ULONG BitCount
)
{
    ULONG memSize = BitCount / 8;
    PDISK_BITMAP diskBitmap = ExAllocatePool(NonPagedPool, memSize + sizeof(ULONG));

    if (diskBitmap)
    {
        RtlZeroMemory(diskBitmap, memSize + sizeof(ULONG));
        diskBitmap->DataLength = memSize;
    }

    return diskBitmap;
}

BOOLEAN
DiskBitmapGetBit(
    IN PDISK_BITMAP* Bitmap,
    IN ULONG Offset
)
{

}

VOID
DiskBitmapSetBit(
    IN PDISK_BITMAP* Bitmap,
    IN ULONG Offset,
    IN BOOLEAN Bit
)
{

}