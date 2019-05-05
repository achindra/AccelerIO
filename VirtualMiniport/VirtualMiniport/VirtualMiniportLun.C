/*++

Module Name:

    VirtualMiniportLun.C

Date:

    15-Feb-2014

Abstract:

    Module contains the routines for implementing the Lun management

    All routines start with VM - for 'V'irtual 'M'iniport

    Device State transition

    Uninitialized->Initialized->Attached->Started->StopPending->Stopped->Detached->Uninitialized
                         |        |          |----------------------^       ^                     
                         |        |-----------------------------------------|
                         |------------------------------------------------------------^

--*/

#include <VirtualMiniportLun.h>
#include <VirtualMiniportDevice.h>

//
// WPP based event trace
//

#include <VirtualMiniportLun.tmh>

//
// Forward declarations of private functions
//

NTSTATUS
VMLunChangeState(
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun,
    _In_ VM_DEVICE_STATE NewState,
    _Inout_ PVM_DEVICE_STATE OldState,
    _In_ BOOLEAN LockAcquired
    );

NTSTATUS
VMLunQueryState(
    _In_ PVIRTUAL_MINIPORT_LUN Lun,
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

#pragma alloc_text(PAGED, VMLunCreateInitialize)
#pragma alloc_text(PAGED, VMLunDeleteUnInitialize)
#pragma alloc_text(PAGED, VMLunAttach)
#pragma alloc_text(PAGED, VMLunDetach)
#pragma alloc_text(PAGED, VMLunStart)
#pragma alloc_text(PAGED, VMLunStop)

#pragma alloc_text(PAGED, VMLunChangeState)
#pragma alloc_text(PAGED, VMLunQueryState)

//
// Lun management routines
//

NTSTATUS
VMLunChangeState(
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun,
    _In_ VM_DEVICE_STATE NewState,
    _Inout_ PVM_DEVICE_STATE OldState,
    _In_ BOOLEAN LockAcquired
    )

/*++

Routine Description:

    Udpates the state of the Lun to desired state if the situation permits

Arguments:
    
    Lun - Lun whose state needs to be changed

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

    if ( Lun == NULL || OldState == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( LockAcquired == TRUE || VMLockAcquireExclusive(&Lun->LunLock) == TRUE ) {
        
        *OldState = Lun->State;

        switch ( Lun->State ) {
        case VMDeviceUninitialized:
            switch ( NewState ) {
            case VMDeviceUninitialized:
            case VMDeviceInitialized:
                Lun->State = NewState;
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
                Lun->State = NewState;
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
                Lun->State = NewState;
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
                Lun->State = NewState;
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
                Lun->State = NewState;
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
                Lun->State = NewState;
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
                Lun->State = NewState;
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
            VMLockReleaseExclusive(&(Lun->LunLock));
        }
    }

Cleanup:
    return(Status);
}

NTSTATUS
VMLunQueryState(
    _In_ PVIRTUAL_MINIPORT_LUN Lun,
    _Inout_ PVM_DEVICE_STATE State,
    _In_ BOOLEAN LockAcquired
    )

/*++

Routine Description:

    Queries the Target state

Arguments:

    Lun - Lun whose state needs to be queried

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

    if ( Lun == NULL || State == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( LockAcquired == TRUE || VMLockAcquireExclusive(&(Lun->LunLock)) == TRUE ) {
    
        *State = Lun->State;

        if ( LockAcquired == FALSE ) {
            VMLockReleaseExclusive(&(Lun->LunLock));
        }
    }

    Status = STATUS_SUCCESS;

Cleanup:
    return(Status);
}

NTSTATUS
VMLunCreateInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR LunCreateDescriptor,
    _Inout_ PVIRTUAL_MINIPORT_LUN *Lun
    )

/*++

Routine Description:

    Allocates and Initializes the Lun. Only part of LUN is created and initialized.
    Logical device is created and initialized only when we attach the lun to the
    target. This is important, as the association between Target and Lun is only
    established at attach.

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this lun will
                       reside on.

    LunCreateDescriptor - configuration details of lun

    Lun - pointer that receives the lun

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
    PVIRTUAL_MINIPORT_LUN NewLun;

    Status = STATUS_UNSUCCESSFUL;
    NewLun = NULL;

    if ( AdapterExtension == NULL || LunCreateDescriptor == NULL || Lun == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    *Lun = NULL;

    StorStatus = StorPortAllocatePool(AdapterExtension,
                                      sizeof(VIRTUAL_MINIPORT_LUN),
                                      VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                      &NewLun);
    if ( StorStatus == STOR_STATUS_SUCCESS ) {
        RtlZeroMemory(NewLun,
                      sizeof(VIRTUAL_MINIPORT_LUN));
        NewLun->State = VMDeviceUninitialized;
        NewLun->Signature = VIRTUAL_MINIPORT_SIGNATURE_LUN;
        NewLun->Type = VMTypeLun;       
        InitializeListHead(&(NewLun->List));
        NewLun->LunId = 0;
        NewLun->Target = NULL;

        Status = VMRtlCreateGUID(&(NewLun->UniqueId));

        //
        // Derfer Lun->Device created to attach time
        //
        NewLun->DeviceCreated = FALSE;

        Status = VMLockInitialize(&(NewLun->LunLock),
                                  LockTypeExecutiveResource);
        if ( NT_SUCCESS(Status) ) {
            *Lun = NewLun;
            NewLun->State = VMDeviceInitialized;
            Status = STATUS_SUCCESS;

            VMTrace(TRACE_LEVEL_INFORMATION,
                    VM_TRACE_LUN,
                    "[%s]:AdapterExtension:%p, Lun:%p, Created, logical device created:%!bool!",
                    __FUNCTION__,
                    AdapterExtension,
                    NewLun,
                    NewLun->DeviceCreated);
        }

    } else {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

Cleanup:

    if ( !NT_SUCCESS(Status) ) {
        if ( NewLun != NULL ) {
            NewLun->DeviceCreated = FALSE;
            StorPortFreePool(AdapterExtension, 
                             NewLun);
        }

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_LUN,
                "[%s]:AdapterExtension:%p, Failed to create Lun, Status:%!STATUS!",
                __FUNCTION__,
                AdapterExtension,
                Status);
    }
    return(Status);
}

NTSTATUS
VMLunDeleteUnInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun
    )

/*++

Routine Description:

    Uninitializes and deletes the Lun

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this lun will
                       reside on.

    Lun - Lun pointer that needs to be cleaned up and deleted

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

    if ( AdapterExtension == NULL || Lun == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLunQueryState(Lun, &State, FALSE) != STATUS_SUCCESS ||
        !(State == VMDeviceStopped || State == VMDeviceInitialized) ) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    if ( Lun->DeviceCreated == TRUE ) {
        Status = VMDeviceDeleteLogicalDevice(AdapterExtension,
                                             &(Lun->Device));
        if ( !NT_SUCCESS(Status) ) {
            
            //
            // Despite this error, we will go and free up rest of Lun
            //

            VMTrace(TRACE_LEVEL_ERROR,
                    VM_TRACE_LUN,
                    "[%s]:AdapterExtension:%p, Lun:%p, Failed to delete logical device, Status:%!STATUS!",
                    __FUNCTION__,
                    AdapterExtension,
                    Lun,
                    Status);
        }
    }

    VMLockUnInitialize(&(Lun->LunLock));
    StorPortFreePool(AdapterExtension,
        Lun);
    Status = STATUS_SUCCESS;

Cleanup:

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_LUN,
            "[%s]:AdapterExtension:%p, Lun:%p, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Lun,
            Status);

    return(Status);
}

NTSTATUS
VMLunAttach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun,
    _In_ PVIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR LunCreateDescriptor,
    _Inout_ PUCHAR LunId
    )

/*++

Routine Description:

    Attached the LUN to the Target

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this Lun
                       resides on.

    Bus - Bus to which this Lun belongs

    Target - Target that will own this Lun

    Lun - Lun which needs to be attached to the target

    LunCreateDescriptor - Lun descriptor that provides the parameters for creating the device

    LunId - LunId for the Lun that was attached

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
    VM_DEVICE_STATE LunState;
    UCHAR Index;

    Status = STATUS_UNSUCCESSFUL;
    LunState = VMDeviceStateUnknown;

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL || Lun == NULL || LunCreateDescriptor == NULL || LunId == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    *LunId = 0;

    Status = STATUS_INVALID_DEVICE_STATE;
    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE) {
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {
            if ( VMLockAcquireExclusive(&(Target->TargetLock)) == TRUE ) {
                if ( VMLockAcquireExclusive(&(Lun->LunLock)) == TRUE ) {

                    Status = STATUS_INVALID_DEVICE_STATE;
                    if ( VMLunQueryState(Lun, &LunState, TRUE) == STATUS_SUCCESS &&  (LunState == VMDeviceInitialized) ) {
                        Status = STATUS_QUOTA_EXCEEDED;
                        for ( Index = 0; Index < Target->MaxLunCount; Index++ ) {
                            if ( Target->Luns [Index] == VIRTUAL_MINIPORT_INVALID_POINTER ) {
                                
                                if ( Lun->DeviceCreated == FALSE ) {
                                    Status = VMDeviceCreateLogicalDevice(AdapterExtension,
                                                                         &(Target->Device),
                                                                         &(Lun->Device),
                                                                         LunCreateDescriptor);
                                    if ( !NT_SUCCESS(Status) ) {

                                        //
                                        // Break out of the loop and exit out. It does not make sense to search for more
                                        // free Lun slots.
                                        //
                                        VMTrace(TRACE_LEVEL_ERROR,
                                                VM_TRACE_LUN,
                                                "[%s]:AdapterExtension:%p, Bus:%p, Target:%p, Lun:%p, Status:%!STATUS!",
                                                __FUNCTION__,
                                                AdapterExtension,
                                                Bus,
                                                Target,
                                                Lun,
                                                Status);
                                        break;
                                    } else {

                                        Lun->DeviceCreated = TRUE;
                                    }
                                }

                                Lun->LunId = Index;
                                Lun->Target = Target;

                                Target->Luns [Index] = Lun;
                                Target->LunCount++;

                                //
                                // Below state transition will never fail, as we validated the state above
                                //
                                VMLunChangeState(Lun,
                                                 VMDeviceAttached,
                                                 &LunState,
                                                 TRUE);

                                *LunId = Index;
                                Status = STATUS_SUCCESS;
                                break;
                            }
                        }
                    }
                    VMLockReleaseExclusive(&(Lun->LunLock));
                }
                VMLockReleaseExclusive(&(Target->TargetLock));
            }        
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_LUN,
            "[%s]:AdapterExtension:%p, Bus:%p, Target:%p, Lun:%p, attach status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Bus,
            Target,
            Lun,
            Status);
    return(Status);
}

NTSTATUS
VMLunDetach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun
    )

/*++

Routine Description:

    Detach the Lun from the Target

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this Lun
                       resides on.

    Bus - Bus to which this Lun belongs to

    Target - Target that owns this Lun

    Lun - Lun that needs to be detached

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
    VM_DEVICE_STATE LunState;

    Status = STATUS_UNSUCCESSFUL;
    LunState = VMDeviceStateUnknown;

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL || Lun == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {
            if ( VMLockAcquireExclusive(&(Target->TargetLock)) == TRUE ) {
                if ( VMLockAcquireExclusive(&(Lun->LunLock)) == TRUE ) {

                    Status = STATUS_INVALID_DEVICE_STATE;
                    if ( Lun->Target != Target ) {

                        Status = STATUS_INVALID_PARAMETER;
                    } else if ( VMLunQueryState(Lun, &LunState, TRUE) == STATUS_SUCCESS && 
                                (LunState == VMDeviceAttached || LunState == VMDeviceStopped)) {

                        if ( Lun->DeviceCreated == TRUE ) {

                            Status = VMDeviceDeleteLogicalDevice(AdapterExtension,
                                                                 &(Lun->Device));
                            if ( !NT_SUCCESS(Status) ) {

                                VMTrace(TRACE_LEVEL_ERROR,
                                        VM_TRACE_LUN,
                                        "[%s]:Lun:%p failed to delete logical device, Status:%!STATUS!",
                                        __FUNCTION__,
                                        Lun,
                                        Status);
                            } else {
                                
                                //
                                // Now that we know that Lun is safe to be detached, proceed to detach.
                                // By now our logical device is deleted
                                //
                                
                                Target->Luns [Lun->LunId] = VIRTUAL_MINIPORT_INVALID_POINTER;
                                Target->LunCount--;

                                Lun->DeviceCreated = FALSE;
                                Lun->Target = VIRTUAL_MINIPORT_INVALID_POINTER;
                                Status = VMLunChangeState(Lun,
                                                          VMDeviceDetached,
                                                          &LunState,
                                                          TRUE);
                            }
                        }                   
                    }
                    VMLockReleaseExclusive(&(Lun->LunLock));
                }
                VMLockReleaseExclusive(&(Target->TargetLock));
            }
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_LUN,
            "[%s]:AdapterExtension:%p, Bus:%p, Target:%p, Lun:%p, detach status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Bus,
            Target,
            Lun,
            Status);
    return(Status);
}

NTSTATUS
VMLunStart(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _In_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun
    )

/*++

Routine Description:

    Starts the Lun

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this target
    resides on.

    Bus - Bus on which this Lun resides

    Target - Target that owns this Lun

    Lun - Lun that needs to be started

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

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL || Lun == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {       
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {
            if ( VMLockAcquireExclusive(&(Target->TargetLock)) == TRUE ) {
                if ( VMLockAcquireExclusive(&(Lun->LunLock)) == TRUE ) {
                    
                    Status = STATUS_INVALID_DEVICE_STATE;
                    if ( Lun->Target != Target ) {

                        Status = STATUS_INVALID_PARAMETER;
                    } else if ( VMLunQueryState(Lun, &State, TRUE) == STATUS_SUCCESS && State == VMDeviceAttached ) {

                        Status = VMLunChangeState(Lun, VMDeviceStarted, &State, TRUE);
                    }
                    VMLockReleaseExclusive(&(Lun->LunLock));
                }
                VMLockReleaseExclusive(&(Target->TargetLock));
            }
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_LUN,
            "[%s]:AdapterExtension:%p, Bus:%p, Target:%p, Lun:%p, OldState:%!VMDEVICESTATE!, NewState:%!VMDEVICESTATE!, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Bus,
            Target,
            Lun,
            State,
            (Status == STATUS_SUCCESS)?VMDeviceStarted:State,
            Status);

    return(Status);
}

NTSTATUS
VMLunStop(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVIRTUAL_MINIPORT_TARGET Target,
    _Inout_ PVIRTUAL_MINIPORT_LUN Lun
    )

/*++

Routine Description:

    Stops the Lun

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this lun
                       resides on.

    Bus - Bus on which this Lun resides

    Target - Target that owns this lun

    Lun - Lun that needs to be stopped

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

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL || Lun == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    do {
        if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
            if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {
                if ( VMLockAcquireExclusive(&(Target->TargetLock)) == TRUE ) {
                    
                    Status = VMLunChangeState(Lun, VMDeviceStopped, &OldState, FALSE);
                    VMLunQueryState(Lun, &NewState, FALSE);

                    VMTrace(TRACE_LEVEL_INFORMATION,
                            VM_TRACE_LUN,
                            "[%s]:AdapterExtension:%p, Bus:%p, Target:%p, Lun:%p, OldState:%!VMDEVICESTATE!, NewState:%!VMDEVICESTATE!, Status:%!STATUS!",
                            __FUNCTION__,
                            AdapterExtension,
                            Bus,
                            Target,
                            Lun,
                            OldState,
                            NewState,
                            Status);
                    VMLockReleaseExclusive(&(Target->TargetLock));
                }
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
VMLunQueryById(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _In_ PVIRTUAL_MINIPORT_TARGET Target,
    _In_ UCHAR LunId,
    _Inout_ PVIRTUAL_MINIPORT_LUN *Lun,
    _In_ BOOLEAN Reference
    )
/*++

Routine Description:

    Find the Lun by Lun ID

Arguments:

    AdapterExtension - AdapterExtension for the adapter on which this lun
                       resides on.

    Bus - Bus on which this lun resides

    Target - Target that owns this lun

    LunId - LunId for which we need to look up the Lun

    Lun - Pointer of Lun for the given LunId

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

    if ( AdapterExtension == NULL || Bus == NULL || Target == NULL || Lun == NULL || Reference == TRUE ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    *Lun = NULL;

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {
            if ( VMLockAcquireExclusive(&(Target->TargetLock)) == TRUE ) {
                Status = STATUS_DEVICE_DOES_NOT_EXIST;
                if ( LunId < Target->MaxLunCount ) {
                    
                    Status = STATUS_DEVICE_NOT_CONNECTED;
                    if ( Target->Luns [LunId] != VIRTUAL_MINIPORT_INVALID_POINTER ) {
                        *Lun = Target->Luns [LunId];
                        //
                        // Take reference when we implement it
                        //
                        Status = STATUS_SUCCESS;
                    }
                }            
                VMLockReleaseExclusive(&(Target->TargetLock));
            }
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    return(Status);
}