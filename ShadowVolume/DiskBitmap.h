#ifndef _BITMAP_H
#define _BITMAP_H

#include <ntddk.h>

typedef struct _DISK_BITMAP{
    ULONG   DataLength;
    UCHAR   DataBuffer[1];
} DISK_BITMAP, *PDISK_BITMAP;

PDISK_BITMAP 
CreateDiskBitmap(
    IN ULONG BitCount
);

BOOLEAN 
DiskBitmapGetBit(
    IN PDISK_BITMAP* Bitmap,
    IN ULONG Offset
);

VOID
DiskBitmapSetBit(
    IN PDISK_BITMAP* Bitmap,
    IN ULONG Offset,
    IN BOOLEAN Bit
);

#endif // _BITMAP_H