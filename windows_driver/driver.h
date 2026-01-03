#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <Ndis.h>
#include <usb.h>
#include <wdfusb.h>
#include "trace.h"
#include "register.h"
#include "hw_init.h"
#include "rtw_xmit.h"
#include "rtw_recv.h"

//
// This driver is a foundational skeleton for a Realtek EU card driver.
// It demonstrates USB device detection, basic hardware initialization, and I/O.
// To function as a network driver, it requires NDIS integration.
//

//
// Device context structure for WDF USB operations
//
typedef struct _DEVICE_CONTEXT {
    WDFUSBDEVICE UsbDevice;
    WDFUSBINTERFACE UsbInterface;
    NDIS_HANDLE NdisAdapterHandle;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

//
// Transmit context
//
typedef struct _TX_CONTEXT {
    WDFUSBPIPE BulkOutPipe;
} TX_CONTEXT, *PTX_CONTEXT;

//
// Receive context
//
typedef struct _RX_CONTEXT {
    WDFUSBPIPE BulkInPipe;
} RX_CONTEXT, *PRX_CONTEXT;

//
// Main context structure for the NDIS Miniport Adapter
//
typedef struct _MINIPORT_ADAPTER_CONTEXT {
    WDFDEVICE WdfDevice;
    UCHAR MacAddress[6];
    TX_CONTEXT TxContext;
    RX_CONTEXT RxContext;
    ULONG Status;
} MINIPORT_ADAPTER_CONTEXT, *PMINIPORT_ADAPTER_CONTEXT;


DRIVER_INITIALIZE DriverEntry;

MINIPORT_INITIALIZE MiniportInitializeEx;
MINIPORT_HALT MiniportHaltEx;
MINIPORT_UNLOAD MiniportUnload;
MINIPORT_OID_REQUEST MiniportOidRequest;
MINIPORT_SEND_NET_BUFFER_LISTS MiniportSendNetBufferLists;
MINIPORT_RETURN_NET_BUFFER_LISTS MiniportReturnNetBufferLists;
MINIPORT_RESET MiniportReset;


EVT_WDF_DEVICE_PREPARE_HARDWARE EvtDevicePrepareHardware;
NTSTATUS SelectInterfaces(WDFDEVICE Device);

NTSTATUS StartReceivePath(WDFDEVICE Device);
EVT_WDF_USB_READER_COMPLETION_ROUTINE EvtUsbContinuousReaderCompletion;
