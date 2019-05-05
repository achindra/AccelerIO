/*++

Module Name:

    VirtualMiniportLun.h

Date:

    15-Feb-2014

Abstract:

    Module contains the prototypes of Lun implementation

--*/

#ifndef __VIRTUAL_MINIPORT_LUN_H_
#define __VIRTUAL_MINIPORT_LUN_H_

#include <wdm.h>
#include <ntstrsafe.h>
#include <storport.h>

#include <VirtualMiniportDefinitions.h>
#include <VirtualMiniportCommon.h>
#include <VirtualMiniportTrace.h>


NTSTATUS
VMLunCreateInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR LunCreateDescriptor,
    _Inout_ PVIRTUAL_MINIPORT_LUN *Lun
    );

NTSTATUS
VMLunDeleteUnInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun
    );

NTSTATUS
VMLunAttach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun,
    _In_ PVIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR LunCreateDescriptor,
    _Inout_ PUCHAR LunId
    );

NTSTATUS
VMLunDetach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun
    );

NTSTATUS
VMLunStart(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _In_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun
    );

NTSTATUS
VMLunStop(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun
    );

NTSTATUS
VMLunQueryById(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _In_ PVIRTUAL_MINIPORT_TARGET Target,
    _In_ UCHAR LunId,
    _Inout_ PVIRTUAL_MINIPORT_LUN *Lun,
    _In_ BOOLEAN Reference
    );

#endif //__VIRTUAL_MINIPORT_TARGET_H_