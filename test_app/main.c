#include <windows.h>
#include <stdio.h>

int main()
{
    HANDLE hDevice;

    hDevice = CreateFile(L"\\\\.\\eucard",
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open device: %d\n", GetLastError());
        return 1;
    }

    printf("Device opened successfully\n");

    CloseHandle(hDevice);

    return 0;
}
