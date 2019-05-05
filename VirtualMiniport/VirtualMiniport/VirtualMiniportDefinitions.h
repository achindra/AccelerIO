/*++

Module Name:

    VirtualMiniportDefinitions.h

Date:

    4-Jan-2014

Abstract:

    Module contains the type definitions for the Virtual Storport.

    Here is the layout of all the components and the way they are linked
    to each other

    VIRTUAL_MINIPORT_DEVICE_EXTENSION, ExtensionLock
    |
    |---AdapterCount
    |---Adapters
        |
        |--VIRTUAL_MINIPORT_ADAPTER_EXTENSION, AdapterLock
           |                |
           |---BusCount     |--Scheduler, SchedulerLock(SpinLock)
           |---Buses
               |
               |--VIRTUAL_MINIPORT_BUS, BusLock
                   |
                   |---TargetCount
                   |---Targets
                       |
                       |---VIRTUAL_MINIPORT_TARGET, TargetLock
                           |
                           |---LunCount
                           |---Luns
                               |
                               |---VIRTUAL_MINIPORT_LUN, LunLock
                                   |
                                   |---

    Locking rules

    *   Locks can be owned on individual types
    *   If multiple Locks have to be acquired, they are acquired from top-down order and release
        in reverse order
    *   If we need to acquire the top level lock, we need to release the sub-level locks and start
        acquiring the locks from top-down   
    *   Tearing down of types starts from the top, acquires the locks at each level and tears down
        all the types below and itself before returning back to the caller
    *   We can traverse from lower most component in the hierarchy back to top most component, but
        we should abide by the lock ordering rules

--*/

#ifndef __VIRTUAL_MINIPORT_DEFINITIONS_H_
#define __VIRTUAL_MINIPORT_DEFINITIONS_H_

#include <wdm.h>
#include <guiddef.h>
#include <VirtualMiniportProduct.h>
#include <VirtualMiniportWrapper.h>

#include <VirtualMiniportScheduler.h>
#include <VirtualMiniportDeviceTypes.h>

/*++

    Represents the types.
    DO NOT CHANGE THE ORDER OF THESE. VMDeviceFindDeviceByAddress depends on this ordering
    for validation

--*/

typedef enum _VM_TYPE {
    VMTypeAdapter,
    VMTypeBus,
    VMTypeTarget,
    VMTypeLun    
}VM_TYPE, *PVM_TYPE;

/*++

    Represents state of data structure.

--*/

typedef enum _VM_DEVICE_STATE {
    VMDeviceUninitialized,     // Initial and can be Final state; can be cleaned up
    VMDeviceInitialized,       // Initialized state, but cannot start processing yet
    VMDeviceAttached,          // Device is attached to parent
    VMDeviceStarted,           // Ready to process the requests
    VMDeviceStopPending,       // New requests will be rejected, queue requests being processed
    VMDeviceStopped,           // Ready to be detached
    VMDeviceDetached,          // Device is detached, ready to be cleaned up
    VMDeviceStateUnknown = -1  // For unknown inititializations
}VM_DEVICE_STATE, *PVM_DEVICE_STATE;

//
// Forward declarations of types for backward references
//

typedef struct _VIRTUAL_MINIPORT_BUS *PVIRTUAL_MINIPORT_BUS;
typedef struct _VIRTUAL_MINIPORT_TARGET *PVIRTUAL_MINIPORT_TARGET;
typedef struct _VIRTUAL_MINIPORT_ADAPTER_EXTENSION *PVIRTUAL_MINIPORT_ADAPTER_EXTENSION;
typedef struct _VIRTUAL_MINIPORT_DEVICE_EXTENSION *PVIRTUAL_MINIPORT_DEVICE_EXTENSION;

//
// SCSI type definitions
//

/*++

    Represents the extended SRB context; allocated by storport
    as part of SRB. Does not need synchronization as each request
    is processed by one execution context at a time.

    Owned by SCSI modules

--*/

