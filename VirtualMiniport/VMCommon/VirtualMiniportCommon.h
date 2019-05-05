/*++

Module Name:

    VirtualMiniportCommon.h

Date:

    1-Feb-2014

Abstract:

    Header contains the common declarations that can be shared
    by the driver and the control program.

--*/

#ifndef __VIRTUAL_MINIPORT_COMMON_H_
#define __VIRTUAL_MINIPORT_COMMON_H_

#include <ntddscsi.h>
#include <guiddef.h>

//
// Symbolic name for our control device
//

#define VIRTUAL_MINIPORT_CONTROL_DEVICE_SYMBOLIC_NAME L"\\DosDevices\\VMControlDevice"
#define VIRTUAL_MINIPORT_CONTROL_DEVICE L"\\\\.\\VMControlDevice"

//
// Max buffer size
//

#define VIRTUAL_MINIPORT_MAX_BUFFER 255

//
// Device GUID to allow users to discover the VMMiniport HBA Device PDOs by name. 
// But use device interface class to do so. {A1629363-FB34-469C-9699-B80A2BA22CCF}
//

DEFINE_GUID(GUID_DEVINTERFACE_VIRTUAL_MINIPORT_DEVICE, 0xa1629363, 0xfb34, 0x469c, 0x96, 0x99, 0xb8, 0xa, 0x2b, 0xa2, 0x2c, 0xcf );


//
// Type definitions for IOCTLs
//
#define VIRTUAL_MINIPORT_MAX_TIERS 2
typedef enum _VIRTUAL_MINIPORT_TIER {
    VMTierMin,
    VMTierNone = VMTierMin,
    VMTierPhysicalMemory,
    VMTierFile,
    VMTierMax = VMTierFile
}VIRTUAL_MINIPORT_TIER, *PVIRTUAL_MINIPORT_TIER;

typedef struct _VIRTUAL_MINIPORT_TARGET_TIER_DESCRIPTOR {
    VIRTUAL_MINIPORT_TIER Tier;
    ULONGLONG TierSize;  // Unit: MegaBytes

    //
    // Tier specific details
    //
}VIRTUAL_MINIPORT_TARGET_TIER_DESCRIPTOR, *PVIRTUAL_MINIPORT_TARGET_TIER_DESCRIPTOR;

typedef struct _VIRTUAL_MINIPORT_DUMMY_DATA {
    UCHAR Buffer [100];
}VIRTUAL_MINIPORT_DUMMY_DATA, *PVIRTUAL_MINIPORT_DUMMY_DATA;

typedef struct _VIRTUAL_MINIPORT_ADAPTER_LIST {
    ULONG AdapterCount;
    GUID AdapterIds [1]; // Variable-sized array of GUIDs;
}VIRTUAL_MINIPORT_ADAPTER_LIST, *PVIRTUAL_ADAPTER_MINIPORT_LIST;

typedef struct _VIRTUAL_MINIPORT_ADAPTER_DETAILS {
    GUID AdapterId;

    ULONG MaxBusCount;
    ULONG BusCount;
    UCHAR Buses [1];
}VIRTUAL_MINIPORT_ADAPTER_DETAILS, *PVIRTUAL_MINIPORT_ADAPTER_DETAILS;

typedef struct _VIRTUAL_MINIPORT_BUS_DETAILS {
    GUID AdapterId;
    UCHAR Bus;

    ULONG MaxTargetCount;
    ULONG TargetCount;
    UCHAR Targets [1];
}VIRTUAL_MINIPORT_BUS_DETAILS, *PVIRTUAL_MINIPORT_BUS_DETAILS;

typedef enum _VIRTUAL_MINIPORT_BLOCK_SIZE {
    //
    // Unit: Bytes
    //
    VMBlockSizeDefault = 0x200,
    VMBlockSize512 = VMBlockSizeDefault,
    VMBlockSize1024 = 0x400,
    VMBlockSize4096 = 0x1000
}VIRTUAL_MINIPORT_BLOCK_SIZE, *PVIRTUAL_MINIPORT_BLOCK_SIZE;

typedef struct _VIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR {
    //
    // Location of the device
    //

    GUID AdapterId;
    UCHAR Bus;

    //
    // Device description
    //

    ULONGLONG Size;                        // Unit: Bytes
    VIRTUAL_MINIPORT_BLOCK_SIZE BlockSize; // Unit: Bytes
    ULONG TierCount;
    VIRTUAL_MINIPORT_TARGET_TIER_DESCRIPTOR TierDescription [VIRTUAL_MINIPORT_MAX_TIERS];
}VIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR, *PVIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR;

typedef struct _VIRTUAL_MINIPORT_TARGET_DEVICE_DETAILS {
    ULONGLONG Size;                        // Bytes
    VIRTUAL_MINIPORT_BLOCK_SIZE BlockSize; // Bytes
    ULONGLONG MaxBlocks;
    ULONG LogicalDeviceCount;
    ULONG TierCount;
}VIRTUAL_MINIPORT_TARGET_DEVICE_DETAILS, *PVIRTUAL_MINIPORT_TARGET_DEVICE_DETAILS;

typedef struct _VIRTUAL_MINIPORT_TARGET_DETAILS {
    //
    //Input
    //
    GUID AdapterId;
    UCHAR Bus;
    UCHAR Target;

    //
    // Output
    //
    ULONG MaxLunCount;
    ULONG LunCount;
    VIRTUAL_MINIPORT_TARGET_DEVICE_DETAILS DeviceDetails;

    //
    // Variable part
    //
    UCHAR Luns [1];
}VIRTUAL_MINIPORT_TARGET_DETAILS, *PVIRTUAL_MINIPORT_TARGET_DETAILS;

