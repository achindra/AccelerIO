/*++

Module Name:

    VirtualMiniportPnp.h

Date:

    7-Mar-2014

Abstract:

    Module contains the prototypes of Pnp handling

--*/

#ifndef __VIRTUAL_MINIPORT_PNP_H_
#define __VIRTUAL_MINIPORT_PNP_H_

#include <wdm.h>
#include <storport.h>

#include <VirtualMiniportDefinitions.h>
#include <VirtualMiniportCommon.h>
#include <VirtualMiniportSupportRoutines.h>
#include <VirtualMiniportTrace.h>

//
// Forward declarations
//

BOOLEAN
VMSrbPnp(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PSCSI_PNP_REQUEST_BLOCK Srb
    );


#endif //__VIRTUAL_MINIPORT_PNP_H_