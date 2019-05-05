/*++

Module Name:

    VirtualMiniportAdapter.C

Date:

    28-Feb-2014

Abstract:

    Module implements adapter specific functionality.
    All routines start with VM - for 'V'irtual 'M'iniport

--*/

//
// Headers
//

#include <VirtualMiniportAdapter.h>
#include <VirtualMiniportBus.h>

//
// WPP based event trace
//

#include <VirtualMiniportAdapter.tmh>

//
// Forward declarations of private routines
//

//
// Define the attributes of functions; declarations are
// in module specific header
//

#pragma alloc_text(PAGED, VMAdapterInitialize)
#pragma alloc_text(PAGED, VMAdapterUnInitialize)
#pragma alloc_text(PAGED, VMAdapterStart)
#pragma alloc_text(PAGED, VMAdapterStop)

//
// Adapter routines
//
_Requires_lock_held_(DeviceExtension->ExtensionLock)
NTSTATUS
VMAdapterInitialize(
    _In_ PVIRTUAL_MINIPORT_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVOID VMHwContext,
    _In_ PVOID BusInformation,
    _In_ PVOID LowerDevice,
    _In_ PCHAR ArgumentString
    )

/*++

Routine Description:

    Initializes the adapter extension

Arguments:

    DeviceExtension - Device extension of the control device

    AdapterExtension - adapter extension to be initialized

    VMHwContext - PDO of the PnP managed for virtual miniport

    BusInformation - pointer to miniport's FDO

    LowerDevice - pointer to miniport controllerd FDO

    ArgumentString - NULL-terminated ASCII string; Device information
    from registry such as a base parameter

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_DEVICE_DOES_NOT_EXIST
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    ULONG StorStatus;
    ULONG Index;

    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(ArgumentString);
    
    Status = STATUS_DEVICE_DOES_NOT_EXIST;
    StorStatus = STOR_STATUS_NOT_IMPLEMENTED;

    //
    // Initialize the adapter extension
    //
    RtlZeroMemory(AdapterExtension,
                  sizeof(VIRTUAL_MINIPORT_ADAPTER_EXTENSION));
    AdapterExtension->State = VMDeviceUninitialized;
    InitializeListHead(&(AdapterExtension->List));
    AdapterExtension->Signature = VIRTUAL_MINIPORT_SIGNATURE_ADAPTER_EXTENSION;
    AdapterExtension->Type = VMTypeAdapter;
    Status = VMRtlCreateGUID(&(AdapterExtension->UniqueId));
    if( !NT_SUCCESS(Status) ) {
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_ADAPTER,
                "[%s]:VMRtlCreateGUID failed with Status:%!STATUS!",
                __FUNCTION__,
                Status);
        goto Cleanup;
    }
    AdapterExtension->DeviceExtension = DeviceExtension;

    //
    // Take the reference on these Device Objects
    //
    ObReferenceObject((PDEVICE_OBJECT) VMHwContext);
    ObReferenceObject(BusInformation);
    ObReferenceObject(LowerDevice);
    AdapterExtension->PhysicalDeviceObject = (PDEVICE_OBJECT) VMHwContext;
    AdapterExtension->DeviceObject = BusInformation;
    AdapterExtension->LowerDeviceObject = LowerDevice;
    AdapterExtension->BusCount = 0;
    AdapterExtension->MaxBusCount = DeviceExtension->Configuration.BusesPerAdapter;
    //InitializeListHead(&(AdapterExtension->Buses));
    StorStatus = StorPortAllocatePool(AdapterExtension,
                                      (DeviceExtension->Configuration.BusesPerAdapter*sizeof(PVIRTUAL_MINIPORT_BUS)),
                                      VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                      (PVOID*)&(AdapterExtension->Buses));

    if( StorStatus != STOR_STATUS_SUCCESS ) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_ADAPTER,
                "[%s]:StorPortAllocatePool failed with Status:%!STATUS!",
                __FUNCTION__,
                StorStatus);
        goto Cleanup;
    }

    for( Index = 0; Index < DeviceExtension->Configuration.BusesPerAdapter; Index++ ) {
        AdapterExtension->Buses [Index] = VIRTUAL_MINIPORT_INVALID_POINTER;
    }

    Status = VMLockInitialize(&(AdapterExtension->AdapterLock),
                              LockTypeExecutiveResource);
    if( !NT_SUCCESS(Status) ) {

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_ADAPTER,
                "[%s]:VMLockInitialize failed with Status:%!STATUS!",
                __FUNCTION__,
                Status);
        goto Cleanup;
    }

    Status = VMSchedulerInitialize(AdapterExtension,
                                   &AdapterExtension->Scheduler);

    if ( !NT_SUCCESS(Status) ) {
        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_ADAPTER,
                "[%s]:VMSchedulerInitialize failed, Status:%!STATUS!",
                __FUNCTION__,
                Status
                );
        goto Cleanup;
    }

    Status = IoRegisterDeviceInterface(AdapterExtension->PhysicalDeviceObject,
                                       &GUID_DEVINTERFACE_VIRTUAL_MINIPORT_DEVICE,
                                       NULL,
                                       &(AdapterExtension->SymbolicLinkName));
    if( !NT_SUCCESS(Status) ) {

        //
        // Log error
        //

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_ADAPTER,
                "[%s]:IoRegisterDeviceInterface failed with Status:%!STATUS!",
                __FUNCTION__,
                Status);
        goto Cleanup;
    }

    Status = IoSetDeviceInterfaceState(&(AdapterExtension->SymbolicLinkName),
                                       TRUE);
    if( !NT_SUCCESS(Status) ) {

        //
        // Log error
        //

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_ADAPTER,
                "[%s]:IoSetDeviceInterfaceState failed (SymbolicLink:%wZ)  Status:%!STATUS!",
                __FUNCTION__,
                &(AdapterExtension->SymbolicLinkName),
                Status);
        goto Cleanup;
    }

    Status = STATUS_SUCCESS;
    AdapterExtension->State = VMDeviceInitialized;

Cleanup:
    if( !NT_SUCCESS(Status) ) {
        if( AdapterExtension->Buses != NULL ) {
            StorPortFreePool(AdapterExtension,
                             AdapterExtension->Buses);
        }

        VMLockUnInitialize(&(AdapterExtension->AdapterLock));
        VMSchedulerUnInitialize(AdapterExtension,
                                &(AdapterExtension->Scheduler));
        AdapterExtension->State = VMDeviceUninitialized;
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_ADAPTER,
            "[%s]:AdapterExtension: %p, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Status);

    return(Status);
}

_Requires_lock_held_(DeviceExtension->ExtensionLock)
NTSTATUS
VMAdapterUnInitialize(
    _In_ PVIRTUAL_MINIPORT_DEVICE_EXTENSION DeviceExtension,
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension
    )

/*++

Routine Description:

    Un-Initializes the adapter extension

Arguments:

    DeviceExtension - Device extension of the control device

    AdapterExtension - adapter extension to be initialized

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_DEVICE_DOES_NOT_EXIST
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceExtension);

    ASSERT ( (AdapterExtension->State == VMDeviceInitialized) || (AdapterExtension->State == VMDeviceStopped) );

    Status = IoSetDeviceInterfaceState(&(AdapterExtension->SymbolicLinkName),
                                       FALSE);
    if( !NT_SUCCESS(Status) ) {
    
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_ADAPTER,
                "[%s]:IoSetDeviceInterfaceState failed (SymbolicLink:%wZ)  Status:%!STATUS!",
                __FUNCTION__,
                &(AdapterExtension->SymbolicLinkName),
                Status);
        goto Cleanup;
    }

    //
    // Could not find how to un-register the registered device interface. So keeping this
    // comment here.
    //

    //
    // Remove Buses and the hierarchy 
    //

    //
    // Stop the scheduler
    //

    Status = VMSchedulerUnInitialize(AdapterExtension,
                                     &AdapterExtension->Scheduler);
    //
    // Free the bus array now
    //
    if( AdapterExtension->Buses != NULL ) {
        StorPortFreePool(AdapterExtension,
                         AdapterExtension->Buses);
    }

    //
    // Remove the device object references
    //
    ObDereferenceObject(AdapterExtension->DeviceObject);
    ObDereferenceObject(AdapterExtension->PhysicalDeviceObject);
    ObDereferenceObject(AdapterExtension->LowerDeviceObject);

    AdapterExtension->State = VMDeviceUninitialized;

    VMLockUnInitialize(&(AdapterExtension->AdapterLock));

Cleanup:

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_ADAPTER,
            "[%s]:AdapterExtension: %p, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Status);

    return(Status);
}

NTSTATUS
VMAdapterStart(
    _In_ PVIRTUAL_MINIPORT_DEVICE_EXTENSION DeviceExtension,
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension
    )

/*++

Routine Description:

    Transitions the adapater state to started and takes necessary actions
    to transitions the various components of an adapter.

Arguments:

    DeviceExtension - Device extension of the control device

    AdapterExtension - adapter extension to be started

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    VM_SCHEDULER_STATE SchedulerOldState;
    VM_DEVICE_STATE PreviousState, CurrentState;

    UNREFERENCED_PARAMETER(DeviceExtension);
    Status = STATUS_REQUEST_NOT_ACCEPTED;
    PreviousState = VMDeviceStateUnknown;
    CurrentState = VMDeviceStateUnknown;
    
    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
    
        Status = VMSchedulerChangeState(&(AdapterExtension->Scheduler),
                                        VMSchedulerStarted,
                                        &SchedulerOldState,
                                        TRUE);
        if ( NT_SUCCESS(Status) ) {
            PreviousState = AdapterExtension->State;
            AdapterExtension->State = VMDeviceStarted;
            CurrentState = AdapterExtension->State;
        }

        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

    if ( NT_SUCCESS(Status) ) {
        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_ADAPTER,
                "[%s]:AdapterExtension:%p, State transitioned from %!VMDEVICESTATE! to %!VMDEVICESTATE!, Status:%!STATUS!",
                __FUNCTION__,
                AdapterExtension,
                PreviousState,
                CurrentState,
                Status);
    }

    return (Status);
}

NTSTATUS
VMAdapterStop(
    _In_ PVIRTUAL_MINIPORT_DEVICE_EXTENSION DeviceExtension,
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ BOOLEAN Block
    )

/*++

Routine Description:

    If possible, transitions the adapter state to stopped

Arguments:

    DeviceExtension - Device extension of the control device

    AdapterExtension - adapter extension to be stopped

    Block - Indicates if the request should be blocked

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    VM_SCHEDULER_STATE SchedulerOldState;
    VM_DEVICE_STATE PreviousState, CurrentState;

    UNREFERENCED_PARAMETER(DeviceExtension);
    Status = STATUS_REQUEST_NOT_ACCEPTED;
    PreviousState = VMDeviceStateUnknown;
    CurrentState = VMDeviceStateUnknown;

    if ( VMLockAcquireExclusive(&(AdapterExtension->AdapterLock)) == TRUE ) {
    
        Status = VMSchedulerChangeState(&(AdapterExtension->Scheduler),
                                        VMSchedulerStopped,
                                        &SchedulerOldState,
                                        Block);
        
        PreviousState = AdapterExtension->State;
        if ( Status == STATUS_SUCCESS ) {            
            AdapterExtension->State = VMDeviceStopped;           
        }
        CurrentState = AdapterExtension->State;
        VMLockReleaseExclusive(&(AdapterExtension->AdapterLock));
    }

    if ( Status == STATUS_SUCCESS ) {
        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_ADAPTER,
                "[%s]:AdapterExtension:%p, State transitioned from %!VMDEVICESTATE! to %!VMDEVICESTATE!, Status:%!STATUS!",
                __FUNCTION__,
                AdapterExtension,
                PreviousState,
                CurrentState,
                Status);
    }
    return (Status);
}