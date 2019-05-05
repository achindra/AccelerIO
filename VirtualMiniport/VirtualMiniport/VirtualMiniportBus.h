/*++

Module Name:

    VirtualMiniportBus.h

Date:

    9-Feb-2014

Abstract:

    Module contains the prototypes of Bus implementation

--*/

#ifndef __VIRTUAL_MINIPORT_BUS_H_
#define __VIRTUAL_MINIPORT_BUS_H_

#include <wdm.h>
#include <ntstrsafe.h>
#include <storport.h>

#include <VirtualMiniportDefinitions.h>
#include <VirtualMiniportTrace.h>

//
// Bus management routines
//

NTSTATUS
VMBusCreateInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS *Bus
    );

NTSTATUS
VMBusDeleteUninitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus
    );

NTSTATUS
VMBusAttach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PUCHAR BusId
    );

NTSTATUS
VMBusDetach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus
    );

NTSTATUS
VMBusStart(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus
    );

NTSTATUS
VMBusStop(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus
    );

NTSTATUS
VMBusQueryById(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _Inout_ PVIRTUAL_MINIPORT_BUS *Bus,
    _In_ BOOLEAN Reference
    );

#endif //__VIRTUAL_MINIPORT_BUS_H_