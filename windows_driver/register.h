#pragma once

//
// Register addresses
//
#define REG_SYS_FUNC_EN 0x0002
#define REG_SYS_CFG1_8822E 0x0080


NTSTATUS ReadRegister32(WDFDEVICE Device, ULONG Address, PULONG Data);
NTSTATUS WriteRegister32(WDFDEVICE Device, ULONG Address, ULONG Data);
NTSTATUS WriteRegister8(WDFDEVICE Device, ULONG Address, UCHAR Data);
