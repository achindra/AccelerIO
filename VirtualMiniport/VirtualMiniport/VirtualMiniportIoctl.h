/*++

Module Name:

    VirtualMiniportIoctl.h

Date:

    3-Mar-2014

Abstract:

    Module contains the prototypes of SRB_IO_CONTROL handling functions

--*/

#ifndef __VIRTUAL_MINIPORT_IOCTL_H_
#define __VIRTUAL_MINIPORT_IOCTL_H_

#include <wdm.h>
#include <ntstrsafe.h>
#include <storport.h>
#include <initguid.h>

#include <VirtualMiniportDefinitions.h>
#include <VirtualMiniportCommon.h>
#include <VirtualMiniportSupportRoutines.h>
#include <VirtualMiniportTrace.h>

//
// Forward declarations
//

BOOLEAN
VMSrbIoControl(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    );

#endif //__VIRTUAL_MINIPORT_IOCTL_H_