#define VIRTUAL_MINIPORT_SIGNATURE_SRB_EXTENSION 'XSrb'
typedef struct _VIRTUAL_MINIPORT_SRB_EXTENSION {
    VIRTUAL_MINIPORT_SCHEDULER_WORKITEM Header;
    ULONG Signature;

    //
    // Adapter this request was sent to; This is important so that
    // when we defer the request, we still need to know which adapter
    // this request was directed to. For example, we need to know
    // the adapter to lookup the bus, target and lun as these are
    // unique only to an adapter
    //
    PVIRTUAL_MINIPORT_ADAPTER_EXTENSION Adapter; 
    PSCSI_REQUEST_BLOCK Srb;                        // Pointer to SRB that we are part of
}VIRTUAL_MINIPORT_SRB_EXTENSION, *PVIRTUAL_MINIPORT_SRB_EXTENSION;


/*++

    Represents a logical unit.

--*/

#define VIRTUAL_MINIPORT_SIGNATURE_LUN 'VLun'
typedef struct _VIRTUAL_MINIPORT_LUN {
    LIST_ENTRY List;    // Not used
    ULONG Signature;
    VM_TYPE Type;
    VM_DEVICE_STATE State;
    GUID UniqueId;

    VM_LOCK LunLock;
    UCHAR LunId;

    BOOLEAN DeviceCreated; // Logical device creation is deferred to attach
    VIRTUAL_MINIPORT_LOGICAL_DEVICE Device;

    PVIRTUAL_MINIPORT_TARGET Target; // Allow to get back to Target from Lun
}VIRTUAL_MINIPORT_LUN, *PVIRTUAL_MINIPORT_LUN;

/*++
    
    Represents a LUN extension. Allcocated by storport.
    We dont use it as I wanted to use it for now.

--*/

typedef struct _VIRTUAL_MINIPORT_LUN_EXTENSION {
    PVIRTUAL_MINIPORT_LUN Lun;
}VIRTUAL_MINIPORT_LUN_EXTENSION, *PVIRTUAL_MINIPORT_LUN_EXTENSION;

/*++

    Represents the Target

--*/

#define VIRTUAL_MINIPORT_SIGNATURE_TARGET 'VTar'
typedef struct _VIRTUAL_MINIPORT_TARGET {
    LIST_ENTRY List;    // Not used
    ULONG Signature;
    VM_TYPE Type;
    VM_DEVICE_STATE State;
    GUID UniqueId;

    VM_LOCK TargetLock;
    UCHAR TargetId;

    PVIRTUAL_MINIPORT_BUS Bus;  // Allow us to get back to Bus on which Target resides
    ULONG LunCount;
    ULONG MaxLunCount;
    //LIST_ENTRY Luns;
    VIRTUAL_MINIPORT_TIERED_DEVICE Device;
    PVIRTUAL_MINIPORT_LUN *Luns;
}VIRTUAL_MINIPORT_TARGET, *PVIRTUAL_MINIPORT_TARGET;

/*++

    Represents the bus

--*/

#define VIRTUAL_MINIPORT_SIGNATURE_BUS 'VBus'
typedef struct _VIRTUAL_MINIPORT_BUS {
    LIST_ENTRY List;    // No used
    ULONG Signature;
    VM_TYPE Type;
    VM_DEVICE_STATE State;
    GUID UniqueId;

    VM_LOCK BusLock;
    UCHAR BusId;

    PVIRTUAL_MINIPORT_ADAPTER_EXTENSION Adapter;    // Allow us to get back to Adapter who own's us
    ULONG TargetCount;
    ULONG MaxTargetCount;
    //LIST_ENTRY Targets;
    PVIRTUAL_MINIPORT_TARGET *Targets;
}VIRTUAL_MINIPORT_BUS, *PVIRTUAL_MINIPORT_BUS;

/*++

    Represents a virtual adapter we implement. This is
    allocated by the storport and passed down to us.

--*/

