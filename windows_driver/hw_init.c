#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>
#include "trace.h"
#include "register.h"
#include "hw_init.h"
#include "driver.h"
#include "fw.h"

NTSTATUS ReadMacAddress(WDFDEVICE Device)
{
    NTSTATUS status = STATUS_SUCCESS;
    PMINIPORT_ADAPTER_CONTEXT adapterContext = (PMINIPORT_ADAPTER_CONTEXT)WdfObjectGet_MINIPORT_ADAPTER_CONTEXT(Device);
    int i;

    for (i = 0; i < 6; i++)
    {
        status = ReadRegister8(Device, REG_MACID + i, &adapterContext->MacAddress[i]);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    return status;
}


NTSTATUS DownloadFirmware(WDFDEVICE Device)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG firmwareOffset = 0;
    ULONG chunkSize = 4096;
    UCHAR val8;
    int i;

    // Enable firmware download
    ReadRegister8(Device, REG_MCUFW_CTRL, &val8);
    WriteRegister8(Device, REG_MCUFW_CTRL, val8 | 0x20);


    while (firmwareOffset < rtl8822e_fw_len)
    {
        if (rtl8822e_fw_len - firmwareOffset < chunkSize)
        {
            chunkSize = rtl8822e_fw_len - firmwareOffset;
        }

        status = WriteMemory(Device, 0x1000 + firmwareOffset, rtl8822e_fw + firmwareOffset, chunkSize);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        firmwareOffset += chunkSize;
    }

    // Disable firmware download and start firmware
    ReadRegister8(Device, REG_MCUFW_CTRL, &val8);
    WriteRegister8(Device, REG_MCUFW_CTRL, val8 & ~0x20);
    WriteRegister8(Device, REG_MCUFW_CTRL, val8 | 0x80);

    // Wait for firmware to be ready
    for (i = 0; i < 1000; i++) {
        ReadRegister8(Device, REG_MCUFW_CTRL, &val8);
        if (val8 & 0x80) {
            break;
        }
        LARGE_INTEGER delay;
        delay.QuadPart = -10 * 1000 * 10; // 10ms
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }

    if (!(val8 & 0x80)) {
        return STATUS_DEVICE_DATA_ERROR;
    }

    return status;
}


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
        WriteRegister8(Device, REG_SPS0_CTRL, 0x00);
        WriteRegister8(Device, REG_AFE_CTRL1, 0x00);
        WriteRegister8(Device, REG_AFE_CTRL2, 0x00);
        WriteRegister8(Device, REG_AFE_CTRL3, 0x00);
        WriteRegister8(Device, REG_AFE_CTRL4, 0x00);
        WriteRegister8(Device, REG_AFE_CTRL5, 0x00);
        WriteRegister8(Device, REG_AFE_CTRL6, 0x00);
        WriteRegister8(Device, REG_LDO_EFUSE_CTRL, 0x00);
    }

    if (NT_SUCCESS(status)) {
        status = ReadMacAddress(Device);
    }

    if (NT_SUCCESS(status)) {
        status = DownloadFirmware(Device);
    }

    if (NT_SUCCESS(status)) {
        WriteRegister8(Device, REG_SYS_FUNC_EN, 0x1); // REG_SYS_FUNC_EN
    }

    return status;
}
