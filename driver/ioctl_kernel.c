
#include "vhidmini.h"


EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL EvtIoInternalDeviceControl;

//
// This is the default report descriptor for the virtual Hid device returned
// by the mini driver in response to IOCTL_HID_GET_REPORT_DESCRIPTOR.
//
HID_REPORT_DESCRIPTOR G_DefaultReportDescriptor[] = {
    // ===== KEYBOARD =====
    0x05, 0x01,       // USAGE_PAGE (Generic Desktop)
    0x09, 0x06,       // USAGE (Keyboard)
    0xA1, 0x01,       // COLLECTION (Application)
    0x85, 0x01,       // REPORT_ID (1)

    0x05, 0x07,       // USAGE_PAGE (Keyboard/Keypad)
    0x19, 0xE0,       // Usage Minimum (224, Left Control)
    0x29, 0xE7,       // Usage Maximum (231, Right GUI)
    0x15, 0x00,       // Logical Minimum (0)
    0x25, 0x01,       // Logical Maximum (1)
    0x75, 0x01,
    0x95, 0x08,       // Report count
    0x81, 0x02,       // Modifiers

    0x95, 0x01,       // Report count
    0x75, 0x08,
    0x81, 0x01,       // Reserved

    0x95, 0x06,       // Report count
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,       // Keys
    0xC0,

    // ===== MOUSE =====
    0x05, 0x01,       // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,       // USAGE (Mouse)
    0xA1, 0x01,       // COLLECTION (Application)
    0x85, 0x02,       // Report ID (2)

    0x09, 0x01,       // Usage (Pointer)
    0xA1, 0x00,       // Collection (Physical)
    0x05, 0x09,       // Usage Page (Buttons)
    0x19, 0x01,       // Usage Minimum = Button 1
    0x29, 0x03,       // Usage Maximum = Button 3
    0x15, 0x00,       // Logical Min = 0
    0x25, 0x01,       // Logical Max = 1
    0x95, 0x03,       // Report size
    0x75, 0x01,
    0x81, 0x02,       // Input (Data, Variable, Absolute)

    0x95, 0x01,
    0x75, 0x05,
    0x81, 0x01,       // Input (Constant) -> padding pour aligner à 1 octet

    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x30,       // Usage X
    0x09, 0x31,       // Usage Y
    0x15, 0x81,
    0x25, 0x7F,
    0x75, 0x08,
    0x95, 0x02,
    0x81, 0x06,       // Input (Data, Variable, Relative)
    0xC0
};

//
// This is the default HID descriptor returned by the mini driver
// in response to IOCTL_HID_GET_DEVICE_DESCRIPTOR. The size
// of report descriptor is currently the size of G_DefaultReportDescriptor.
//

HID_DESCRIPTOR              G_DefaultHidDescriptor = {
    0x09,   // length of HID descriptor
    0x21,   // descriptor type == HID  0x21
    0x0100, // hid spec release
    0x00,   // country code == Not Specified
    0x01,   // number of HID class descriptors
    {                                       //DescriptorList[0]
        0x22,                               //report descriptor type 0x22
        sizeof(G_DefaultReportDescriptor)   //total length of report descriptor
    }
};

NTSTATUS
KernelQueueCreate(
    _In_  WDFDEVICE         Device,
    _Out_ WDFQUEUE          *Queue
    )
