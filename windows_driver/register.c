#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>
#include "trace.h"
#include "register.h"
#include "driver.h"


#define RTW_USB_VENQT_READ 0x05
#define RTW_USB_VENQT_WRITE 0x05

NTSTATUS ReadRegister32(WDFDEVICE Device, ULONG Address, PULONG Data)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;
    WDF_USB_CONTROL_SETUP_PACKET setupPacket;
    WDF_MEMORY_DESCRIPTOR memoryDescriptor;
    ULONG bytesTransferred;

    deviceContext = DeviceGetContext(Device);

    WDF_USB_CONTROL_SETUP_PACKET_INIT_VENDOR(&setupPacket,
                                             BmRequestDeviceToHost,
                                             BMREQUEST_TO_DEVICE,
                                             RTW_USB_VENQT_READ,
                                             (USHORT)Address,
                                             0);

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memoryDescriptor, Data, sizeof(ULONG));

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(deviceContext->UsbDevice,
                                                                WDF_NO_HANDLE,
                                                                NULL,
                                                                &setupPacket,
                                                                &memoryDescriptor,
                                                                &bytesTransferred);

    return status;
}

NTSTATUS WriteRegister32(WDFDEVICE Device, ULONG Address, ULONG Data)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;
    WDF_USB_CONTROL_SETUP_PACKET setupPacket;
    WDF_MEMORY_DESCRIPTOR memoryDescriptor;
    ULONG bytesTransferred;

    deviceContext = DeviceGetContext(Device);

    WDF_USB_CONTROL_SETUP_PACKET_INIT_VENDOR(&setupPacket,
                                             BmRequestHostToDevice,
                                             BMREQUEST_TO_DEVICE,
                                             RTW_USB_VENQT_WRITE,
                                             (USHORT)Address,
                                             0);

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memoryDescriptor, &Data, sizeof(ULONG));

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(deviceContext->UsbDevice,
                                                                WDF_NO_HANDLE,
                                                                NULL,
                                                                &setupPacket,
                                                                &memoryDescriptor,
                                                                &bytesTransferred);

    return status;
}

NTSTATUS WriteRegister8(WDFDEVICE Device, ULONG Address, UCHAR Data)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;
    WDF_USB_CONTROL_SETUP_PACKET setupPacket;
    WDF_MEMORY_DESCRIPTOR memoryDescriptor;
    ULONG bytesTransferred;

    deviceContext = DeviceGetContext(Device);

    WDF_USB_CONTROL_SETUP_PACKET_INIT_VENDOR(&setupPacket,
                                             BmRequestHostToDevice,
                                             BMREQUEST_TO_DEVICE,
                                             RTW_USB_VENQT_WRITE,
                                             (USHORT)Address,
                                             0);

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memoryDescriptor, &Data, sizeof(UCHAR));

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(deviceContext->UsbDevice,
                                                                WDF_NO_HANDLE,
                                                                NULL,
                                                                &setupPacket,
                                                                &memoryDescriptor,
                                                                &bytesTransferred);

    return status;
}
