/*++

Module Name:

    VirtualMiniportWrapper.C

Date:

    8-Feb-2014

Abstract:

    Module contains the wrapper functions
    All routines start with VM - for 'V'irtual 'M'iniport

    NOTE: Validity of the locks are checked without any synchronization. There is
    a possible window where the lock may become invalid after validatity is checked.

--*/

//
// This will wrap the base functionality
//

#include <VirtualMiniportWrapper.h>

//
// WPP based event trace
//

#include <VirtualMiniportWrapper.tmh>

//
// Private routines
//

static
BOOLEAN
VMLockIsValid(
    _In_ PVM_LOCK VmLock,
    _In_ BOOLEAN MinimalCheck
    );

//
// Memory allocation wrappers
//

//
// Set the paging attributes for the wrappers
//

#pragma alloc_text(NONPAGED, VMLockIsValid)
#pragma alloc_text(NONPAGED, VMLockInitialize)
#pragma alloc_text(NONPAGED, VMLockUnInitialize)
#pragma alloc_text(NONPAGED, VMLockAcquireExclusive)
#pragma alloc_text(NONPAGED, VMLockReleaseExclusive)
#pragma alloc_text(NONPAGED, VMLockAcquireShared)
#pragma alloc_text(NONPAGED, VMLockReleaseShared)

_Use_decl_annotations_
static
BOOLEAN
VMLockIsValid(
    _In_ PVM_LOCK VmLock,
    _In_ BOOLEAN MinimalCheck
    )

/*++

Routine Description:

    Verifies validity of the lock

Arguments:

    VmLock - Pointer to VM_LOCK

    MinimalCheck - Check for NULL pointer

Environment:

    IRQL - any level

Return Value:

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;

    Status = FALSE;

    if( VmLock == NULL ) {
        Status = FALSE;
        goto Cleanup;
    }

    if ( !MinimalCheck && !(VmLock->LockType >= LockTypeMin && VmLock->LockType <= LockTypeMax) ) {
        Status = FALSE;
        goto Cleanup;
    }

    Status = TRUE;

Cleanup:
    return(Status);

}

_Use_decl_annotations_
NTSTATUS
VMLockInitialize(
    _In_ PVM_LOCK VmLock,
    _In_ VM_LOCK_TYPE VmLockType
    )

/*++

Routine Description:

    Initializes the VmLock of VmLockType.

Arguments:

    VmLock - Pointer to VM_LOCK
    VmLockType - VM_LOCK_TYPE enum for the type of lock to be initialized

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL
    STATUS_SUCCESS
    Any other NTSTATUS

--*/

