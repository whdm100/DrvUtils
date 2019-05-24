
#include "DeviceUitls.h"

// ‰»Î\\??\\c:-->\\device\\harddiskvolume1
NTSTATUS QuerySymbolicLink(
    IN PUNICODE_STRING SymbolicLinkName,
    OUT PUNICODE_STRING LinkTarget
)
{
    OBJECT_ATTRIBUTES   oa = { 0 };
    NTSTATUS            status = 0;
    HANDLE              handle = NULL;

    InitializeObjectAttributes(
        &oa,
        SymbolicLinkName,
        OBJ_CASE_INSENSITIVE,
        0,
        0);

    status = ZwOpenSymbolicLinkObject(&handle, GENERIC_READ, &oa);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    LinkTarget->MaximumLength = MAX_PATH * sizeof(WCHAR);
    LinkTarget->Length = 0;
    LinkTarget->Buffer = ExAllocatePool(PagedPool, LinkTarget->MaximumLength);
    if (!LinkTarget->Buffer)
    {
        ZwClose(handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(LinkTarget->Buffer, LinkTarget->MaximumLength);

    status = ZwQuerySymbolicLinkObject(handle, LinkTarget, NULL);
    ZwClose(handle);

    if (!NT_SUCCESS(status))
    {
        ExFreePool(LinkTarget->Buffer);
    }

    return status;
}

// ‰»Î\\Device\\harddiskvolume1
// ‰≥ˆC:
NTSTATUS
MyRtlVolumeDeviceToDosName(
    IN PUNICODE_STRING DeviceName,
    OUT PUNICODE_STRING DosName
)
{
    NTSTATUS                status = 0;
    UNICODE_STRING          driveLetterName = { 0 };
    WCHAR                   driveLetterNameBuf[128] = { 0 };
    WCHAR                   c = L'\0';
    WCHAR                   DriLetter[3] = { 0 };
    UNICODE_STRING          linkTarget = { 0 };

    for (c = L'A'; c <= L'Z'; c++)
    {
        RtlInitEmptyUnicodeString(&driveLetterName, driveLetterNameBuf, sizeof(driveLetterNameBuf));
        RtlAppendUnicodeToString(&driveLetterName, L"\\??\\");
        DriLetter[0] = c;
        DriLetter[1] = L':';
        DriLetter[2] = 0;
        RtlAppendUnicodeToString(&driveLetterName, DriLetter);

        status = QuerySymbolicLink(&driveLetterName, &linkTarget);
        if (!NT_SUCCESS(status))
        {
            continue;
        }

        if (RtlEqualUnicodeString(&linkTarget, DeviceName, TRUE))
        {
            ExFreePool(linkTarget.Buffer);
            break;
        }

        ExFreePool(linkTarget.Buffer);
    }

    if (c <= L'Z')
    {
        DosName->Buffer = ExAllocatePool(PagedPool, 3 * sizeof(WCHAR));
        if (!DosName->Buffer)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        DosName->MaximumLength = 6;
        DosName->Length = 4;
        *DosName->Buffer = c;
        *(DosName->Buffer + 1) = ':';
        *(DosName->Buffer + 2) = 0;

        return STATUS_SUCCESS;
    }

    return status;
}

//c:\\windows\\hi.txt<--\\device\\harddiskvolume1\\windows\\hi.txt
BOOL
GetNTLinkName(
    WCHAR *wszNTName, 
    WCHAR *wszFileName
)
{
    UNICODE_STRING      ustrFileName = { 0 };
    UNICODE_STRING      ustrDosName = { 0 };
    UNICODE_STRING      ustrDeviceName = { 0 };

    WCHAR               *pPath = NULL;
    ULONG               i = 0;
    ULONG               ulSepNum = 0;


    if (wszFileName == NULL ||
        wszNTName == NULL ||
        _wcsnicmp(wszNTName, L"\\device\\harddiskvolume", wcslen(L"\\device\\harddiskvolume")) != 0)
    {
        return FALSE;
    }

    ustrFileName.Buffer = wszFileName;
    ustrFileName.Length = 0;
    ustrFileName.MaximumLength = sizeof(WCHAR)*MAX_PATH;

    while (wszNTName[i] != L'\0')
    {

        if (wszNTName[i] == L'\0')
        {
            break;
        }
        if (wszNTName[i] == L'\\')
        {
            ulSepNum++;
        }
        if (ulSepNum == 3)
        {
            wszNTName[i] = UNICODE_NULL;
            pPath = &wszNTName[i + 1];
            break;
        }
        i++;
    }

    if (pPath == NULL)
    {
        return FALSE;
    }

    RtlInitUnicodeString(&ustrDeviceName, wszNTName);

    if (!NT_SUCCESS(MyRtlVolumeDeviceToDosName(&ustrDeviceName, &ustrDosName)))
    {
        return FALSE;
    }

    RtlCopyUnicodeString(&ustrFileName, &ustrDosName);
    RtlAppendUnicodeToString(&ustrFileName, L"\\");
    RtlAppendUnicodeToString(&ustrFileName, pPath);

    ExFreePool(ustrDosName.Buffer);

    return TRUE;
}

BOOL 
QueryVolumeName(
    WCHAR ch, 
    WCHAR * name,
    USHORT size
)
{
    WCHAR szVolume[7] = L"\\??\\C:";
    UNICODE_STRING LinkName;
    UNICODE_STRING VolName;
    UNICODE_STRING ustrTarget;
    NTSTATUS ntStatus = 0;

    RtlInitUnicodeString(&LinkName, szVolume);

    szVolume[4] = ch;

    ustrTarget.Buffer = name;
    ustrTarget.Length = 0;
    ustrTarget.MaximumLength = size;

    ntStatus = QuerySymbolicLink(&LinkName, &VolName);
    if (NT_SUCCESS(ntStatus))
    {
        RtlCopyUnicodeString(&ustrTarget, &VolName);
        ExFreePool(VolName.Buffer);
    }
    return NT_SUCCESS(ntStatus);

}

//\\??\\c:\\windows\\hi.txt-->\\device\\harddiskvolume1\\windows\\hi.txt

BOOL
GetNtDeviceName(
    WCHAR * filename, 
    WCHAR * ntname
)
{
    UNICODE_STRING uVolName = { 0,0,0 };
    WCHAR volName[MAX_PATH] = L"";
    WCHAR tmpName[MAX_PATH] = L"";
    WCHAR chVol = L'\0';
    WCHAR * pPath = NULL;
    int i = 0;


    RtlStringCbCopyW(tmpName, MAX_PATH * sizeof(WCHAR), filename);

    for (i = 1; i < MAX_PATH - 1; i++)
    {
        if (tmpName[i] == L':')
        {
            pPath = &tmpName[(i + 1) % MAX_PATH];
            chVol = tmpName[i - 1];
            break;
        }
    }

    if (pPath == NULL)
    {
        return FALSE;
    }

    if (chVol == L'?')
    {
        uVolName.Length = 0;
        uVolName.MaximumLength = MAX_PATH * sizeof(WCHAR);
        uVolName.Buffer = ntname;
        RtlAppendUnicodeToString(&uVolName, L"\\Device\\HarddiskVolume?");
        RtlAppendUnicodeToString(&uVolName, pPath);
        return TRUE;
    }
    else if (QueryVolumeName(chVol, volName, MAX_PATH * sizeof(WCHAR)))
    {
        uVolName.Length = 0;
        uVolName.MaximumLength = MAX_PATH * sizeof(WCHAR);
        uVolName.Buffer = ntname;
        RtlAppendUnicodeToString(&uVolName, volName);
        RtlAppendUnicodeToString(&uVolName, pPath);
        return TRUE;
    }

    return FALSE;
}