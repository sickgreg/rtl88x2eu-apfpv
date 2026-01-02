#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>
#include "trace.h"
#include "register.h"
#include "hw_init.h"
#include "driver.h"

NTSTATUS InitializeHardware(WDFDEVICE Device)
{
    NTSTATUS status;
    ULONG chipVersion = 0;

    // Read chip version
    status = ReadRegister32(Device, REG_SYS_CFG1_8822E, &chipVersion);

    if (NT_SUCCESS(status)) {
        // From rtw_hal_read_chip_version
        WriteRegister32(Device, REG_SYS_CFG1_8822E, 0x80700000);
        WriteRegister8(Device, REG_SYS_CFG1_8822E, 0x80);

        // From rtl8822eu_hal_init
        WriteRegister8(Device, REG_SYS_FUNC_EN, 0x0);
    }

    // Power on sequence from rtl8822eu_hal_init
    if (NT_SUCCESS(status)) {
        WriteRegister8(Device, 0x0067, 0x00);
        WriteRegister8(Device, 0x0021, 0x00);
        WriteRegister8(Device, 0x0022, 0x00);
        WriteRegister8(Device, 0x0023, 0x00);
        WriteRegister8(Device, 0x0024, 0x00);
        WriteRegister8(Device, 0x0025, 0x00);
        WriteRegister8(Device, 0x0026, 0x00);
        WriteRegister8(Device, 0x004E, 0x00);
    }

    return status;
}
