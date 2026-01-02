#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>
#include "trace.h"
#include "register.h"
#include "hw_init.h"

//
// This driver is a foundational skeleton for a Realtek EU card driver.
// It demonstrates USB device detection, basic hardware initialization, and I/O.
// To function as a network driver, it requires NDIS integration.
//

//
// Device context structure
//
typedef struct _DEVICE_CONTEXT {
    WDFUSBDEVICE UsbDevice;
    WDFUSBINTERFACE UsbInterface;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

NTSTATUS EvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit);
NTSTATUS EvtDevicePrepareHardware(WDFDEVICE Device, WDFCMRESLIST ResourcesRaw, WDFCMRESLIST ResourcesTranslated);
NTSTATUS SelectInterfaces(WDFDEVICE Device);

VOID EvtIoRead(WDFQUEUE Queue, WDFREQUEST Request, size_t Length);
VOID EvtIoWrite(WDFQUEUE Queue, WDFREQUEST Request, size_t Length);