typedef struct _VIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR {
    //
    // Input
    //
    GUID AdapterId;
    UCHAR Bus;
    UCHAR Target;
    ULONGLONG Size;
    BOOLEAN ThinProvision; // for now will be false always
}VIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR, *PVIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR;

typedef struct _VIRTUAL_MINIPORT_LOGICAL_DEVICE_DETAILS {
    ULONGLONG Size;                        // Bytes
    VIRTUAL_MINIPORT_BLOCK_SIZE BlockSize; // Bytes
    ULONGLONG MaxBlocks;
    BOOLEAN ThinProvison;
}VIRTUAL_MINIPORT_LOGICAL_DEVICE_DETAILS, *PVIRTUAL_MINIPORT_LOGICAL_DEVICE_DETAILS;

typedef struct _VIRTUAL_MINIPORT_LUN_DETAILS {
    //
    // Input
    //
    GUID AdapterId;
    UCHAR Bus;
    UCHAR Target;
    UCHAR Lun;

    //
    // Output
    //
    VIRTUAL_MINIPORT_LOGICAL_DEVICE_DETAILS DeviceDetails;
}VIRTUAL_MINIPORT_LUN_DETAILS, *PVIRTUAL_MINIPORT_LUN_DETAILS;

typedef struct _VIRTUAL_MINIPORT_IOCTL_DESCRIPTOR {
    SRB_IO_CONTROL SrbIoControl;

    union {
        VIRTUAL_MINIPORT_DUMMY_DATA DummyData;

        //
        // Create Input
        //
        VIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR CreateTarget;
        VIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR CreateLun;

        //
        // Query
        //
        VIRTUAL_MINIPORT_ADAPTER_DETAILS AdapterDetails;
        VIRTUAL_MINIPORT_BUS_DETAILS BusDetails;
        VIRTUAL_MINIPORT_TARGET_DETAILS TargetDetails;
        VIRTUAL_MINIPORT_LUN_DETAILS LunDetails;
    }RequestResponse;

}VIRTUAL_MINIPORT_IOCTL_DESCRIPTOR, *PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR;

//
// IOCTL signature that we will pass in each of our IOCTL
//

#define VIRTUAL_MINIPORT_IOCTL_SIGNATURE "VMIoSig"

//
// Define the IO control codes and respective types
//

typedef enum _IOCTL_VIRTUAL_MINIPORT {
    //
    // Dymmy IOCTL for test purposes
    //

    IOCTL_VIRTUAL_MINIPORT_DUMMY,

    //
    // Adapter IOCTLs
    //

    //
    // Query list of adapters
    // Input- none
    // Output - PVIRTUAL_MINIPORT_ADAPTER_LIST
    //

    IOCTL_VIRTUAL_MINIPORT_QUERY_ADAPTER_IDENTIFIEERS,

    //
    // Query adapter details, given the adapter ID
    // Output - PVIRTUAL_MINIPORT_ADAPTER_DETAILS
    //

    IOCTL_VIRTUAL_MINIPORT_QUERY_ADAPTER_DETAILS,

    //
    // BUS IOCTLs
    //

    //
    // Create Bus IOCTL, allows us to request creation of a bus on an adapter
    // Output - VIRTUAL_MINIPORT_ADAPTER_DETAILS
    //

    IOCTL_VIRTUAL_MINIPORT_CREATE_BUS,

    //
    // Query Bus IOCTL, allows us to request details of a bus
    // Input - PVIRTUAL_MINIPORT_BUS_ADDRESS
    // Output - PVIRTUAL_MINIPORT_BUS_DETAILS
    //

    IOCTL_VIRTUAL_MINIPORT_QUERY_BUS_DETAILS,

    //
    // Target IOCTLs
    //

    //
    // Create Target, allows us to create a target/device on a given adapter and a bus
    // Input - PVIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR
    // Output - PVIRTUAL_MINIPORT_TARGET_ADDRESS
    //

    IOCTL_VIRTUAL_MINIPORT_CREATE_TARGET,

    //
    // Query Target IOCTL, allows us to request details of a target
    // Input - PVIRTUAL_MINIPORT_TARGET_ADDRESS
    // Output - PVIRTUAL_MINIPORT_TARGET_DETAILS
    //

    IOCTL_VIRTUAL_MINIPORT_QUERY_TARGET_DETAILS,

    //
    // Lun IOCTLs
    //

    //
    // Create Lun IOCTL, allows us to create a Lun on a given Adapter, Bus and Target
    // Input - PVIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR
    // Output - PVIRTUAL_MINIPORT_LUN_ADDRESS
    //

    IOCTL_VIRTUAL_MINIPORT_CREATE_LUN,

    //
    // Query Lun IOCTL, allows us to request details of a Lun
    // Input - PVIRTUAL_MINIPORT_LUN_ADDRESS
    // Output - PVIRTUAL_MINIPORT_LUN_DETAILS
    //

    IOCTL_VIRTUAL_MINIPORT_QUERY_LUN_DETAILS
}IOCTL_VIRTUAL_MINIPORT, *PIOCTL_VIRTUAL_MINIPORT;

#endif //__VIRTUAL_MINIPORT_COMMON_H_