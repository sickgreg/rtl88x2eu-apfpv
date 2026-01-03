#pragma once

NTSTATUS InitializeHardware(WDFDEVICE Device);
NTSTATUS DownloadFirmware(WDFDEVICE Device);
NTSTATUS ReadMacAddress(WDFDEVICE Device);
