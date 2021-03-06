// AppDemo.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <windows.h>

#define DEMO_DEV_NAME L"\\\\.\\demo_cdo_ckw23bn"

#define DEMO_DEV_SEND_DATA CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_WRITE_DATA)
#define DEMO_DEV_RECV_DATA CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_READ_DATA)

int main()
{
    HANDLE hDevice = NULL;
    hDevice = CreateFileW(DEMO_DEV_NAME, GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, NULL);

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        std::cout << L"Open device failed" << std::endl;
        return -1;
    }

    const char sendMsg[] = "Driver demo: Send string data.";
    DWORD dwRetLen = 0;
    DeviceIoControl(hDevice, DEMO_DEV_SEND_DATA, (LPVOID)sendMsg, strlen(sendMsg)+1, NULL, 0, &dwRetLen, NULL);

    CloseHandle(hDevice);
}

