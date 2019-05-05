/*++

Module Name:

    VirtualMiniportConfig.h

Date:

    5-Feb-2014

Abstract:

    Module contains the prototypes of config routines

--*/

#ifndef __VIRTUAL_MINIPORT_CONFIG_H_
#define __VIRTUAL_MINIPORT_CONFIG_H_

#include <wdm.h>
#include <ntstrsafe.h>
#include <storport.h>

#include <VirtualMiniportDefinitions.h>
#include <VirtualMiniportTrace.h>

NTSTATUS
VMDriverLoadConfig (
    _In_ PUNICODE_STRING RegistryPath,
    _Inout_ PVIRTUAL_MINIPORT_CONFIGURATION Configuration
    );

NTSTATUS
VMDriverUnLoadConfig(
    _Inout_ PVIRTUAL_MINIPORT_CONFIGURATION Configuration
    );

#endif //__VIRTUAL_MINIPORT_CONFIG_H_