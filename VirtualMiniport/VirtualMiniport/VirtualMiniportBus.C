/*++

Module Name:

    VirtualMiniportBus.C

Date:

    9-Feb-2014

Abstract:

    Module contains the routines for implementing the Bus management

    All routines start with VM - for 'V'irtual 'M'iniport
    All routines are further prefixed with component name

    Device State transition

    Uninitialized->Initialized->Attached->Started->StopPending->Stopped->Detached->Uninitialized
                         |        |          |----------------------^       ^                     
                         |        |-----------------------------------------|
                         |------------------------------------------------------------^

--*/

#include <VirtualMiniportBus.h>

//
// WPP based event trace
//

#include <VirtualMiniportBus.tmh>

//
// Forward declarations of private functions
//

NTSTATUS
VMBusChangeState(
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus,
    _In_ VM_DEVICE_STATE NewState,
    _Inout_ PVM_DEVICE_STATE OldState,
    _In_ BOOLEAN LockAcquired
    );

NTSTATUS
VMBusQueryState(
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
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
#pragma alloc_text(PAGED, VMBusCreateInitialize)
#pragma alloc_text(PAGED, VMBusDeleteUninitialize)
#pragma alloc_text(PAGED, VMBusAttach)
#pragma alloc_text(PAGED, VMBusDetach)
#pragma alloc_text(PAGED, VMBusStart)
#pragma alloc_text(PAGED, VMBusStop)
#pragma alloc_text(PAGED, VMBusQueryById)

#pragma alloc_text(PAGED, VMBusChangeState)
#pragma alloc_text(PAGED, VMBusQueryState)

//
// Device routines
//

//
// Bus management routines
//

NTSTATUS
VMBusChangeState(
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus,
    _In_ VM_DEVICE_STATE NewState,
    _Inout_ PVM_DEVICE_STATE OldState,
    _In_ BOOLEAN LockAcquired
    )

/*++

Routine Description:

    Udpates the state of the Bus to desired state if the situation permits

Arguments:

    Bus - Bus whose state needs to be changed

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

    if ( Bus == NULL || OldState == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( LockAcquired == TRUE || VMLockAcquireExclusive(&Bus->BusLock) == TRUE ) {
        
        *OldState = Bus->State;

        switch(Bus->State) {
        case VMDeviceUninitialized:
            switch ( NewState ) {
            case VMDeviceUninitialized:
            case VMDeviceInitialized:
                Bus->State = NewState;
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
                Bus->State = NewState;
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
                Bus->State = NewState;
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
                Bus->State = NewState;
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
                Bus->State = NewState;
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
                Bus->State = NewState;
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
                Bus->State = NewState;
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

        VMTrace(TRACE_LEVEL_INFORMATION, 
                VM_TRACE_BUS,
                "[%s]:Bus:%p, State Tarnsition %!VMDEVICESTATE!-->%!VMDEVICESTATE!, Status:%!STATUS!",
                __FUNCTION__,
                Bus,
                *OldState,
                Bus->State,
                Status);

        if ( LockAcquired == FALSE ) {
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
    }

Cleanup:
    return(Status);
}

NTSTATUS
VMBusQueryState(
    _In_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PVM_DEVICE_STATE State,
    _In_ BOOLEAN LockAcquired
    )

/*++

Routine Description:

    Queries the bus state.

Arguments:

    Bus - Bus's whose state needs to be queries

    State - Pointer to receceive the current Bus state

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

    if ( Bus == NULL || State == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( LockAcquired == TRUE || VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE ) {
    
        *State = Bus->State;

        if ( LockAcquired == FALSE ) {
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
    }

    Status = STATUS_SUCCESS;

Cleanup:
    return(Status);
}


NTSTATUS
VMBusCreateInitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS *Bus
    )

/*++

Routine Description:

    Allocates and Initializes the bus.

Arguments:

    AdapterExtension - AdapterExtension for the adapter to which this bus
                       is expected to be attached later. This is needed to
                       read the bus configuration, and associate the allocation
                       to the adapter

    Bus - Pointer to a variable that receives the newly created bus

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
    PVIRTUAL_MINIPORT_BUS NewBus;
    ULONG Index;

    NewBus = NULL;
    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || Bus == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    StorStatus = StorPortAllocatePool(AdapterExtension,
                                     sizeof(VIRTUAL_MINIPORT_BUS),
                                     VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                     &NewBus);

    if ( StorStatus == STOR_STATUS_SUCCESS ) {
        RtlZeroMemory(NewBus,
                      sizeof(VIRTUAL_MINIPORT_BUS));

        NewBus->State = VMDeviceUninitialized;
        NewBus->Type = VMTypeBus;
        NewBus->Signature = VIRTUAL_MINIPORT_SIGNATURE_BUS;
        NewBus->MaxTargetCount = AdapterExtension->DeviceExtension->Configuration.BusesPerAdapter;
        NewBus->Adapter = NULL;
        NewBus->TargetCount = 0;
        StorStatus = StorPortAllocatePool(AdapterExtension,
                                          (sizeof(PVIRTUAL_MINIPORT_TARGET) *NewBus->MaxTargetCount),
                                          VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                          (PVOID)&(NewBus->Targets));
        if ( StorStatus == STOR_STATUS_SUCCESS ) {
            for ( Index = 0; Index < NewBus->MaxTargetCount; Index++ ) {
                NewBus->Targets [Index] = VIRTUAL_MINIPORT_INVALID_POINTER;
            }

            InitializeListHead(&NewBus->List);
            Status = VMRtlCreateGUID(&NewBus->UniqueId);

            //
            // Keep this at the end
            //
            Status = VMLockInitialize(&(NewBus->BusLock),
                                      LockTypeExecutiveResource);

            if ( NT_SUCCESS(Status) ) {

                NewBus->State = VMDeviceInitialized;
                Status = STATUS_SUCCESS;
                *Bus = NewBus;
                VMTrace(TRACE_LEVEL_INFORMATION,
                        VM_TRACE_BUS,
                        "[%s]:AdapterExtension:%p, Bus:%p Created",
                        __FUNCTION__,
                        AdapterExtension,
                        Bus);
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
        
        if ( NewBus != NULL ) {
            
            VMLockUnInitialize(&(NewBus->BusLock));
            if ( NewBus->Targets != NULL) {
                StorPortFreePool(AdapterExtension,
                                 NewBus->Targets);
            }
            StorPortFreePool(AdapterExtension,
                             NewBus);
        }

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_BUS,
                "[%s]:AdapterExtension:%p, Bus creation failed, Status:%!STATUS!",
                __FUNCTION__,
                AdapterExtension,
                Status);
    }

    return(Status);
}

NTSTATUS
VMBusDeleteUninitialize(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus
    )

/*++

Routine Description:

    Uninitializes and deletes the bus. Uninitialize and deletion can
    happen only if bus is detached/disconnected from the adapter. We
    get adapter as a parameter for consistency. But at this time we are
    already disconnected from the adapter.

Arguments:

    AdapterExtension - AdapterExtension for the adapter to which this bus
    belongs to.

    Bus - Bus pointer that needs to be cleaned up

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    VM_DEVICE_STATE State;

    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || Bus == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    //
    // Validate the Bus state.

    if ( (VMBusQueryState(Bus, &State, FALSE) != STATUS_SUCCESS) || 
        !(State == VMDeviceInitialized || State == VMDeviceDetached)) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    //
    // By this time, we would have cleaned up targets
    //

    if ( Bus->Targets != NULL ) {
        StorPortFreePool(AdapterExtension,
                         Bus->Targets);
    }
    
    VMLockUnInitialize(&(Bus->BusLock));

    StorPortFreePool(AdapterExtension,
                     Bus);
    Status = STATUS_SUCCESS;

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_BUS,
            "[%s]:AdapterExtension:%p, Bus:%p, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Bus,
            Status);
    return(Status);
}

NTSTATUS
VMBusAttach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus,
    _Inout_ PUCHAR BusId
    )

/*++

Routine Description:

   Attaches the bus to the adapter. Attach/detach routines are a deviation
   to the module isolation I am trying to implement by keeping the responsibilty
   of adapter attach/detach with adapter module. We will follow this same
   approach in other modules though.

Arguments:

    AdapterExtension - AdapterExtension for the adapter to which this bus
                       belongs to.

    Bus - Bus pointer that needs to be attached

    BusId - Bus id where we got attached successfully

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    UCHAR Index;
    VM_DEVICE_STATE State;

    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || Bus == NULL || BusId == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    //
    // Acquire the adapter lock to scan for a free slot and attach the bus to the 
    // adapter. We acquire the bus lock to keep it consistent. This is not needed
    // as the bus we are attempting to attach should not be used by anybody else.
    //
    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE) {

            Status = STATUS_INVALID_DEVICE_STATE;

            //
            // Validate the state
            //
            if ( VMBusQueryState(Bus, &State, TRUE) == STATUS_SUCCESS && State == VMDeviceInitialized ) {
                Status = STATUS_QUOTA_EXCEEDED;
                for ( Index = 0; Index < AdapterExtension->MaxBusCount; Index++ ) {
                    if ( AdapterExtension->Buses [Index] == VIRTUAL_MINIPORT_INVALID_POINTER ) {

                        //
                        // Update bus attributes
                        //
                        Bus->BusId = Index;                      
                        Bus->Adapter = AdapterExtension;

                        //
                        // Update adapter attributes
                        //
                        AdapterExtension->BusCount++;
                        AdapterExtension->Buses [Index] = Bus;

                        *BusId = Bus->BusId;

                        //
                        // Dont validate the return status of this state change, we ensured
                        // we are in right state before asking this transition
                        //
                        VMBusChangeState(Bus, VMDeviceAttached, &State, TRUE);
                        Status = STATUS_SUCCESS;
                        break;
                    }
                }
            }
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_BUS,
            "[%s]::AdapterExtension:%p, Bus:%p [%d] attach status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Bus,
            (Status==STATUS_SUCCESS)? Bus->BusId:-1,
            Status);

    return(Status);
}

NTSTATUS
VMBusDetach(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus
    )

/*++

Routine Description:

   Detaches the bus from the adapter

Arguments:

    AdapterExtension - AdapterExtension for the adapter to which this bus
    belongs to.

    Bus - Bus pointer that needs to be detached

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    VM_DEVICE_STATE State, OldState;

    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || Bus == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    //
    // Acquire the adapter lock to scan for a free slot and attach the bus to the 
    // adapter. We acquire the bus lock to keep it consistent. This is not needed
    // as the bus we are attempting to attach should not be used by anybody else.
    //
    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
        if ( VMLockAcquireExclusive(&(Bus->BusLock)) == TRUE) {

            Status = STATUS_INVALID_DEVICE_STATE;

            if ( Bus->Adapter != AdapterExtension ) {
                Status = STATUS_INVALID_PARAMETER;
            } 
            else if ( VMBusQueryState(Bus, &State, TRUE) == STATUS_SUCCESS && 
                      (State == VMDeviceAttached || State == VMDeviceStopped) ) {

                AdapterExtension->Buses [Bus->BusId] = VIRTUAL_MINIPORT_INVALID_POINTER;
                AdapterExtension->BusCount--;

                Bus->Adapter = NULL;
                Status = VMBusChangeState(Bus,
                                          VMDeviceDetached,
                                          &OldState,
                                          TRUE);
            }
            VMLockReleaseExclusive(&(Bus->BusLock));
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_BUS,
            "[%s]::AdapterExtension:%p, Bus:%p [%d] detach status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Bus,
            (Status == STATUS_SUCCESS)?Bus->BusId:-1,
            Status);
    return(Status);
}

NTSTATUS
VMBusStart(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus
    )

/*++

Routine Description:

   Starts the bus. We acquire the adapter lock here, so that adapter state
   does not change when we are starting the bus. This could be relaxed.

Arguments:

    AdapterExtension - AdapterExtension for the adapter to which this bus
    belongs to.

    Bus - Bus pointer that needs to be started

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    VM_DEVICE_STATE OldState, NewState;

    Status = STATUS_UNSUCCESSFUL;
    OldState = VMDeviceStateUnknown;
    NewState = VMDeviceStateUnknown;


    if ( AdapterExtension == NULL || Bus == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
        
        Status = VMBusChangeState(Bus,
                                  VMDeviceStarted,
                                  &OldState,
                                  FALSE);
        
        VMBusQueryState(Bus,
                        &NewState,
                        FALSE);

        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_BUS,
                "[%s]::AdapterExtension:%p, Bus:%p, OldState:%!VMDEVICESTATE!, NewState:%!VMDEVICESTATE!, Status:%!STATUS!",
                __FUNCTION__,
                AdapterExtension,
                Bus,
                OldState,
                NewState,
                Status);
        
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    return(Status);
}

NTSTATUS
VMBusStop(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_BUS Bus
    )

/*++

Routine Description:

   Stops the bus

   We acquire the adapter lock here, so that adapter state
   does not change when we are stopping the bus. This could be relaxed.

Arguments:

    AdapterExtension - AdapterExtension for the adapter to which this bus
    belongs to.

    Bus - Bus pointer that needs to be stopped

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
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

    if ( AdapterExtension == NULL || Bus == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    //
    // Block for the state transition to either succeed or to fail, but in
    // pending state.
    //
    do {
        if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {

            Status = VMBusChangeState(Bus,
                                      VMDeviceStopped,
                                      &OldState,
                                      FALSE);
            
            // We dont care about query status
            VMBusQueryState(Bus,
                            &NewState,
                            FALSE);

            VMTrace(TRACE_LEVEL_INFORMATION,
                    VM_TRACE_BUS,
                    "[%s]::AdapterExtension:%p, Bus:%p, OldState:%!VMDEVICESTATE!, NewState:%!VMDEVICESTATE!, Status:%!STATUS!",
                    __FUNCTION__,
                    AdapterExtension,
                    Bus,
                    OldState,
                    NewState,
                    Status);

            VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
        }

        if ( NewState == VMDeviceStopPending ) {
            VMRtlDelayExecution(&DelayFiveSeconds);
        }
    } while ( Status == STATUS_PENDING || NewState == VMDeviceStopPending);

Cleanup:
    return(Status);
}

NTSTATUS
VMBusQueryById(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _Inout_ PVIRTUAL_MINIPORT_BUS *Bus,
    _In_ BOOLEAN Reference
    )

/*++
Routine Description:

   Find the pointer to the bus given the bus ID

Arguments:

    AdapterExtension - AdapterExtension for the adapter to which this bus
                      belongs to.

    BusId - Bus address

    Bus - pointer that receives the bus

    Reference - indicates if the reference has to be taken on the bus.
                For now this is un-used

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    Other NTSTATUS from callee
--*/

{
    NTSTATUS Status;

    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || Bus == NULL || Reference == TRUE ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    *Bus = NULL;

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {

        Status = STATUS_DEVICE_DOES_NOT_EXIST;

        //
        // If bus is found on the adapter, and on the given busID, its state should be
        // in attached or started.
        //
        if ( BusId < AdapterExtension->MaxBusCount ) {

            Status = STATUS_DEVICE_NOT_CONNECTED;
            if ( AdapterExtension->Buses [BusId] != VIRTUAL_MINIPORT_INVALID_POINTER ) {
                *Bus = AdapterExtension->Buses [BusId];

                //
                // Bus reference should be acquired here, when we implement it
                //
                Status = STATUS_SUCCESS;
            }
        }
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    return(Status);
}