/*++

Module Name:

    VirtualMiniportSupportRoutines.h

Date:

    5-Feb-2014

Abstract:

    Module contains the prototypes of support routines

--*/

#ifndef __VIRTUAL_MINIPORT_SUPPORT_ROUTINES_H_
#define __VIRTUAL_MINIPORT_SUPPORT_ROUTINES_H_

#include <ntddk.h>
#include <wdm.h>
#include <VirtualMiniportTrace.h>

//
// Allocation tags
//

#define VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG 'VTag'

/*++

    Some basic definitions

--*/

#define VIRTUAL_MINIPORT_INVALID_POINTER NULL

NTSTATUS
VMRtlCreateGUID(
    _Inout_ PGUID Guid
    );

VOID
VMRtlDelayExecution(
    _In_ PLARGE_INTEGER Timeout
    );

VOID
VMRtlBugcheck(
    _In_ ULONG BugcheckCode,
    _In_ ULONG_PTR Parameter1,
    _In_ ULONG_PTR Parameter2,
    _In_ ULONG_PTR Parameter3,
    _In_ ULONG_PTR Parameter4
    );

VOID
VMRtlDebugBreak();

#endif // __VIRTUAL_MINIPORT_SUPPORT_ROUTINES_H_

