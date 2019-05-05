/*++

Module Name:

    VirtualMiniportSupportRoutines.C

Date:

    5-Feb-2014

Abstract:

    Module contains the support functions
    All routines start with VM - for 'V'irtual 'M'iniport

--*/

//
// Headers
//

#include <VirtualMiniportSupportRoutines.h>

//
// WPP based event trace
//

#include <VirtualMiniportSupportRoutines.tmh>

//
// Forward declarations for private routines
//

//
// Routine attributes
//

#pragma alloc_text(NONPAGED, VMRtlCreateGUID)
#pragma alloc_text(NONPAGED, VMRtlDelayExecution)
#pragma alloc_text(NONPAGED, VMRtlBugcheck)
#pragma alloc_text(NONPAGED, VMRtlDebugBreak)

//
// Driver specific routines
//

NTSTATUS
VMRtlCreateGUID(
    _Inout_ PGUID Guid
    )

/*++

Routine Description:

    Create a GUID/UUID in the caller-allocated space

Arguments:

    Guid - caller allocated GUID space

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    Other NTSTATUS from callee

--*/

{

    NTSTATUS Status;

    if( Guid == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    Status = ExUuidCreate(Guid);

Cleanup:
    return (Status);
}

VOID
VMRtlDelayExecution(
    _In_ PLARGE_INTEGER Timeout
    )

/*++

Routine Description:

    Implements the sleep of given timeout

Arguments:

    Timeout - Sleep time

Environment:

    IRQL - <= APC_LEVEL

Return Value:

    None
    NOTE: We assume that we will never fail KeDelayExecutionThread
          and parameter passed will be sane.

--*/

{
    KeDelayExecutionThread(KernelMode,
        FALSE,
        Timeout);
}

VOID
VMRtlBugcheck(
    _In_ ULONG BugcheckCode,
    _In_ ULONG_PTR Parameter1,
    _In_ ULONG_PTR Parameter2,
    _In_ ULONG_PTR Parameter3,
    _In_ ULONG_PTR Parameter4
    )

/*++

Routine Description:

    Bugcheck the system

Arguments:

    BugcheckCode - Bugcheck code

Environment:

    IRQL - Any level

Return Value:

    None

--*/

{
    KeBugCheckEx(BugcheckCode,
                 Parameter1,
                 Parameter2,
                 Parameter3,
                 Parameter4
                 );
}

VOID
VMRtlDebugBreak(VOID
                )

/*++

Routine Description:

    Issue a breakpoint if debugger is configured

Arguments:

    VOID

Environment:

    IRQL - Any level

Return Value:

    None

--*/

{
    if( KdRefreshDebuggerNotPresent() == FALSE ) {
        DbgBreakPoint();
    }
}