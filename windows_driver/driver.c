#include "driver.h"

//
// This driver is a foundational skeleton for a Realtek EU card driver.
// It demonstrates USB device detection, basic hardware initialization, and I/O.
// To function as a network driver, it requires NDIS integration.
//

NDIS_OID SupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAC_OPTIONS,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_11_BSSID_LIST_SCAN
};

WDFDRIVER g_WdfDriver;
NDIS_HANDLE g_NdisDriverHandle;


NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NDIS_STATUS status;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS characteristics;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);
    config.DriverInitFlags |= WdfDriverInitNonPnpDriver;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes, &config, &g_WdfDriver);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    NdisZeroMemory(&characteristics, sizeof(characteristics));
    characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    characteristics.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);
    characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;

    characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;

    characteristics.InitializeHandlerEx = MiniportInitializeEx;
    characteristics.HaltHandlerEx = MiniportHaltEx;
    characteristics.UnloadHandler = MiniportUnload;
    characteristics.OidRequestHandler = MiniportOidRequest;
    characteristics.SendNetBufferListsHandler = MiniportSendNetBufferLists;
    characteristics.ReturnNetBufferListsHandler = MiniportReturnNetBufferLists;
    characteristics.ResetHandlerEx = MiniportReset;

    status = NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &characteristics, &g_NdisDriverHandle);

    return status;
}

NTSTATUS MiniportInitializeEx(NDIS_HANDLE NdisAdapterHandle, NDIS_HANDLE MiniportDriverContext, PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    NTSTATUS status;
    PMINIPORT_ADAPTER_CONTEXT adapterContext;
    PWDFDEVICE_INIT deviceInit = NULL;
    PDEVICE_CONTEXT deviceContext;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    adapterContext = (PMINIPORT_ADAPTER_CONTEXT)NdisAllocateMemoryWithTagPriority(NdisAdapterHandle, sizeof(MINIPORT_ADAPTER_CONTEXT), 'eucd', NormalPoolPriority);
    if (!adapterContext) {
        return NDIS_STATUS_RESOURCES;
    }
    NdisZeroMemory(adapterContext, sizeof(MINIPORT_ADAPTER_CONTEXT));

    NdisMSetMiniportAttributes(NdisAdapterHandle,
        &(NDIS_MINIPORT_ADAPTER_ATTRIBUTES){
            .Header = { .Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_ATTRIBUTES,
                        .Revision = NDIS_MINIPORT_ADAPTER_ATTRIBUTES_REVISION_1,
                        .Size = sizeof(NDIS_MINIPORT_ADAPTER_ATTRIBUTES) },
            .GeneralAttributes = {
                .MediaType = NdisMedium802_3,
                .PhysicalMediumType = NdisPhysicalMediumNative802_11,
                .MtuSize = 1500,
                .MaxXmitLinkSpeed = 866000000,
                .MaxRcvLinkSpeed = 866000000,
                .MediaType = NdisMediumNative802_11,
                .InterfaceType = NDIS_INTERFACE_TYPE_USB
            }
        });

    deviceInit = WdfControlDeviceInitAllocate(g_WdfDriver, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
    if (!deviceInit) {
        NdisFreeMemory(adapterContext, 0, 0);
        return NDIS_STATUS_RESOURCES;
    }

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.ParentObject = WdfGetDriver();

    status = WdfDeviceCreate(&deviceInit, &attributes, &adapterContext->WdfDevice);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(deviceInit);
        NdisFreeMemory(adapterContext, 0, 0);
        return status;
    }

    deviceContext = DeviceGetContext(adapterContext->WdfDevice);
    deviceContext->NdisAdapterHandle = NdisAdapterHandle;
    adapterContext->DeviceContext = deviceContext;

    return NDIS_STATUS_SUCCESS;
}


VOID MiniportUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    NdisMDeregisterMiniportDriver(g_NdisDriverHandle);
}


VOID MiniportHaltEx(NDIS_HANDLE MiniportAdapterContext, NDIS_HALT_ACTION HaltAction)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(HaltAction);
}

NDIS_STATUS MiniportReset(NDIS_HANDLE MiniportAdapterContext, PBOOLEAN AddressingReset)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    *AddressingReset = FALSE;
    return NDIS_STATUS_SUCCESS;
}



