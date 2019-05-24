#ifndef _DEVICE_UTILS_H
#define _DEVICE_UTILS_H

#include <ntddk.h>
#include <windef.h>
#include <ntstrsafe.h>


NTSTATUS QuerySymbolicLink(
    IN PUNICODE_STRING SymbolicLinkName,
    OUT PUNICODE_STRING LinkTarget
);

NTSTATUS
MyRtlVolumeDeviceToDosName(
    IN PUNICODE_STRING DeviceName,
    OUT PUNICODE_STRING DosName
);

BOOL
GetNTLinkName(
    WCHAR *wszNTName,
    WCHAR *wszFileName
);

BOOL
QueryVolumeName(
    WCHAR ch,
    WCHAR * name,
    USHORT size
);

BOOL
GetNtDeviceName(
    WCHAR * filename,
    WCHAR * ntname
);

#endif // _DEVICE_UTILS_H