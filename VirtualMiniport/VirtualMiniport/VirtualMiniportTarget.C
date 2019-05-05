/*++

Module Name:

    VirtualMiniportTarget.C

Date:

    15-Feb-2014

Abstract:

    Module contains the routines for implementing the Target management

    All routines start with VM - for 'V'irtual 'M'iniport

    Device State transition

    Uninitialized->Initialized->Attached->Started->StopPending->Stopped->Detached->Uninitialized
                         |        |          |----------------------^       ^                     
                         |        |-----------------------------------------|
                         |------------------------------------------------------------^

--*/

#include <VirtualMiniportTarget.h>
#include <VirtualMiniportDevice.h>

//
// WPP based event trace
//

#include <VirtualMiniportTarget.tmh>

//
// Forward declarations of private functions
//

NTSTATUS
VMTargetChangeState(
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _In_ VM_DEVICE_STATE NewState,
    _Inout_ PVM_DEVICE_STATE OldState,
    _In_ BOOLEAN LockAcquired
    );

NTSTATUS
VMTargetQueryState(
    _In_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVM_DEVICE_STATE State,
    _In_ BOOLEAN LockAcquired
    );


//
// Define the attributes of functions; declarations are in module
// specific header
//

//
// These are paged as we are called from the passive level
//

#pragma alloc_text(PAGED, VMTargetCreateInitialize)
#pragma alloc_text(PAGED, VMTargetDeleteUnInitialize)
#pragma alloc_text(PAGED, VMTargetAttach)
#pragma alloc_text(PAGED, VMTargetDetach)
#pragma alloc_text(PAGED, VMTargetStart)
#pragma alloc_text(PAGED, VMTargetStop)
#pragma alloc_text(PAGED, VMTargetQueryById)

#pragma alloc_text(PAGED, VMTargetChangeState)
#pragma alloc_text(PAGED, VMTargetQueryState)

//
// Device routines
//

//
// Target management routines
//

NTSTATUS
VMTargetChangeState(
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _In_ VM_DEVICE_STATE NewState,
    _Inout_ PVM_DEVICE_STATE OldState,
    _In_ BOOLEAN LockAcquired
    )

/*++

Routine Description:

    Udpates the state of the Target to desired state if the situation permits

Arguments:
    
    Target - Target whose state needs to be changed

    NewState - Destination state

    OldState - pointer that receives the old state

    LockAcquired - Tells if we need to acquire the lock

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL

--*/