{
    NTSTATUS Status;

    Status = STATUS_UNSUCCESSFUL;

    if( VMLockIsValid(VmLock, TRUE) == FALSE ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    switch( VmLockType ) {
    
    //
    //case LockTypeDefault:
    //
    case LockTypeSpinlock:
        KeInitializeSpinLock(&(VmLock->Lock.SpinLock.SpinLock));
        Status = STATUS_SUCCESS;
        break;

    case LockTypeExecutiveResource:
        Status = ExInitializeResourceLite(&(VmLock->Lock.ExecutiveResourceLock));
        VmLock->ReturnAddress = NULL;
        break;

    default:
        Status = STATUS_UNSUCCESSFUL;
        break;
    }

Cleanup:

    if( NT_SUCCESS(Status) ) {
        VmLock->LockType = VmLockType;
    } else {
        VmLock->LockType = LockTypeInvalid;
    }

    return(Status);
}

_Use_decl_annotations_
NTSTATUS
VMLockUnInitialize(
    _In_ PVM_LOCK VmLock
    )

/*++

Routine Description:

    Un-Initializes the VmLock

Arguments:

    VmLock - Pointer to VM_LOCK

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL
    STATUS_SUCCESS
    Any other NTSTATUS

--*/

{
    NTSTATUS Status;

    Status = STATUS_UNSUCCESSFUL;

    if( VMLockIsValid(VmLock, FALSE) == FALSE ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    switch( VmLock->LockType ) {
        //
        // LockTypeDefault
        //
    case LockTypeSpinlock:
        //
        // Spinlocks does have a way to uninitialize
        //
        Status = STATUS_SUCCESS;
        break;

    case LockTypeExecutiveResource:
        Status = ExDeleteResourceLite(&(VmLock->Lock.ExecutiveResourceLock));
        break;

    default:
        Status = STATUS_UNSUCCESSFUL;
        break;
    }

Cleanup:

    if( NT_SUCCESS(Status) ) {
        VmLock->LockType = LockTypeInvalid;
    }

    return(Status);
}

_Use_decl_annotations_
BOOLEAN
VMLockAcquireExclusive(
    _In_ PVM_LOCK VmLock
    )

/*++

Routine Description:

    Acquires the lock in exclusive mode; Exclusiveness is
    lock type dependent.

Arguments:

    VmLock - Pointer to VM_LOCK

Environment:

    IRQL - Lock dependent

Return Value:

    TRUE
    FALSE

--*/
{
    BOOLEAN Status;

    Status = FALSE;

    if( VMLockIsValid(VmLock, FALSE) == FALSE ) {
        Status = FALSE;
        goto Cleanup;
    }

    switch( VmLock->LockType ) {
        //
        // LockTypeDefault
        //
    case LockTypeSpinlock:
        KeAcquireSpinLock(&(VmLock->Lock.SpinLock.SpinLock),
                          &(VmLock->Lock.SpinLock.OldIrql));
        Status = TRUE;
        break;

    case LockTypeExecutiveResource:
        Status = ExAcquireResourceExclusiveLite(&(VmLock->Lock.ExecutiveResourceLock),
                                                TRUE);
        VmLock->ReturnAddress = _ReturnAddress();
        break;

    default:
        Status = FALSE;
        break;
    }

Cleanup:

    return(Status);
}

_Use_decl_annotations_
BOOLEAN
VMLockReleaseExclusive(
    _In_ PVM_LOCK VmLock
)
/*++

Routine Description:

    Release the lock from exclusive mode; Exclusiveness is
    lock type dependent.

Arguments:

    VmLock - Pointer to VM_LOCK

Environment:

    IRQL - Lock dependent

Return Value:

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;

    Status = FALSE;

    if( VMLockIsValid(VmLock, FALSE) == FALSE ) {
        Status = FALSE;
        goto Cleanup;
    }

    switch( VmLock->LockType ) {
        //
        // LockTypeDefault
        //
    case LockTypeSpinlock:
        KeReleaseSpinLock(&(VmLock->Lock.SpinLock.SpinLock),
                          VmLock->Lock.SpinLock.OldIrql);
        Status = TRUE;
        break;

    case LockTypeExecutiveResource:
        VmLock->ReturnAddress = NULL;
        ExReleaseResourceLite(&(VmLock->Lock.ExecutiveResourceLock));
        Status = TRUE;
        break;

    default:
        Status = FALSE;
        break;
    }

Cleanup:

    return(Status);
}

_Use_decl_annotations_
BOOLEAN
VMLockAcquireShared(
    _In_ PVM_LOCK VmLock
    )

/*++

Routine Description:

    Acquires the lock in shared mode; Shared-mode is
    lock type dependent.

Arguments:

    VmLock - Pointer to VM_LOCK

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;

    Status = FALSE;

    if( VMLockIsValid(VmLock, FALSE) == FALSE ) {
        Status = FALSE;
        goto Cleanup;
    }

    switch( VmLock->LockType ) {
        //
        // LockTypeDefault
        //
    case LockTypeSpinlock:
        KeAcquireSpinLock(&(VmLock->Lock.SpinLock.SpinLock),
                          &(VmLock->Lock.SpinLock.OldIrql));
        Status = TRUE;
        break;

    case LockTypeExecutiveResource:
        Status = ExAcquireResourceSharedLite(&(VmLock->Lock.ExecutiveResourceLock),
                                             TRUE);
        VmLock->ReturnAddress = _ReturnAddress();
        break;

    default:
        Status = FALSE;
        break;
    }

Cleanup:

    return(Status);
}

BOOLEAN
VMLockReleaseShared(
    _In_ PVM_LOCK VmLock
    )

/*++

Routine Description:

    Release the lock from shared mode; Shared-mode is
    lock type dependent.

Arguments:

    VmLock - Pointer to VM_LOCK

Environment:

    IRQL - Lock dependent

Return Value:

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;

    Status = FALSE;

    if( VMLockIsValid(VmLock, FALSE) == FALSE ) {
        Status = FALSE;
        goto Cleanup;
    }

    switch( VmLock->LockType ) {
        //
        // LockTypeDefault
        //
    case LockTypeSpinlock:
        KeReleaseSpinLock(&(VmLock->Lock.SpinLock.SpinLock),
                          VmLock->Lock.SpinLock.OldIrql);
        Status = TRUE;
        break;

    case LockTypeExecutiveResource:
        VmLock->ReturnAddress = NULL;
        ExReleaseResourceLite(&(VmLock->Lock.ExecutiveResourceLock));
        Status = TRUE;
        break;

    default:
        Status = FALSE;
        break;
    }

Cleanup:

    return(Status);
}
