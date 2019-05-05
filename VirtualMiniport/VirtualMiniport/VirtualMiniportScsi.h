/*++

Module Name:

    VirtualMiniportScsi.h

Date:

    3-Mar-2014

Abstract:

    Module contains the prototypes for routines handling SRB_FUNCTION_EXECUTE_SCSI

--*/

#ifndef __VIRTUAL_MINIPORT_SCSI_H_
#define __VIRTUAL_MINIPORT_SCSI_H_

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
VMSrbExecuteScsi(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    );

#endif //__VIRTUAL_MINIPORT_SCSI_H_