/*++

Module Name:

    VirtualMiniportWrapper.h

Date:

    8-Feb-2014

Abstract:

    Module contains the wrapper types and routine prototypes

--*/

#ifndef __VIRTUAL_MINIPORT_WRAPPER_H_
#define __VIRTUAL_MINIPORT_WRAPPER_H_

#include<wdm.h>
#include <VirtualMiniportTrace.h>

typedef enum _VM_LOCK_TYPE {
    LockTypeUnknown,
    LockTypeMin = 1,
    LockTypeSpinlock = LockTypeMin,
    LockTypeDefault = LockTypeSpinlock,
    LockTypeExecutiveResource,
    LockTypeMax = LockTypeExecutiveResource, // Should be equal to last lock type
    LockTypeInvalid = -1
} VM_LOCK_TYPE;

//
// I would want the implementation of this lock to be such that
//  * This wraps all types of locks (Spinlock, ERESOURCE etc...)
//  * Lock header will indicate the type of lock
//  * Initialization of the lock will specify the type of lock
//  * Lock acquisition/release/destruction will look at the header
//    to use right semantics
//

typedef struct _VM_LOCK {
    VM_LOCK_TYPE LockType;
    union {
        struct {
            KIRQL OldIrql;
            KSPIN_LOCK SpinLock;
        }SpinLock;
        ERESOURCE ExecutiveResourceLock;
    }Lock;
    PVOID ReturnAddress;
}VM_LOCK, *PVM_LOCK;


NTSTATUS
VMLockInitialize(
    _In_ PVM_LOCK VmLock,
    _In_ VM_LOCK_TYPE VmLockType
    );

NTSTATUS
VMLockUnInitialize(
    _In_ PVM_LOCK VmLock
    );

BOOLEAN
VMLockAcquireExclusive(
    _In_ PVM_LOCK VmLock
    );

BOOLEAN
VMLockReleaseExclusive(
    _In_ PVM_LOCK VmLock
    );

BOOLEAN
VMLockAcquireShared(
    _In_ PVM_LOCK VmLock
    );

BOOLEAN
VMLockReleaseShared(
    _In_ PVM_LOCK VmLock
    );

#endif //__VIRTUAL_MINIPORT_WRAPPER_H_