#include <windows.h>
#include <winioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <setupapi.h>
#include "vhidmini_ioctl.h"

HANDLE OpenVhidMini()
{
    HDEVINFO deviceInfo;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA detailData;
    DWORD requiredSize = 0;
    HANDLE deviceHandle = INVALID_HANDLE_VALUE;

    deviceInfo = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_VHIDMINI,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (deviceInfo == INVALID_HANDLE_VALUE) {
		printf("Failed to get device info set\n");
        return INVALID_HANDLE_VALUE;
    }

    interfaceData.cbSize = sizeof(interfaceData);

    if (!SetupDiEnumDeviceInterfaces(
        deviceInfo,
        NULL,
        &GUID_DEVINTERFACE_VHIDMINI,
        0,
        &interfaceData))
    {
        printf("Failed to enumerate device\n");
        SetupDiDestroyDeviceInfoList(deviceInfo);
        return INVALID_HANDLE_VALUE;
    }

    SetupDiGetDeviceInterfaceDetail(
        deviceInfo,
        &interfaceData,
        NULL,
        0,
        &requiredSize,
        NULL);

    detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    if (SetupDiGetDeviceInterfaceDetail(
        deviceInfo,
        &interfaceData,
        detailData,
        requiredSize,
        NULL,
        NULL))
    {
        deviceHandle = CreateFile(
            detailData->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);
        if (deviceHandle == INVALID_HANDLE_VALUE)
            printf("Failed to open file: %d\n", GetLastError());
    }
    else {
        printf("Failed to get device detail\n");
    }

    free(detailData);
    SetupDiDestroyDeviceInfoList(deviceInfo);

    return deviceHandle;
}

VOID sendKey(HANDLE hDevice, PVHID_KEY_EVENT key) {
    if (!DeviceIoControl(hDevice, (DWORD)IOCTL_VHIDMINI_KEY_EVENT, key, sizeof(VHID_KEY_EVENT), NULL, 0, NULL, NULL))
        printf("Failed to send: %d\n", GetLastError());
}

int main(char* argc, char* argv[]) {
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);

    VHID_KEY_EVENT keyEvent = {
        .KeyCode = 0x04,
        .Pressed = 1
    };

    HANDLE hDevice = OpenVhidMini();
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Impossible d’ouvrir le device: %d\n", GetLastError());
        return 1;
    }

    sendKey(hDevice, &keyEvent);
    keyEvent.Pressed=0;
    Sleep(50);
    sendKey(hDevice, &keyEvent);

    Sleep(50);
	CloseHandle(hDevice);
	return 0;
}


