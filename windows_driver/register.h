#pragma once

//
// Register addresses
//
#define REG_SYS_FUNC_EN 0x0002
#define REG_SYS_CFG1_8822E 0x0080
#define REG_MCUFW_CTRL 0x01E0
#define REG_MACID 0x0500

#define REG_SPS0_CTRL 0x0067
#define REG_AFE_CTRL1 0x0021
#define REG_AFE_CTRL2 0x0022
#define REG_AFE_CTRL3 0x0023
#define REG_AFE_CTRL4 0x0024
#define REG_AFE_CTRL5 0x0025
#define REG_AFE_CTRL6 0x0026
#define REG_LDO_EFUSE_CTRL 0x004E



NTSTATUS ReadRegister8(WDFDEVICE Device, ULONG Address, PUCHAR Data);
NTSTATUS ReadRegister32(WDFDEVICE Device, ULONG Address, PULONG Data);
NTSTATUS WriteRegister8(WDFDEVICE Device, ULONG Address, UCHAR Data);
NTSTATUS WriteRegister32(WDFDEVICE Device, ULONG Address, ULONG Data);
NTSTATUS WriteMemory(WDFDEVICE Device, ULONG Address, PVOID Data, ULONG Length);