/*++
Routine Description:

    This function creates a default, parallel I/O queue to proces IOCTLs
    from hidclass.sys.

Arguments:

    Device - Handle to a framework device object.

    Queue - Output pointer to a framework I/O queue handle, on success.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS                status;
    WDF_IO_QUEUE_CONFIG     queueConfig;
    WDF_OBJECT_ATTRIBUTES   queueAttributes;
    WDFQUEUE                queue;
    PQUEUE_CONTEXT          queueContext;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

    queueConfig.EvtIoInternalDeviceControl = EvtIoInternalDeviceControl;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&queueAttributes, QUEUE_CONTEXT);

    status = WdfIoQueueCreate(Device, &queueConfig, &queueAttributes, &queue);
    if (!NT_SUCCESS(status)) {
        KdPrint(("WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }

    queueContext = GetQueueContext(queue);
    queueContext->Queue = queue;
    queueContext->DeviceContext = GetDeviceContext(Device);

    *Queue = queue;
    return status;
}


NTSTATUS
ReadReport(
    _In_  PQUEUE_CONTEXT    QueueContext,
    _In_  WDFREQUEST        Request,
    _Always_(_Out_)
    BOOLEAN* CompleteRequest
)
{
    NTSTATUS                status;
	PDEVICE_CONTEXT		    deviceContext = QueueContext->DeviceContext;

    KdPrint(("ReadReport\n"));

    WdfWaitLockAcquire(deviceContext->StateLock, NULL);
    if(deviceContext->KeyboardChanged) {
		status = RequestCopyFromBuffer(Request, &deviceContext->KeyboardState, sizeof(HID_KEYBOARD_REPORT));
        deviceContext->KeyboardChanged = FALSE;
        *CompleteRequest = TRUE;
        return status;
    }
    if (deviceContext->MouseChanged) {
        status = RequestCopyFromBuffer(Request, &deviceContext->MouseState, sizeof(HID_MOUSE_REPORT));
        deviceContext->MouseChanged = FALSE;
        *CompleteRequest = TRUE;
        return status;
    }
    WdfWaitLockRelease(deviceContext->StateLock);

    //
    // forward the request to manual queue
    //
    status = WdfRequestForwardToIoQueue(Request, deviceContext->ManualQueue);
    if (!NT_SUCCESS(status)) {
        KdPrint(("WdfRequestForwardToIoQueue failed with 0x%x\n", status));
        *CompleteRequest = TRUE;
    }
    else {
        *CompleteRequest = FALSE;
    }

    return status;
}

NTSTATUS
GetInputReport(
    _In_  PQUEUE_CONTEXT QueueContext,
    _In_  WDFREQUEST     Request
)
{
    NTSTATUS status;
    HID_XFER_PACKET packet;

    status = RequestGetHidXferPacket_ToReadFromDevice(Request, &packet);
    if (!NT_SUCCESS(status)) return status;

    switch (packet.reportId)
    {
    case KEYBOARD_REPORT_ID:
    {
        if (packet.reportBufferLen != sizeof(HID_KEYBOARD_REPORT))
            return STATUS_INVALID_BUFFER_SIZE;
        
        WdfWaitLockAcquire(QueueContext->DeviceContext->StateLock, NULL);
        RtlCopyMemory(packet.reportBuffer, &QueueContext->DeviceContext->KeyboardState, sizeof(HID_KEYBOARD_REPORT));
        WdfWaitLockRelease(QueueContext->DeviceContext->StateLock);
        WdfRequestSetInformation(Request, sizeof(HID_KEYBOARD_REPORT));
        break;
    }
    case MOUSE_REPORT_ID:
    {
        if (packet.reportBufferLen != sizeof(HID_MOUSE_REPORT))
            return STATUS_INVALID_BUFFER_SIZE;

        WdfWaitLockAcquire(QueueContext->DeviceContext->StateLock, NULL);
        RtlCopyMemory(packet.reportBuffer, &QueueContext->DeviceContext->MouseState, sizeof(HID_MOUSE_REPORT));
        WdfWaitLockRelease(QueueContext->DeviceContext->StateLock);
        WdfRequestSetInformation(Request, sizeof(HID_MOUSE_REPORT));
        break;
    }
    default:
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
SetOutputReport(
    _In_  PQUEUE_CONTEXT QueueContext,
    _In_  WDFREQUEST     Request
)
{
    UNREFERENCED_PARAMETER(QueueContext);

    HID_XFER_PACKET packet;
    NTSTATUS status = RequestGetHidXferPacket_ToWriteToDevice(Request, &packet);
    if (!NT_SUCCESS(status)) return status;

    switch (packet.reportId)
    {
    case KEYBOARD_REPORT_ID:
        if (packet.reportBufferLen != sizeof(HID_KEYBOARD_REPORT))
            return STATUS_DEVICE_DATA_ERROR;
        WdfRequestSetInformation(Request, sizeof(HID_KEYBOARD_REPORT));
        break;

    case MOUSE_REPORT_ID:
        if (packet.reportBufferLen != sizeof(HID_MOUSE_REPORT))
            return STATUS_DEVICE_DATA_ERROR;
        WdfRequestSetInformation(Request, sizeof(HID_MOUSE_REPORT));
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
GetStringId(
    _In_  WDFREQUEST        Request,
    _Out_ ULONG* StringId,
    _Out_ ULONG* LanguageId
)
/*++

Routine Description:

    Helper routine to decode IOCTL_HID_GET_INDEXED_STRING and IOCTL_HID_GET_STRING.

Arguments:

    Request - Pointer to Request Packet.

Return Value:

    NT status code.

--*/
{
    NTSTATUS                status;
    ULONG                   inputValue;

    WDF_REQUEST_PARAMETERS  requestParameters;

    //
    // IOCTL_HID_GET_STRING:                      // METHOD_NEITHER
    // IOCTL_HID_GET_INDEXED_STRING:              // METHOD_OUT_DIRECT
    //
    // The string id (or string index) is passed in Parameters.DeviceIoControl.
    // Type3InputBuffer. However, Parameters.DeviceIoControl.InputBufferLength
    // was not initialized by hidclass.sys, therefore trying to access the
    // buffer with WdfRequestRetrieveInputMemory will fail
    //
    // Another problem with IOCTL_HID_GET_INDEXED_STRING is that METHOD_OUT_DIRECT
    // expects the input buffer to be Irp->AssociatedIrp.SystemBuffer instead of
    // Type3InputBuffer. That will also fail WdfRequestRetrieveInputMemory.
    //
    // The solution to the above two problems is to get Type3InputBuffer directly
    //
    // Also note that instead of the buffer's content, it is the buffer address
    // that was used to store the string id (or index)
    //

    WDF_REQUEST_PARAMETERS_INIT(&requestParameters);
    WdfRequestGetParameters(Request, &requestParameters);

    inputValue = PtrToUlong(
        requestParameters.Parameters.DeviceIoControl.Type3InputBuffer);

    status = STATUS_SUCCESS;

    //
    // The least significant two bytes of the INT value contain the string id.
    //
    *StringId = (inputValue & 0x0ffff);

    //
    // The most significant two bytes of the INT value contain the language
    // ID (for example, a value of 1033 indicates English).
    //
    *LanguageId = (inputValue >> 16);

    return status;
}


