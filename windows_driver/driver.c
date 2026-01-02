#include "driver.h"

//
// This driver is a foundational skeleton for a Realtek EU card driver.
// It demonstrates USB device detection, basic hardware initialization, and I/O.
// To function as a network driver, it requires NDIS integration.
//

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);

    return status;
}

NTSTATUS EvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit) {
    NTSTATUS status;
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    PDEVICE_CONTEXT deviceContext;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;
    UNICODE_STRING dosDeviceName;

    UNREFERENCED_PARAMETER(Driver);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    deviceContext = DeviceGetContext(device);

    WDF_USB_DEVICE_CREATE_CONFIG createParams;
    WDF_USB_DEVICE_CREATE_CONFIG_INIT(&createParams, USBD_CLIENT_CONTRACT_VERSION_602);
    status = WdfUsbTargetDeviceCreateWithParameters(device, &createParams, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->UsbDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    USB_DEVICE_DESCRIPTOR deviceDescriptor;
    WdfUsbTargetDeviceGetDeviceDescriptor(deviceContext->UsbDevice, &deviceDescriptor);

    if (deviceDescriptor.idVendor == 0x0BDA &&
        (deviceDescriptor.idProduct == 0x8812 || // RTL8812A
         deviceDescriptor.idProduct == 0xE822 || // RTL8822E
         deviceDescriptor.idProduct == 0xA82A || // RTL8822E
         deviceDescriptor.idProduct == 0xA81A))  // 8812EU
    {
        // This is our device
    } else {
        // Not our device, fail to load
        return STATUS_UNSUCCESSFUL;
    }

    RtlInitUnicodeString(&dosDeviceName, L"\\DosDevices\\eucard");
    status = WdfDeviceCreateSymbolicLink(device, &dosDeviceName);

    if (!NT_SUCCESS(status)) {
        return status;
    }


    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoRead = EvtIoRead;
    queueConfig.EvtIoWrite = EvtIoWrite;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);

    return status;
}

NTSTATUS EvtDevicePrepareHardware(WDFDEVICE Device, WDFCMRESLIST ResourcesRaw, WDFCMRESLIST ResourcesTranslated)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    status = SelectInterfaces(Device);

    if (NT_SUCCESS(status)) {
        status = InitializeHardware(Device);
    }

    return status;
}

NTSTATUS SelectInterfaces(WDFDEVICE Device)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;

    deviceContext = DeviceGetContext(Device);

    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(&configParams);

    status = WdfUsbTargetDeviceSelectConfig(deviceContext->UsbDevice, WDF_NO_OBJECT_ATTRIBUTES, &configParams);

    if (NT_SUCCESS(status)) {
        deviceContext->UsbInterface = configParams.Types.singleInterface.ConfiguredUsbInterface;
    }

    return status;
}

VOID EvtIoRead(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    NTSTATUS status;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT deviceContext = DeviceGetContext(device);
    WDFMEMORY memory;

    status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    status = WdfUsbTargetPipeReadSynchronously(WdfUsbInterfaceGetConfiguredPipe(deviceContext->UsbInterface, 0, NULL),
                                             Request,
                                             NULL,
                                             NULL,
                                             memory,
                                             NULL);

    WdfRequestComplete(Request, status);
}

VOID EvtIoWrite(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    NTSTATUS status;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT deviceContext = DeviceGetContext(device);
    WDFMEMORY memory;

    status = WdfRequestRetrieveInputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    status = WdfUsbTargetPipeWriteSynchronously(WdfUsbInterfaceGetConfiguredPipe(deviceContext->UsbInterface, 1, NULL),
                                              Request,
                                              NULL,
                                              NULL,
                                              memory,
                                              NULL);

    WdfRequestComplete(Request, status);
}
