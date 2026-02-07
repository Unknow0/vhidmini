#ifndef __VHIDMINI_H_
#define __VHIDMINI_H_

#include <ntddk.h>

#include <wdf.h>

#include <hidport.h>

typedef UCHAR HID_REPORT_DESCRIPTOR, *PHID_REPORT_DESCRIPTOR;

#define MAXIMUM_STRING_LENGTH           (126 * sizeof(WCHAR))
#define VHIDMINI_MANUFACTURER_STRING    L"UMDF Virtual hidmini device Manufacturer string"  
#define VHIDMINI_PRODUCT_STRING         L"UMDF Virtual hidmini device Product string"  
#define VHIDMINI_SERIAL_NUMBER_STRING   L"UMDF Virtual hidmini device Serial Number string"  
#define VHIDMINI_DEVICE_STRING          L"UMDF Virtual hidmini device"  
#define VHIDMINI_DEVICE_STRING_INDEX    5

#include <pshpack1.h>

typedef struct _HID_KEYBOARD_REPORT {
    UCHAR ReportId;      // Report ID = 1
    UCHAR Modifiers;     // Ctrl, Shift, Alt, GUI
    UCHAR Reserved;      // Toujours 0
    UCHAR Keys[6];       // Codes des touches
} HID_KEYBOARD_REPORT, * PHID_KEYBOARD_REPORT;

typedef struct _HID_MOUSE_REPORT {
    UCHAR ReportId;      // Report ID = 2
    UCHAR Buttons;       // bits 0-2 = bouton1-3, bits 3-7 padding
    CHAR X;              // mouvement X relatif
    CHAR Y;              // mouvement Y relatif
} HID_MOUSE_REPORT, * PHID_MOUSE_REPORT;

//
// Misc definitions
//
#define KEYBOARD_REPORT_ID   0x01
#define MOUSE_REPORT_ID   0x02

//
// These are the device attributes returned by the mini driver in response
// to IOCTL_HID_GET_DEVICE_ATTRIBUTES.
//
#define HIDMINI_PID             0xFEED
#define HIDMINI_VID             0xDEED
#define HIDMINI_VERSION         0x0101

#include <poppack.h>

DRIVER_INITIALIZE                   DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD           EvtDeviceAdd;

typedef struct _DEVICE_CONTEXT
{
    WDFDEVICE               Device;
    WDFQUEUE                QueueKernel;
    WDFQUEUE                ManualQueue;
    WDFQUEUE                QueueUser;
    HID_DEVICE_ATTRIBUTES   HidDeviceAttributes;
    WDFWAITLOCK             StateLock;
	HID_KEYBOARD_REPORT     KeyboardState;
    int                     KeyboardChanged;
	HID_MOUSE_REPORT        MouseState;
    int                     MouseChanged;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

typedef struct _QUEUE_CONTEXT
{
    WDFQUEUE                Queue;
    PDEVICE_CONTEXT         DeviceContext;
} QUEUE_CONTEXT, *PQUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, GetQueueContext);

NTSTATUS
KernelQueueCreate(
    _In_  WDFDEVICE         Device,
    _Out_ WDFQUEUE          *Queue
    );

NTSTATUS
ManualQueueCreate(
    _In_  WDFDEVICE         Device,
    _Out_ WDFQUEUE          *Queue
    );

NTSTATUS
UserQueueCreate(
    _In_  WDFDEVICE         Device,
    _Out_ WDFQUEUE* Queue
);

NTSTATUS
RequestCopyFromBuffer(
    _In_  WDFREQUEST        Request,
    _In_  PVOID             SourceBuffer,
    _When_(NumBytesToCopyFrom == 0, __drv_reportError(NumBytesToCopyFrom cannot be zero))
    _In_  size_t            NumBytesToCopyFrom
    );

NTSTATUS
RequestGetHidXferPacket_ToReadFromDevice(
    _In_  WDFREQUEST        Request,
    _Out_ HID_XFER_PACKET  *Packet
    );

NTSTATUS
RequestGetHidXferPacket_ToWriteToDevice(
    _In_  WDFREQUEST        Request,
    _Out_ HID_XFER_PACKET  *Packet
    );

#endif // __VHIDMINI_H_