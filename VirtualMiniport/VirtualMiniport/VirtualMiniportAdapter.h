/*++

Module Name:

    VirtualMiniportAdapter.h

Date:

    28-Feb-2014

Abstract:

    Module contains the prototypes of adapter

--*/

#ifndef __VIRTUAL_MINIPORT_ADAPTER_H_
#define __VIRTUAL_MINIPORT_ADAPTER_H_

#include <wdm.h>
#include <storport.h>
#include <ntstrsafe.h>
#include <initguid.h>

#include <VirtualMiniportDefinitions.h>
#include <VirtualMiniportCommon.h>
#include <VirtualMiniportSupportRoutines.h>
#include <VirtualMiniportTrace.h>

NTSTATUS
VMAdapterInitialize (
    _In_ PVIRTUAL_MINIPORT_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVOID VMHwContext,
    _In_ PVOID BusInformation,
    _In_ PVOID LowerDevice,
    _In_ PCHAR ArgumentString
    );

NTSTATUS
VMAdapterUnInitialize (
    _In_ PVIRTUAL_MINIPORT_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension
    );

NTSTATUS
VMAdapterStart (
    _In_ PVIRTUAL_MINIPORT_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension
    );

NTSTATUS
VMAdapterStop (
    _In_ PVIRTUAL_MINIPORT_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ BOOLEAN Block
    );

#endif //__VIRTUAL_MINIPORT_ADAPTER_H_