NTSTATUS EvtDevicePrepareHardware(WDFDEVICE Device, WDFCMRESLIST ResourcesRaw, WDFCMRESLIST ResourcesTranslated)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext = DeviceGetContext(Device);
    PMINIPORT_ADAPTER_CONTEXT adapterContext = NdisMGetMiniportAdapterContext(deviceContext->NdisAdapterHandle);


    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    // The WDF device is now available, perform USB-specific initialization
    WDF_USB_DEVICE_CREATE_CONFIG createParams;
    WDF_USB_DEVICE_CREATE_CONFIG_INIT(&createParams, USBD_CLIENT_CONTRACT_VERSION_602);
    status = WdfUsbTargetDeviceCreateWithParameters(deviceContext->WdfDevice, &createParams, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->UsbDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = SelectInterfaces(Device);

    if (NT_SUCCESS(status)) {
        status = InitializeHardware(Device);
    }

    if (NT_SUCCESS(status)) {
        status = StartReceivePath(Device);
    }

    if (NT_SUCCESS(status)) {
        adapterContext->Status = NdisMediaStateConnected;
    }


    return status;
}

NTSTATUS SelectInterfaces(WDFDEVICE Device)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;
    PMINIPORT_ADAPTER_CONTEXT adapterContext;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;
    UCHAR i;
    UCHAR numPipes;

    deviceContext = DeviceGetContext(Device);
    adapterContext = NdisMGetMiniportAdapterContext(deviceContext->NdisAdapterHandle);

    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(&configParams);

    status = WdfUsbTargetDeviceSelectConfig(deviceContext->UsbDevice, WDF_NO_OBJECT_ATTRIBUTES, &configParams);

    if (NT_SUCCESS(status)) {
        deviceContext->UsbInterface = configParams.Types.singleInterface.ConfiguredUsbInterface;

        numPipes = WdfUsbInterfaceGetNumConfiguredPipes(deviceContext->UsbInterface);

        for (i = 0; i < numPipes; i++)
        {
            WDF_USB_PIPE_INFORMATION pipeInfo;
            WDFUSBPIPE pipe = WdfUsbInterfaceGetConfiguredPipe(deviceContext->UsbInterface, i, &pipeInfo);

            if (WdfUsbPipeTypeBulk == pipeInfo.PipeType)
            {
                if (WdfUsbTargetPipeIsOutEndpoint(pipe))
                {
                    adapterContext->TxContext.BulkOutPipe = pipe;
                }
                else if (WdfUsbTargetPipeIsInEndpoint(pipe))
                {
                    adapterContext->RxContext.BulkInPipe = pipe;
                }
            }
        }
    }

    return status;
}


NDIS_STATUS MiniportOidRequest(NDIS_HANDLE MiniportAdapterContext, PNDIS_OID_REQUEST OidRequest)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    PMINIPORT_ADAPTER_CONTEXT adapterContext = (PMINIPORT_ADAPTER_CONTEXT)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
    case NdisRequestQueryInformation:
    case NdisRequestQueryStatistics:
        switch (OidRequest->DATA.QUERY_INFORMATION.Oid)
        {
        case OID_GEN_SUPPORTED_LIST:
            OidRequest->DATA.QUERY_INFORMATION.BytesWritten = sizeof(SupportedOids);
            OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(SupportedOids);
            RtlCopyMemory(OidRequest->DATA.QUERY_INFORMATION.InformationBuffer, SupportedOids, sizeof(SupportedOids));
            break;

        case OID_GEN_HARDWARE_STATUS:
            *(PULONG)OidRequest->DATA.QUERY_INFORMATION.InformationBuffer = NdisHardwareStatusReady;
            OidRequest->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            *(PULONG)OidRequest->DATA.QUERY_INFORMATION.InformationBuffer = adapterContext->Status;
            OidRequest->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
            break;

        case OID_802_3_PERMANENT_ADDRESS:
        case OID_802_3_CURRENT_ADDRESS:
            OidRequest->DATA.QUERY_INFORMATION.BytesWritten = sizeof(adapterContext->MacAddress);
            OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(adapterContext->MacAddress);
            RtlCopyMemory(OidRequest->DATA.QUERY_INFORMATION.InformationBuffer, adapterContext->MacAddress, sizeof(adapterContext->MacAddress));
            break;

        default:
            status = NDIS_STATUS_NOT_SUPPORTED;
            break;
        }
        break;

    case NdisRequestSetInformation:
        switch (OidRequest->DATA.SET_INFORMATION.Oid)
        {
        case OID_802_11_BSSID_LIST_SCAN:
            // For now, just complete the request successfully
            OidRequest->DATA.SET_INFORMATION.BytesRead = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;
            break;

        default:
            status = NDIS_STATUS_NOT_SUPPORTED;
            break;
        }
        break;

    default:
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    return status;
}