NTSTATUS
GetIndexedString(
    _In_  WDFREQUEST        Request
)
/*++

Routine Description:

    Handles IOCTL_HID_GET_INDEXED_STRING

Arguments:

    Request - Pointer to Request Packet.

Return Value:

    NT status code.

--*/
{
    NTSTATUS                status;
    ULONG                   languageId, stringIndex;

    status = GetStringId(Request, &stringIndex, &languageId);

    // While we don't use the language id, some minidrivers might.
    //
    UNREFERENCED_PARAMETER(languageId);

    if (NT_SUCCESS(status)) {

        if (stringIndex != VHIDMINI_DEVICE_STRING_INDEX)
        {
            status = STATUS_INVALID_PARAMETER;
            KdPrint(("GetString: unkown string index %d\n", stringIndex));
            return status;
        }

        status = RequestCopyFromBuffer(Request, VHIDMINI_DEVICE_STRING, sizeof(VHIDMINI_DEVICE_STRING));
    }
    return status;
}


NTSTATUS
GetString(
    _In_  WDFREQUEST        Request
)
/*++

Routine Description:

    Handles IOCTL_HID_GET_STRING.

Arguments:

    Request - Pointer to Request Packet.

Return Value:

    NT status code.

--*/
{
    NTSTATUS                status;
    ULONG                   languageId, stringId;
    size_t                  stringSizeCb;
    PWSTR                   string;

    status = GetStringId(Request, &stringId, &languageId);

    // While we don't use the language id, some minidrivers might.
    //
    UNREFERENCED_PARAMETER(languageId);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    switch (stringId) {
    case HID_STRING_ID_IMANUFACTURER:
        stringSizeCb = sizeof(VHIDMINI_MANUFACTURER_STRING);
        string = VHIDMINI_MANUFACTURER_STRING;
        break;
    case HID_STRING_ID_IPRODUCT:
        stringSizeCb = sizeof(VHIDMINI_PRODUCT_STRING);
        string = VHIDMINI_PRODUCT_STRING;
        break;
    case HID_STRING_ID_ISERIALNUMBER:
        stringSizeCb = sizeof(VHIDMINI_SERIAL_NUMBER_STRING);
        string = VHIDMINI_SERIAL_NUMBER_STRING;
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        KdPrint(("GetString: unkown string id %d\n", stringId));
        return status;
    }

    status = RequestCopyFromBuffer(Request, string, stringSizeCb);
    return status;
}

