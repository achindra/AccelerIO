/*++

Module Name:

    VirtualMiniportTarget.h

Date:

    15-Feb-2014

Abstract:

    Module contains the prototypes of Target implementation

--*/

#ifndef __VIRTUAL_MINIPORT_TARGET_H_
#define __VIRTUAL_MINIPORT_TARGET_H_

#include <wdm.h>
#include <ntstrsafe.h>
#include <storport.h>

#include <VirtualMiniportDefinitions.h>
#include <VirtualMiniportCommon.h>
#include <VirtualMiniportTrace.h>

NTSTATUS
VMTargetCreateInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR TargetCreateDescriptor,
    _Inout_ PVIRTUAL_MINIPORT_TARGET *Target
    );

NTSTATUS
VMTargetDeleteUnInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target
    );

NTSTATUS
VMTargetAttach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PUCHAR TargetId
    );

NTSTATUS
VMTargetDetach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target
    );

NTSTATUS
VMTargetStart(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target
    );

NTSTATUS
VMTargetStop(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target
    );

NTSTATUS
VMTargetQueryById(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _In_ UCHAR TargetId,
    _Inout_ PVIRTUAL_MINIPORT_TARGET *Target,
    _In_ BOOLEAN Reference
    );

#endif //__VIRTUAL_MINIPORT_TARGET_H_