/*++

Module Name:

    VirtualMiniportDevice.h

Date:

    9-Feb-2014

Abstract:

    Module contains the prototypes of overall device management

--*/

#ifndef __VIRTUAL_MINIPORT_DEVICE_H_
#define __VIRTUAL_MINIPORT_DEVICE_H_

#include <wdm.h>
#include <ntstrsafe.h>
#include <storport.h>

#include <VirtualMiniportWrapper.h>
#include <VirtualMiniportSupportRoutines.h>
#include <VirtualMiniportTrace.h>
#include <VirtualMiniportAdapter.h>

//
// Device management routines
//

UCHAR
VMDeviceValidateAddress(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR Bus,
    _In_ UCHAR Target,
    _In_ UCHAR Lun
    );

UCHAR
VMDeviceReferenceAddress(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR Bus,
    _In_ UCHAR Target,
    _In_ UCHAR Lun
    );

UCHAR
VMDeviceDereferenceAddress(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR Bus,
    _In_ UCHAR Target,
    _In_ UCHAR Lun
    );

UCHAR 
VMDeviceFindDeviceByAddress(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _In_ UCHAR TargetId,
    _In_ UCHAR LunId,
    _In_ VM_TYPE DeviceType,
    _Inout_ PVOID *Device
    );

//
// State change notification
//

ULONG
VMDeviceReportStateChange(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR Bus,
    _In_ UCHAR Target,
    _In_ UCHAR Lun,
    _In_ ULONG ChangedEntity
    );

//
// Physical device management routines
//

NTSTATUS
VMDeviceCreatePhysicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR TargetCreateDescriptor,
    _Inout_ PVIRTUAL_MINIPORT_TIERED_DEVICE Device
    );

NTSTATUS
VMDeviceDeletePhysicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_TIERED_DEVICE Device
    );

NTSTATUS
VMDeviceBuildPhysicalDeviceDetails(
    _In_ PVIRTUAL_MINIPORT_TIERED_DEVICE Device,
    _Inout_ PVIRTUAL_MINIPORT_TARGET_DEVICE_DETAILS DeviceDetails
    );

//
// Logical device APIs
//

NTSTATUS
VMDeviceCreateLogicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_TIERED_DEVICE PhysicalDevice,
    _Inout_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE LogicalDevice,
    _In_ PVIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR LunCreateDescriptor
    );

NTSTATUS
VMDeviceDeleteLogicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE LogicalDevice
    );

NTSTATUS
VMDeviceBuildLogicalDeviceDetails(
    _In_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE LogicalDevice,
    _Inout_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE_DETAILS DeviceDetails
    );

NTSTATUS
VMDeviceReadWriteLogicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE LogicalDevice,
    _In_ BOOLEAN Read,
    _Inout_ PVOID Buffer,
    _In_ ULONGLONG LogicalBlockNumber,
    _In_ ULONG BlockCount,
    _Inout_ PULONG TransferredBytes
    );

#endif //__VIRTUAL_MINIPORT_DEVICE_H_