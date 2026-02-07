#include "vhidmini.h"
#include "vhidmini_ioctl.h"

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;

NTSTATUS
UserQueueCreate(
    _In_  WDFDEVICE         Device,
    _Out_ WDFQUEUE*         Queue
) {
    NTSTATUS                status;
    WDF_IO_QUEUE_CONFIG     queueConfig;
    WDF_OBJECT_ATTRIBUTES   queueAttributes;
    WDFQUEUE                queue;
    PQUEUE_CONTEXT          queueContext;

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchParallel);

    queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&queueAttributes, QUEUE_CONTEXT);

    status = WdfIoQueueCreate(Device, &queueConfig, &queueAttributes, &queue);
    if (!NT_SUCCESS(status)) {
        KdPrint(("WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }
    queueContext = GetQueueContext(queue);
    queueContext->Queue = queue;
    queueContext->DeviceContext = GetDeviceContext(Device);

    status = WdfDeviceConfigureRequestDispatching(Device, queue, WdfRequestTypeDeviceControl);
    if (!NT_SUCCESS(status)) {
        KdPrint(("WdfDeviceConfigureRequestDispatching failed 0x%x\n", status));
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(Device, &GUID_DEVINTERFACE_VHIDMINI, NULL);
    if (!NT_SUCCESS(status)) {
        KdPrint(("WdfDeviceCreateDeviceInterface failed 0x%x\n", status));
        return status;
    }

    *Queue = queue;
    return status;
}

NTSTATUS SendReport(PDEVICE_CONTEXT Ctx, VOID* Report, size_t Size)
{
    WDFREQUEST request;
    NTSTATUS status;

    status = WdfIoQueueRetrieveNextRequest(Ctx->ManualQueue, &request);
    if (!NT_SUCCESS(status))
        return status;

    status = RequestCopyFromBuffer(request, Report, Size);
    WdfRequestComplete(request, status);
    return status;
}

VOID updateKey(PHID_KEYBOARD_REPORT report, UCHAR old, UCHAR new) {
    for (int i = 0; i < 6; i++) {
        if (report->Keys[i] == old) {
            report->Keys[i] = new;
            return;
        }
    }
}

VOID
EvtIoDeviceControl(
    _In_  WDFQUEUE          Queue,
    _In_  WDFREQUEST        Request,
    _In_  size_t            OutputBufferLength,
    _In_  size_t            InputBufferLength,
    _In_  ULONG             IoControlCode
)
{
    PDEVICE_CONTEXT          deviceContext = GetQueueContext(Queue)->DeviceContext;
    UNREFERENCED_PARAMETER(OutputBufferLength);

    KdPrint(("IOCtl received 0x%x\n", IoControlCode));

	NTSTATUS status;
    switch (IoControlCode)
    {
    case IOCTL_VHIDMINI_KEY_EVENT:
    {
        if (InputBufferLength < sizeof(VHID_KEY_EVENT)) {
            status = STATUS_INVALID_BUFFER_SIZE;
            break;
        }
        PVHID_KEY_EVENT keyEvent;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(VHID_KEY_EVENT), (PVOID*)&keyEvent, NULL);
        if (NT_SUCCESS(status)) {
			UCHAR KeyCode = keyEvent->KeyCode;
            WdfWaitLockAcquire(deviceContext->StateLock, NULL);
            if (KeyCode >= 0xE0 && KeyCode <= 0xE7) {
                UCHAR mask = 1 << (KeyCode - 0xE0);
                if (keyEvent->Pressed)
                    deviceContext->KeyboardState.Modifiers |= mask;
                else
                    deviceContext->KeyboardState.Modifiers &= ~mask;
            }
            else {
                if (keyEvent->Pressed)
                    updateKey(&deviceContext->KeyboardState, 0, KeyCode);
                else
                    updateKey(&deviceContext->KeyboardState, KeyCode, 0);
            }
			if(SendReport(deviceContext, &deviceContext->KeyboardState, sizeof(HID_KEYBOARD_REPORT)) == STATUS_NO_MORE_ENTRIES)
				deviceContext->KeyboardChanged = TRUE;
            WdfWaitLockRelease(deviceContext->StateLock);
        }
        break;
	}
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    WdfRequestComplete(Request, status);
}