{
    NTSTATUS Status;

    Status = STATUS_UNSUCCESSFUL;

    UNREFERENCED_PARAMETER(NewState);
    UNREFERENCED_PARAMETER(OldState);

    if ( Target == NULL || OldState == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( LockAcquired == TRUE || VMLockAcquireExclusive(&Target->TargetLock) == TRUE ) {
        
        *OldState = Target->State;

        switch ( Target->State ) {
        case VMDeviceUninitialized:
            switch ( NewState ) {
            case VMDeviceUninitialized:
            case VMDeviceInitialized:
                Target->State = NewState;
                Status = STATUS_SUCCESS;
                break;
            default:
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
            break;

        case VMDeviceInitialized:
            switch ( NewState ) {
            case VMDeviceUninitialized:
            case VMDeviceInitialized:
            case VMDeviceAttached:
                Target->State = NewState;
                Status = STATUS_SUCCESS;
                break;
            default:
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
            break;

        case VMDeviceAttached:
            switch ( NewState ) {
            case VMDeviceAttached:
            case VMDeviceDetached:
            case VMDeviceStarted:
                Target->State = NewState;
                Status = STATUS_SUCCESS;
                break;
            default:
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
            break;

        case VMDeviceStarted:
            switch ( NewState ) {
            case VMDeviceStarted:
            case VMDeviceStopPending:
            case VMDeviceStopped:
                Target->State = NewState;
                Status = STATUS_SUCCESS;
                break;
            default:
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
            break;

        case VMDeviceStopPending:
            switch ( NewState ) {
            case VMDeviceStopPending:
            case VMDeviceStopped:
                Target->State = NewState;
                Status = STATUS_SUCCESS;
                break;
            default:
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
            break;

        case VMDeviceStopped:
            switch ( NewState ) {
            case VMDeviceStopped:
            case VMDeviceDetached:
                Target->State = NewState;
                Status = STATUS_SUCCESS;
                break;
            default:
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
            break;

        case VMDeviceDetached:
            switch ( NewState ) {
            case VMDeviceDetached:
            case VMDeviceUninitialized:
                Target->State = NewState;
                Status = STATUS_SUCCESS;
                break;
            default:
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
            break;

        default:
            Status = STATUS_UNSUCCESSFUL;
            break;
        }

        if ( LockAcquired == FALSE ) {
            VMLockReleaseExclusive(&(Target->TargetLock));
        }
    }

Cleanup:
    return(Status);
}

NTSTATUS
VMTargetQueryState(
    _In_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVM_DEVICE_STATE State,
    _In_ BOOLEAN LockAcquired
    )

/*++

Routine Description:

    Queries the Target state

Arguments:

    Target - Target whose state needs to be queried

    State - Pointer to variable that receives the current state

    LockAcquired - Tells if we need to acquire the lock

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL

--*/

{
    NTSTATUS Status;

    Status = STATUS_UNSUCCESSFUL;

    if ( Target == NULL || State == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( LockAcquired == TRUE || VMLockAcquireExclusive(&(Target->TargetLock)) == TRUE ) {
    
        *State = Target->State;

        if ( LockAcquired == FALSE ) {
            VMLockReleaseExclusive(&(Target->TargetLock));
        }
    }

    Status = STATUS_SUCCESS;

Cleanup:
    return(Status);
}

NTSTATUS
VMTargetCreateInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR TargetCreateDescriptor,
    _Inout_ PVIRTUAL_MINIPORT_TARGET *Target
    )

/*++

Routine Description:

    Allocates and Initializes the Target.

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this target will
                       reside on.

    TargetCreateDescriptor - configuration details of target

    Target - pointer that receives the target

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL
    STATUS_INSUFFICIENT_RESOURCES
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    ULONG StorStatus;
    PVIRTUAL_MINIPORT_TARGET NewTarget;
    ULONG Index;
    BOOLEAN DeviceCreated;
    BOOLEAN LockInitialized;

    Status = STATUS_UNSUCCESSFUL;
    NewTarget = NULL;
    DeviceCreated = FALSE;
    LockInitialized = FALSE;

    if ( AdapterExtension == NULL || TargetCreateDescriptor == NULL || Target == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    *Target = NULL;

    StorStatus = StorPortAllocatePool(AdapterExtension,
                                      sizeof(VIRTUAL_MINIPORT_TARGET),
                                      VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                      &NewTarget);

    if ( StorStatus == STOR_STATUS_SUCCESS ) {
        RtlZeroMemory(NewTarget,
                      sizeof(VIRTUAL_MINIPORT_TARGET));

        NewTarget->State = VMDeviceUninitialized;
        NewTarget->Type = VMTypeTarget;
        NewTarget->Signature = VIRTUAL_MINIPORT_SIGNATURE_TARGET;
        NewTarget->MaxLunCount = AdapterExtension->DeviceExtension->Configuration.LunsPerTarget;
        NewTarget->Bus = NULL;
        NewTarget->LunCount = 0;
        StorStatus = StorPortAllocatePool(AdapterExtension,
                                          (sizeof(VIRTUAL_MINIPORT_LUN) *NewTarget->MaxLunCount),
                                          VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                          (PVOID) &(NewTarget->Luns));
        if ( StorStatus == STOR_STATUS_SUCCESS ) {
            for ( Index = 0; Index < NewTarget->MaxLunCount; Index++ ) {
                NewTarget->Luns [Index] = VIRTUAL_MINIPORT_INVALID_POINTER;
            }

            InitializeListHead(&NewTarget->List);
            Status = VMRtlCreateGUID(&NewTarget->UniqueId);

            //
            // Initialize the device
            //
            Status = VMDeviceCreatePhysicalDevice(AdapterExtension,
                                                  TargetCreateDescriptor,
                                                  &(NewTarget->Device));
            if ( !NT_SUCCESS(Status) ) {
                goto Cleanup;
            } 

            DeviceCreated = TRUE;
            //
            // Keep this at the end
            //
            Status = VMLockInitialize(&(NewTarget->TargetLock),
                                      LockTypeExecutiveResource);

            if ( NT_SUCCESS(Status) ) {
                LockInitialized = TRUE;
                NewTarget->State = VMDeviceInitialized;
                Status = STATUS_SUCCESS;
                *Target = NewTarget;
                VMTrace(TRACE_LEVEL_INFORMATION, 
                        VM_TRACE_TARGET,
                        "[%s]:AdapterExtension:%p, Target:%p Created",
                        __FUNCTION__,
                        AdapterExtension,
                        Target);
            }
        }
    } else {

        //
        // Convert everyother STOR status to insufficient resources.
        // This is NOT right, but for now we will do this.
        //
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

Cleanup:

    if ( !NT_SUCCESS(Status) ) {

        if ( NewTarget != NULL ) {

            if ( LockInitialized == TRUE ) {
                VMLockUnInitialize(&(NewTarget->TargetLock));
            }
            if ( DeviceCreated == TRUE ) {
                VMDeviceDeletePhysicalDevice(AdapterExtension,
                                             &(NewTarget->Device));
            }
            if ( NewTarget->Luns != NULL ) {
                StorPortFreePool(AdapterExtension,
                                 NewTarget->Luns);
            }
            StorPortFreePool(AdapterExtension,
                             NewTarget);
        }

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_TARGET,
                "[%s]:AdapterExtension:%p, Failed to create Target, Status:%!STATUS!",
                __FUNCTION__,
                AdapterExtension,
                Status);
    }

    return(Status);
}

NTSTATUS
VMTargetDeleteUnInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target
    )

/*++

Routine Description:

    Deletes and UnInitializes the Target.

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this target
                       resides on.

    Target - pointer that needs to be cleaned up

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    PVIRTUAL_MINIPORT_BUS Bus;
    VM_DEVICE_STATE State;

    Status = STATUS_UNSUCCESSFUL;
    Bus = NULL;
    State = VMDeviceStateUnknown;

    if ( AdapterExtension == NULL || Target == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    //
    // Ensure the Target state is safe to delete
    //

    if ( VMTargetQueryState(Target, &State, FALSE) != STATUS_SUCCESS ||
        !(State == VMDeviceInitialized || State == VMDeviceDetached )) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;    
    }

    //
    // Start the cleanup of this target
    //

    Status = VMDeviceDeletePhysicalDevice(AdapterExtension,
                                          &(Target->Device));
    if ( !NT_SUCCESS(Status) ) {

        //
        // Despite this error, we go with cleaning up rest of Target
        //

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_TARGET,
                "[%s]:Target:%p, Failed to delete the device, Status:%!STATUS!",
                __FUNCTION__,
                Target,
                Status);
    }

    if ( Target->Luns != NULL ) {
        StorPortFreePool(AdapterExtension,
                         Target->Luns);
    }

    VMLockUnInitialize(&(Target->TargetLock));
    StorPortFreePool(AdapterExtension,
                     Target);
    Status = STATUS_SUCCESS;

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_TARGET,
            "[%s]:AdapterExtension:%p, Target:%p, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Target,
            Status);
    return(Status);
}

NTSTATUS
VMTargetAttach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PUCHAR TargetId
    )

/*++

Routine Description:

    Attached the Target to the bus

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this target
                       resides on.

    Bus - Bus to which this target needs to be attached

    Target - Target that needs to be attached

    TargetId - Target Id where Target was attached

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    UCHAR Index;
    VM_DEVICE_STATE TargetState;

    Status = STATUS_UNSUCCESSFUL;
    TargetState = VMDeviceStateUnknown;

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL || TargetId == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    *TargetId = 0;

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {
            if ( VMLockAcquireExclusive(&(Target->TargetLock)) == TRUE ) {

                Status = STATUS_INVALID_DEVICE_STATE;
                if ( VMTargetQueryState(Target, &TargetState, TRUE) == STATUS_SUCCESS && TargetState == VMDeviceInitialized ) {

                    Status = STATUS_QUOTA_EXCEEDED;
                    for ( Index = 0; Index < Bus->MaxTargetCount; Index++ ) {
                        if ( Bus->Targets [Index] == VIRTUAL_MINIPORT_INVALID_POINTER ) {

                            Target->TargetId = Index;
                            Target->Bus = Bus;

                            Bus->TargetCount++;
                            Bus->Targets [Index] = Target;

                            //
                            // Below state transition will never fail, as we validated the state above
                            //
                            VMTargetChangeState(Target,
                                                VMDeviceAttached,
                                                &TargetState,
                                                TRUE);

                            *TargetId = Index;
                            Status = STATUS_SUCCESS;
                            break;
                        }
                    }
                }
                VMLockReleaseExclusive(&(Target->TargetLock));
            }
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_TARGET,
            "[%s]:AdapterExtension:%p, Bus:%p, Target:%p [%d], attach status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Bus,
            Target,
            (Status == STATUS_SUCCESS)?Target->TargetId:-1,
            Status);

    return(Status);
}

NTSTATUS
VMTargetDetach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target
    )

/*++

Routine Description:

    Detach the target from the bus

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this target
                       resides on.

    Bus - Bus from which this target needs to be detached

    Target - Target that needs to be detached

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    VM_DEVICE_STATE State;

    Status = STATUS_UNSUCCESSFUL;
    State = VMDeviceStateUnknown;

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {
            if ( VMLockAcquireExclusive(&(Target->TargetLock)) == TRUE ) {
                
                Status = STATUS_INVALID_DEVICE_STATE;
                if ( Target->Bus != Bus ) {

                    Status = STATUS_INVALID_PARAMETER;
                } else if ( VMTargetQueryState(Target, &State, TRUE) == STATUS_SUCCESS &&
                            (State == VMDeviceAttached || State == VMDeviceStopped) ) {

                    Bus->Targets [Target->TargetId] = VIRTUAL_MINIPORT_INVALID_POINTER;
                    Bus->TargetCount--;

                    Target->Bus = VIRTUAL_MINIPORT_INVALID_POINTER;
                    Status = VMTargetChangeState(Target,
                                                 VMDeviceDetached,
                                                 &State,
                                                 TRUE);
                } 
                VMLockReleaseExclusive(&(Target->TargetLock));
            }
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_TARGET,
            "[%s]:AdapterExtension:%p, Bus:%p, Target:%p, detach status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Bus,
            Target,
            Status);
    return(Status);
}

NTSTATUS
VMTargetStart(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target
    )

/*++

Routine Description:

    Starts the Target

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this target
                       resides on.

    Bus - Bus on which this target resides

    Target - Target that needs to be started

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    VM_DEVICE_STATE State;

    Status = STATUS_UNSUCCESSFUL;
    State = VMDeviceStateUnknown;

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {
            if ( VMLockAcquireExclusive(&(Target->TargetLock)) == TRUE ) {

                Status = STATUS_INVALID_DEVICE_STATE;
                if ( Target->Bus != Bus ) {
                    Status = STATUS_INVALID_PARAMETER;
                } else if ( VMTargetQueryState(Target, &State, TRUE) == STATUS_SUCCESS && State == VMDeviceAttached ) {
                    Status = VMTargetChangeState(Target, VMDeviceStarted, &State, TRUE);
                }
                VMLockReleaseExclusive(&(Target->TargetLock));
            }
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_TARGET,
            "[%s]:AdapterExtension:%p, Bus:%p, Traget:%p, OldState:%!VMDEVICESTATE!, NewState:%!VMDEVICESTATE!, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Bus,
            Target,
            State,
            (Status == STATUS_SUCCESS) ? VMDeviceStarted : State,
            Status);

    return(Status);
}

NTSTATUS
VMTargetStop(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target
    )
/*++

Routine Description:

    Stops the target

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this target
                       resides on.

    Bus - Bus on which this target resides

    Target - Target that needs to be stopped

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    VM_DEVICE_STATE OldState, NewState;
    LARGE_INTEGER DelayFiveSeconds;

    Status = STATUS_UNSUCCESSFUL;
    OldState = VMDeviceStateUnknown;
    NewState = VMDeviceStateUnknown;
    DelayFiveSeconds.QuadPart = -5000LL * 1000LL * 10LL; // 5 seconds

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    do {
        if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {

            if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {

                Status = VMTargetChangeState(Target,
                                             VMDeviceStopped,
                                             &OldState,
                                             FALSE);
                VMTargetQueryState(Target,
                                   &NewState,
                                   FALSE);
                VMTrace(TRACE_LEVEL_INFORMATION,
                        VM_TRACE_TARGET,
                        "[%s]:AdapterExtension:%p, Bus:%p, Target:%p, OldState:%!VMDEVICESTATE!, NewState:%!VMDEVICESTATE!, Status:%!STATUS!",
                        __FUNCTION__,
                        AdapterExtension,
                        Bus,
                        Target,
                        OldState,
                        NewState,
                        Status);
                VMLockReleaseExclusive(&(Bus->BusLock));
            }
            VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
        }

        if ( NewState == VMDeviceStopPending ) {

            VMRtlDelayExecution(&DelayFiveSeconds);
        }
    } while ( Status == STATUS_PENDING && NewState == VMDeviceStopPending );

Cleanup:
    return(Status);
}

NTSTATUS
VMTargetQueryById(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _In_ UCHAR TargetId,
    _Inout_ PVIRTUAL_MINIPORT_TARGET *Target,
    _In_ BOOLEAN Reference
    )

/*++

Routine Description:

    Find the Traget by Traget ID

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this target
                       resides on.

    Bus - Bus on which this target resides

    TargetId - TargetId of the target that we need to find

    Traget - pointer that receives the target

    Reference - Reserved for now

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    STATUS_UNSUCCESSFUL
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;

    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL || Reference == TRUE ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    *Target = NULL;

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {

            Status = STATUS_DEVICE_DOES_NOT_EXIST;
            if ( TargetId < Bus->MaxTargetCount ) {

                Status = STATUS_DEVICE_NOT_CONNECTED;
                if ( Bus->Targets [TargetId] != VIRTUAL_MINIPORT_INVALID_POINTER ) {
                    *Target = Bus->Targets [TargetId];
                    //
                    // Take reference here when we implement it
                    //
                    Status = STATUS_SUCCESS;
                }            
            }
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }
Cleanup:
    return(Status);
}