VOID
EvtIoInternalDeviceControl(
    _In_  WDFQUEUE          Queue,
    _In_  WDFREQUEST        Request,
    _In_  size_t            OutputBufferLength,
    _In_  size_t            InputBufferLength,
    _In_  ULONG             IoControlCode
)
/*++
Routine Description:

    This event callback function is called when the driver receives an

    (KMDF) IOCTL_HID_Xxx code when handlng IRP_MJ_INTERNAL_DEVICE_CONTROL
    (UMDF) IOCTL_HID_Xxx, IOCTL_UMDF_HID_Xxx when handling IRP_MJ_DEVICE_CONTROL

Arguments:

    Queue - A handle to the queue object that is associated with the I/O request

    Request - A handle to a framework request object.

    OutputBufferLength - The length, in bytes, of the request's output buffer,
            if an output buffer is available.

    InputBufferLength - The length, in bytes, of the request's input buffer, if
            an input buffer is available.

    IoControlCode - The driver or system defined IOCTL associated with the request

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS                status;
    BOOLEAN                 completeRequest = TRUE;
    WDFDEVICE               device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT         deviceContext = NULL;
    PQUEUE_CONTEXT          queueContext = GetQueueContext(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    deviceContext = GetDeviceContext(device);

    switch (IoControlCode)
    {
    case IOCTL_HID_GET_DEVICE_ATTRIBUTES:   // METHOD_NEITHER
        //
        //Retrieves a device's attributes in a HID_DEVICE_ATTRIBUTES structure.
        //
        status = RequestCopyFromBuffer(Request,
            &queueContext->DeviceContext->HidDeviceAttributes,
            sizeof(HID_DEVICE_ATTRIBUTES));
        break;
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:   // METHOD_NEITHER
        //
        // Retrieves the device's HID descriptor.
        //
        _Analysis_assume_(G_DefaultHidDescriptor.bLength != 0);
        status = RequestCopyFromBuffer(Request,
            &G_DefaultHidDescriptor,
            G_DefaultHidDescriptor.bLength);
        break;
    case IOCTL_HID_GET_REPORT_DESCRIPTOR:   // METHOD_NEITHER
        //
        //Obtains the report descriptor for the HID device.
        //
        status = RequestCopyFromBuffer(Request,
            &G_DefaultReportDescriptor,
            sizeof(G_DefaultReportDescriptor));
        break;

    case IOCTL_HID_READ_REPORT:             // METHOD_NEITHER
        //
        // Returns a report from the device into a class driver-supplied
        // buffer.
        //
        status = ReadReport(queueContext, Request, &completeRequest);
        break;
    case IOCTL_HID_GET_INPUT_REPORT:        // METHOD_OUT_DIRECT
        status = GetInputReport(queueContext, Request);
        break;

    case IOCTL_HID_SET_OUTPUT_REPORT:       // METHOD_IN_DIRECT
    case IOCTL_HID_WRITE_REPORT:            // METHOD_NEITHER
		status = SetOutputReport(queueContext, Request);
        break;


    case IOCTL_HID_GET_STRING:                      // METHOD_NEITHER
        status = GetString(Request);
        break;
    case IOCTL_HID_GET_INDEXED_STRING:              // METHOD_OUT_DIRECT
        status = GetIndexedString(Request);
        break;

    case IOCTL_HID_GET_FEATURE:             // METHOD_OUT_DIRECT
    case IOCTL_HID_SET_FEATURE:             // METHOD_IN_DIRECT
    case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:  // METHOD_NEITHER
        //
        // This has the USBSS Idle notification callback. If the lower driver
        // can handle it (e.g. USB stack can handle it) then pass it down
        // otherwise complete it here as not inplemented. For a virtual
        // device, idling is not needed.
        //
        // Not implemented. fall through...
        //
    case IOCTL_HID_ACTIVATE_DEVICE:                 // METHOD_NEITHER
    case IOCTL_HID_DEACTIVATE_DEVICE:               // METHOD_NEITHER
    case IOCTL_GET_PHYSICAL_DESCRIPTOR:             // METHOD_OUT_DIRECT
        //
        // We don't do anything for these IOCTLs but some minidrivers might.
        //
        // Not implemented. fall through...
        //
    default:
        status = STATUS_NOT_IMPLEMENTED;
        break;
    }

    //
    // Complete the request. Information value has already been set by request
    // handlers.
    //
    if (completeRequest) {
        WdfRequestComplete(Request, status);
    }
}