#define VIRTUAL_MINIPORT_SIGNATURE_ADAPTER_EXTENSION 'VAda'
typedef struct _VIRTUAL_MINIPORT_ADAPTER_EXTENSION {
    LIST_ENTRY List;
    ULONG Signature;
    VM_TYPE Type;
    VM_DEVICE_STATE State;
    GUID UniqueId;
    PVIRTUAL_MINIPORT_DEVICE_EXTENSION DeviceExtension;
    VM_LOCK AdapterLock;


    PDEVICE_OBJECT PhysicalDeviceObject;  // PDO of our stack; PnP owns the PDO in case of virtual miniport
    PDEVICE_OBJECT DeviceObject;          // HBA's FDO
    PDEVICE_OBJECT LowerDeviceObject; 
    UNICODE_STRING RegistryPath;
    UNICODE_STRING SymbolicLinkName;
    
    VIRTUAL_MINIPORT_SCHEDULER_DATABASE Scheduler;

    ULONG BusCount;
    ULONG MaxBusCount;
    //LIST_ENTRY Buses;
    PVIRTUAL_MINIPORT_BUS *Buses;
}VIRTUAL_MINIPORT_ADAPTER_EXTENSION, *PVIRTUAL_MINIPORT_ADAPTER_EXTENSION;

/*++

    Device name for control device

--*/

#define VIRTUAL_MINIPORT_CONTROL_DEVICE_NAME L"\\Device\\VMControlDevice"

/*++

    Relative location of parameters key

--*/

#define VIRTUAL_MINIPORT_PARAMETERS_KEY L"\\Parameters"
#define VIRTUAL_MINIPORT_CONFIGURATION_KEY L"\\Configuration"

/*
   Configuration constants
*/

#define VIRTUAL_MINIPORT_METADATA_LOCATION L"C:\\Windows\\"

/*++

    Global configuration for the driver

--*/

typedef struct _VIRTUAL_MINIPORT_CONFIGURATION {
    ULONG NumberOfAdapters;
    UCHAR BusesPerAdapter;
    UCHAR TargetsPerBus;
    UCHAR LunsPerTarget;
    ULONG PhysicalBreaks;

    BOOLEAN FreeUnicodeStringsAtUnload;
    BOOLEAN FreeAnsiStringsAtUnload;

#define VIRTUAL_MINIPORT_ID_STRING_LENGTH 8
    UNICODE_STRING VendorID;
    UNICODE_STRING ProductID;
    ANSI_STRING AnsiVendorID;
    ANSI_STRING AnsiProductID;

#define VIRTUAL_MINIPORT_PRODUCT_REVISION_STRING_LENGTH 4
    UNICODE_STRING ProductRevision;
    ANSI_STRING AnsiProductRevision;

    ULONGLONG DeviceSizeMax;
    UNICODE_STRING MetadataLocation;
}VIRTUAL_MINIPORT_CONFIGURATION, *PVIRTUAL_MINIPORT_CONFIGURATION;

/*++

    Represents the global state of the driver

--*/

#define VIRTUAL_MINIPORT_SIGNATURE 'VVm'
typedef struct _VIRTUAL_MINIPORT_DEVICE_EXTENSION {
    ULONG Signature;

    //
    // Below fields are not protected. They are initialized at driver load
    // and are read-only afterwards. So we dont protect them.
    //

    UNICODE_STRING RegistryPath;
    UNICODE_STRING ControlDeviceName;
    UNICODE_STRING ControlDeviceSymbolicName;
    PDRIVER_OBJECT DriverObject;
    VIRTUAL_MINIPORT_CONFIGURATION Configuration;

    //
    // Extension lock protects any here after.
    //

    VM_LOCK ExtensionLock;

    ULONG AdapterCount;
    LIST_ENTRY Adapters;
}VIRTUAL_MINIPORT_DEVICE_EXTENSION, *PVIRTUAL_MINIPORT_DEVICE_EXTENSION;

#endif //__VIRTUAL_MINIPORT_DEFINITIONS_H_