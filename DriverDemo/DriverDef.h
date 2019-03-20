#pragma once

#include <wdm.h>

#define DEMO_DRIVER_ENABLE 0
#define KEYBOARD_DRIVER_ENABLE 1

#define CTRL_CODE_ENABLE    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_READ_DATA)
#define CTRL_CODE_DISABLE   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_READ_DATA)
#define CTRL_CODE_SEND_DATA CTL_CODE(FILE_DEVICE_UNKNOWN, 0x911, METHOD_BUFFERED, FILE_WRITE_DATA)
#define CTRL_CODE_RECV_DATA CTL_CODE(FILE_DEVICE_UNKNOWN, 0x912, METHOD_BUFFERED, FILE_READ_DATA)

//测试驱动，数据传输
#define DEMO_SYB_NAME L"\\??\\dev_demo_skr19"

extern PDEVICE_OBJECT g_ddDevObj;

void ddDriverUnload(PDRIVER_OBJECT drvObj);

NTSTATUS ddDriverDispatch(PDEVICE_OBJECT devObj, PIRP irp);

//键盘过滤驱动
#define KBDFLT_DEV_NAME L"\\??\\dev_kbdflt_skr19"

NTSTATUS kbdDriverInit(PDRIVER_OBJECT drvObj);

void kbdDriverUnload(PDRIVER_OBJECT drvObj);

NTSTATUS kbdDriverDispatchGeneral(PDEVICE_OBJECT devObj, PIRP irp);

NTSTATUS kbdDriverDispatchRead(PDEVICE_OBJECT devObj, PIRP irp);

NTSTATUS kbdDriverReadComplete(PDEVICE_OBJECT devObj, PIRP irp, PVOID context);

NTSTATUS kbdDriverPnp(PDEVICE_OBJECT devObj, PIRP irp);

//安装过滤设备
typedef struct KeyBoard_Filter_Device_Extension
{
    ULONG devExtSize;
    PDEVICE_OBJECT fltDevObj;
    PDEVICE_OBJECT oldDevObj;
    PDEVICE_OBJECT topDevObj;
    KSPIN_LOCK iorSpinLock;
    KEVENT ioSyncEvent;
}KBDFLT_DEV_EXT, *PKBDFLT_DEV_EXT;

void initFilterDevExt(
    PKBDFLT_DEV_EXT devExt,
    PDEVICE_OBJECT oldObj,
    PDEVICE_OBJECT fltObj,
    PDEVICE_OBJECT topObj
);

void DeinitFilterDevExt(PKBDFLT_DEV_EXT devExt);

NTSTATUS devAttachDevice(
    PDRIVER_OBJECT drvObj,
    PDEVICE_OBJECT oldObj,
    PDEVICE_OBJECT *fltObj,
    PDEVICE_OBJECT *topObj
);