VOID MiniportSendNetBufferLists(NDIS_HANDLE MiniportAdapterContext, PNET_BUFFER_LIST NetBufferLists, ULONG PortNumber, ULONG SendFlags)
{
    UNREFERENCED_PARAMETER(PortNumber);

    PMINIPORT_ADAPTER_CONTEXT adapterContext = (PMINIPORT_ADAPTER_CONTEXT)MiniportAdapterContext;
    PNET_BUFFER_LIST currentNbl = NetBufferLists;
    ULONG numNbls = 0;


    while (currentNbl)
    {
        numNbls++;
        PNET_BUFFER currentNb = NET_BUFFER_LIST_FIRST_NB(currentNbl);
        while (currentNb)
        {
            PVOID data;
            ULONG dataLen;
            WDF_MEMORY_DESCRIPTOR memoryDescriptor;
            NTSTATUS status;

            data = NdisGetDataBuffer(currentNb, &dataLen, NULL, 1, 0);

            // Allocate a buffer that includes space for the Tx descriptor
            PVOID txBuffer;
            WDFMEMORY txMemory;
            status = WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES, PagedPool, 0, sizeof(TX_DESC) + dataLen, &txMemory, &txBuffer);
            if (!NT_SUCCESS(status)) {
                NET_BUFFER_LIST_STATUS(currentNbl) = NDIS_STATUS_RESOURCES;
                currentNb = NET_BUFFER_NEXT_NB(currentNb);
                continue;
            }

            RtwFillTxDesc((PTX_DESC)txBuffer, dataLen);
            RtlCopyMemory((PCHAR)txBuffer + sizeof(TX_DESC), data, dataLen);

            WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&memoryDescriptor, txMemory, NULL);

            status = WdfUsbTargetPipeWriteSynchronously(adapterContext->TxContext.BulkOutPipe,
                                                      NULL,
                                                      NULL,
                                                      &memoryDescriptor,
                                                      NULL);

            if (!NT_SUCCESS(status)) {
                NET_BUFFER_LIST_STATUS(currentNbl) = NDIS_STATUS_FAILURE;
            } else {
                NET_BUFFER_LIST_STATUS(currentNbl) = NDIS_STATUS_SUCCESS;
            }


            WdfObjectDelete(txMemory);

            currentNb = NET_BUFFER_NEXT_NB(currentNb);
        }
        currentNbl = NET_BUFFER_LIST_NEXT_NBL(currentNbl);
    }
    if (numNbls > 0)
    {
        NdisMSendNetBufferListsComplete(adapterContext->NdisAdapterHandle, NetBufferLists, 0);
    }
}


VOID MiniportReturnNetBufferLists(NDIS_HANDLE MiniportAdapterContext, PNET_BUFFER_LIST NetBufferLists, ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(ReturnFlags);

    PNET_BUFFER_LIST currentNbl = NetBufferLists;
    while (currentNbl)
    {
        NdisFreeNetBufferList(currentNbl);
        currentNbl = NET_BUFFER_LIST_NEXT_NBL(currentNbl);
    }
}


NTSTATUS StartReceivePath(WDFDEVICE Device)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext = DeviceGetContext(Device);
    PMINIPORT_ADAPTER_CONTEXT adapterContext = NdisMGetMiniportAdapterContext(deviceContext->NdisAdapterHandle);
    WDF_USB_CONTINUOUS_READER_CONFIG readerConfig;

    WDF_USB_CONTINUOUS_READER_CONFIG_INIT(&readerConfig,
                                          EvtUsbContinuousReaderCompletion,
                                          adapterContext,
                                          4096);

    status = WdfUsbTargetPipeConfigContinuousReader(adapterContext->RxContext.BulkInPipe, &readerConfig);

    return status;
}

VOID EvtUsbContinuousReaderCompletion(WDFUSBPIPE Pipe, WDFMEMORY Buffer, size_t NumBytesTransferred, WDFCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Pipe);

    PMINIPORT_ADAPTER_CONTEXT adapterContext = (PMINIPORT_ADAPTER_CONTEXT)Context;
    PNET_BUFFER_LIST nbl;
    PNET_BUFFER nb;
    PVOID data;
    PRX_DESC rxDesc;
    ULONG pktLen;


    if (NumBytesTransferred >= sizeof(RX_DESC))
    {
        rxDesc = (PRX_DESC)WdfMemoryGetBuffer(Buffer, NULL);
        pktLen = (rxDesc->Dword0 & RX_DESC_PKT_LEN_MASK) >> RX_DESC_PKT_LEN_SHIFT;


        if (NumBytesTransferred >= sizeof(RX_DESC) + pktLen)
        {
            nbl = NdisAllocateNetBufferList(NULL, 0, 0);
            if (nbl)
            {
                nb = NdisAllocateNetBuffer(nbl, NULL, 0, pktLen);
                if (nb)
                {
                    data = NdisGetDataBuffer(nb, NULL, NULL, 1, 0);
                    WdfMemoryCopyToBuffer(Buffer, sizeof(RX_DESC), data, pktLen);
                    NdisMIndicateReceiveNetBufferLists(adapterContext->NdisAdapterHandle, nbl, 0, 1, 0);
                }
                else
                {
                    NdisFreeNetBufferList(nbl);
                }
            }
        }
    }
}
