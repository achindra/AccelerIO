/*++

Module Name:

    VirtualMiniportIoctl.C

Date:

    3-Mar-2014

Abstract:

    Module implements the SRB_IO_CONTROL code sent by the user
    All routines start with VM - for 'V'irtual 'M'iniport

--*/

//
// Headers
//

#include <VirtualMiniportIoctl.h>
#include <VirtualMiniportDevice.h>
#include <VirtualMiniportBus.h>
#include <VirtualMiniportTarget.h>
#include <VirtualMiniportLun.h>

//
// WPP based event trace
//

#include <VirtualMiniportIoctl.tmh>

//
// Forward declarations of private functions
//

NTSTATUS
VMSrbIoControlWorker(
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ BOOLEAN AbortRequest
    );

//
// Ioctl buffer processing routines
//

NTSTATUS
VMSrbIoControlBuildAdapterDetails(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlDescriptor
    );

NTSTATUS
VMSrbIoControlBuildBusDetails(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlDescriptor
    );

NTSTATUS
VMSrbIoControlBuildTargetDetails(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _In_ UCHAR TargetId,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlDescriptor
    );

NTSTATUS
VMSrbIoControlBuildLunDetails(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _In_ UCHAR TargetId,
    _In_ UCHAR LunId,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlDescriptor
    );
//
// Define the attributes of functions; declarations are in module
// specific header
//

#pragma alloc_text(NONPAGED, VMSrbIoControl)

#pragma alloc_text(NONPAGED, VMSrbIoControlWorker)
#pragma alloc_text(NONPAGED, VMSrbIoControlBuildAdapterDetails)
#pragma alloc_text(NONPAGED, VMSrbIoControlBuildBusDetails)
#pragma alloc_text(NONPAGED, VMSrbIoControlBuildTargetDetails)
#pragma alloc_text(NONPAGED, VMSrbIoControlBuildLunDetails)

//
// Driver specific routines
//

BOOLEAN
VMSrbIoControl(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    Handles the SRB_IO_CONTROL from the user

Arguments:

    AdapterExtension - Adapter to which this request is directed to

    Srb - SCSI_REQUEST_BLOCK that represents the IRP_MJ_DEVICECONTROL

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;
    BOOLEAN CompleteRequest;
    BOOLEAN Queued;
    BOOLEAN ValidSignature;
    NTSTATUS NtStatus;
    PSRB_IO_CONTROL IoControl;
    PVIRTUAL_MINIPORT_SRB_EXTENSION SrbExtension;

    Status = FALSE;
    CompleteRequest = FALSE;
    Queued = FALSE;
    ValidSignature = FALSE;

    //
    // We own the SRB extension
    //

    SrbExtension = Srb->SrbExtension;
    SrbExtension->Adapter = AdapterExtension;
    SrbExtension->Srb = (PSCSI_REQUEST_BLOCK) Srb;
    IoControl = (PSRB_IO_CONTROL) Srb->DataBuffer;

    NtStatus = VMSchedulerInitializeWorkItem((PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM) SrbExtension,
                                              VMSchedulerHintDefault,
                                              VMSrbIoControlWorker);
    if ( !NT_SUCCESS(NtStatus) ) {
        Status = FALSE;
        goto Cleanup;
    }

    //
    // Make sure this IOCTL was directed to us
    //
    if ( RtlCompareMemory(IoControl->Signature,
                          VIRTUAL_MINIPORT_IOCTL_SIGNATURE,
                          sizeof(VIRTUAL_MINIPORT_IOCTL_SIGNATURE)) == sizeof(VIRTUAL_MINIPORT_IOCTL_SIGNATURE) ) {
        ValidSignature = TRUE;   
    }

    if ( ValidSignature ) {

        //
        // We switch on control code here and as well in worker routine so that
        // if there are any control code that can be handled here we could do so.
        //
        switch ( IoControl->ControlCode ) {

            //
            // Directly handles control codes here. Anything handled synchronously here
            // will have to update
            //  1. Header->ReturnCode
            //  2. Mark CompleteRequest to TRUE
            //  3. Set Srb->SrbStatus to appropriate SCSI status
            //

        default:
            
            //
            // Everything else gets queued
            //
            if ( VMSchedulerScheduleWorkItem(&(AdapterExtension->Scheduler),
                                             (PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM) SrbExtension,
                                             FALSE) == FALSE ) {
                Status = FALSE;
            } else {
                Queued = TRUE;
                Status = TRUE;
            }
            break;
        }

    } else {
        
        //
        // We are not the targeted device
        //
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        CompleteRequest = TRUE;
    }

    if ( CompleteRequest == TRUE ) {

        //
        // Requests that are to be completed synchronously here will come here
        //
        StorPortNotification(RequestComplete,
            AdapterExtension,
            Srb);
        Status = TRUE;
    }

Cleanup:

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_IOCTL,
            "[%s]:AdpaterExtension:%p, Srb:%p, Ioctl:%!STORIOCTL!, Status:%!bool!, Queued:%!bool!, Srb:%!SRB!",
            __FUNCTION__,
            AdapterExtension,
            Srb,
            IoControl->ControlCode,
            Status,
            Queued,
            Srb);

    return(Status);
}

NTSTATUS
VMSrbIoControlWorker(
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ BOOLEAN Abort
    )

/*++

Routine Description:

    IoControl delayed worker. Any work that we could afford to do at passive
    will be queued here.

Arguments:

    WorkItem - SRB extension in the form of WorkItem

    Abort - Indicates request should be aborted immediately

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    NTSTATUS

    STATUS_SUCCESS
    Any other NTSTATUS

--*/

{
    NTSTATUS Status;
    NTSTATUS Status1;
    ULONG Index;
    UCHAR BusId, TargetId, LunId;
    PVIRTUAL_MINIPORT_SRB_EXTENSION SrbExtension;
    PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension;
    PSCSI_REQUEST_BLOCK Srb;  
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlDescriptor;

    //
    // Declare the variables to hold each device type
    //

    PVIRTUAL_MINIPORT_BUS Bus;
    PVIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR TargetDescriptor;
    PVIRTUAL_MINIPORT_TARGET Target;
    PVIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR LunDescriptor;
    PVIRTUAL_MINIPORT_LUN Lun;

    Status = STATUS_SUCCESS;
    SrbExtension = (PVIRTUAL_MINIPORT_SRB_EXTENSION)WorkItem;
    AdapterExtension = SrbExtension->Adapter;
    BusId = 0;
    TargetId = 0;
    LunId = 0;

    //
    // Srb extension validation should have happened before this SRB was queued to us
    //
    Srb = SrbExtension->Srb;
    IoctlDescriptor = Srb->DataBuffer;

    if ( Abort == TRUE ) {
        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_IOCTL,
                "[%s]:Srb:%p Aborting",
                __FUNCTION__,
                Srb);

        Srb->SrbStatus = SRB_STATUS_ABORTED;
        goto CompleteRequest;
    }

    switch ( IoctlDescriptor->SrbIoControl.ControlCode ) {

    case IOCTL_VIRTUAL_MINIPORT_DUMMY:

        for ( Index = 0; Index < IoctlDescriptor->SrbIoControl.Length; Index++ ) {
            IoctlDescriptor->RequestResponse.DummyData.Buffer [Index] = 'V';
        }
        IoctlDescriptor->SrbIoControl.ReturnCode = STATUS_SUCCESS;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    case IOCTL_VIRTUAL_MINIPORT_QUERY_ADAPTER_DETAILS:

        Status = VMSrbIoControlBuildAdapterDetails(AdapterExtension,
                                                   IoctlDescriptor);

        IoctlDescriptor->SrbIoControl.ReturnCode = Status;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    case IOCTL_VIRTUAL_MINIPORT_CREATE_BUS:

        //
        // NOTE: Bus will acquire adapter lock as needed
        // 1. Create/Initialize bus
        // 2. Attach bus to adapter
        // 3. Send device change notification if we succeed
        // 4. Any failure will revert the changes
        //
        Status = VMBusCreateInitialize(AdapterExtension,
                                       &Bus);
        if ( NT_SUCCESS(Status) ) {
            Status = VMBusAttach(AdapterExtension,
                                 Bus,
                                 &BusId);
            if ( !NT_SUCCESS(Status) ) {
                VMTrace(TRACE_LEVEL_ERROR,
                        VM_TRACE_IOCTL,
                        "[%s]:VMBusAttach Failed with status %!STATUS!",
                        __FUNCTION__,
                        Status);
                Status1 = VMBusDeleteUninitialize(AdapterExtension,
                                                  Bus);
            } else {
                //
                // Handle the error condition, by detach and deleting the bus
                //
                Status = VMBusStart(AdapterExtension,
                                    Bus);
            }
        }

        //
        // STATUS_PENDING is also treated as success,
        // so not using NT_SUCCESS
        //
        if ( Status == STATUS_SUCCESS ) {
            
            VMDeviceReportStateChange(AdapterExtension,
                                      BusId,
                                      0,
                                      0,
                                      STATE_CHANGE_BUS);

            Status = VMSrbIoControlBuildAdapterDetails(AdapterExtension,
                                                       IoctlDescriptor);

        }

        IoctlDescriptor->SrbIoControl.ReturnCode = Status;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    case IOCTL_VIRTUAL_MINIPORT_QUERY_BUS_DETAILS:

        BusId = IoctlDescriptor->RequestResponse.BusDetails.Bus;
        Status = STATUS_DEVICE_NOT_CONNECTED;

        //
        // Address validation is done as part of building the details
        //

        Status = VMSrbIoControlBuildBusDetails(AdapterExtension,
                                               BusId,
                                               IoctlDescriptor);

        IoctlDescriptor->SrbIoControl.ReturnCode = Status;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    case IOCTL_VIRTUAL_MINIPORT_CREATE_TARGET:
        
        //
        // 1. Validate parameters
        // 2. Create/Initialize Target (Will create/attach tiers)
        // 3. Attach Target to Bus
        // 4. Send change notification
        // 5. Any failures, will revert the changes
        //
        //break;

        TargetDescriptor = &(IoctlDescriptor->RequestResponse.CreateTarget);
        BusId = TargetDescriptor->Bus;
        Status = VMBusQueryById(AdapterExtension,
                                BusId,
                                &Bus,
                                FALSE);
        if ( NT_SUCCESS(Status) ) {
            Status = VMTargetCreateInitialize(AdapterExtension,
                                                TargetDescriptor,
                                                &Target);
            if ( NT_SUCCESS(Status) ) {

                Status = VMTargetAttach(AdapterExtension,
                                        Bus,
                                        Target,
                                        &TargetId);
                if ( NT_SUCCESS(Status) ) {

                    //
                    // Failure to start the target need not lead to
                    // detach/deletion of the target. Target will be
                    // left in detached state
                    //
                    Status = VMTargetStart(AdapterExtension,
                                           Bus,
                                           Target);
                } else {
                    VMTrace(TRACE_LEVEL_ERROR,
                            VM_TRACE_IOCTL,
                            "[%s]AdapterExtension:%p, Bus:%p, Target:%p, Attach failed, Status:%!STATUS!",
                            __FUNCTION__,
                            AdapterExtension,
                            Bus,
                            Target,
                            Status);
                    VMTargetDeleteUnInitialize(AdapterExtension,
                                                Target);
                }
            }
        }

        if ( Status == STATUS_SUCCESS ) {
            VMDeviceReportStateChange(AdapterExtension,
                                      BusId,
                                      TargetId,
                                      0,
                                      STATE_CHANGE_TARGET);
            Status = VMSrbIoControlBuildBusDetails(AdapterExtension,
                                                      BusId,
                                                      IoctlDescriptor);
        }
        IoctlDescriptor->SrbIoControl.ReturnCode = Status;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    case IOCTL_VIRTUAL_MINIPORT_QUERY_TARGET_DETAILS:
        BusId = IoctlDescriptor->RequestResponse.TargetDetails.Bus;
        TargetId = IoctlDescriptor->RequestResponse.TargetDetails.Target;
        Status = STATUS_DEVICE_NOT_CONNECTED;

        //
        // Address validation is done as part of building the details
        //

        Status = VMSrbIoControlBuildTargetDetails(AdapterExtension,
                                                  BusId,
                                                  TargetId,
                                                  IoctlDescriptor);

        IoctlDescriptor->SrbIoControl.ReturnCode = Status;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    case IOCTL_VIRTUAL_MINIPORT_CREATE_LUN:

        //
        // 1. Validate parameters
        // 2. Create/Initialize Lun
        // 3. Attach Lun to the Target
        // 4. Send change notification
        // 5. Any failures will revert the changes
        //break;

        LunDescriptor = &(IoctlDescriptor->RequestResponse.CreateLun);
        BusId = LunDescriptor->Bus;
        TargetId = LunDescriptor->Target;

        Status = VMBusQueryById(AdapterExtension, BusId, &Bus, FALSE);
        if ( NT_SUCCESS(Status) ) {
            Status = VMTargetQueryById(AdapterExtension, Bus, TargetId, &Target, FALSE);
            if ( NT_SUCCESS(Status) ) {
                Status = VMLunCreateInitialize(AdapterExtension,
                                               LunDescriptor,
                                               &Lun);
                if ( NT_SUCCESS(Status) ) {
                    Status = VMLunAttach(AdapterExtension,
                                         Bus,
                                         Target,
                                         Lun,
                                         LunDescriptor,
                                         &LunId);
                    if ( NT_SUCCESS(Status) ) {

                        //
                        // Failure to start the Lun, does not demand for deletion of the
                        // Lun. We simply leave it in attached state
                        //
                        Status = VMLunStart(AdapterExtension,
                                            Bus,
                                            Target,
                                            Lun);
                    } else {
                        VMTrace(TRACE_LEVEL_ERROR,
                                VM_TRACE_IOCTL,
                                "[%s]:AdapterExtension:%p, Bus:%p, Target:%p, Lun%p, attach failed, Status:%!STATUS!",
                                __FUNCTION__,
                                AdapterExtension,
                                Bus,
                                Target,
                                Lun,
                                Status);
                        VMLunDeleteUnInitialize(AdapterExtension,
                                                Lun);
                    }
                }
            }
        }

        if ( Status == STATUS_SUCCESS ) {

            //
            // Report Lun state change
            //
            VMDeviceReportStateChange(AdapterExtension,
                                      BusId,
                                      TargetId,
                                      LunId,
                                      STATE_CHANGE_LUN);
            Status = VMSrbIoControlBuildTargetDetails(AdapterExtension,
                                                      BusId,
                                                      TargetId,
                                                      IoctlDescriptor);
        }

        IoctlDescriptor->SrbIoControl.ReturnCode = Status;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    case IOCTL_VIRTUAL_MINIPORT_QUERY_LUN_DETAILS:
        BusId = IoctlDescriptor->RequestResponse.LunDetails.Bus;
        TargetId = IoctlDescriptor->RequestResponse.LunDetails.Target;
        LunId = IoctlDescriptor->RequestResponse.LunDetails.Lun;
        Status = STATUS_DEVICE_NOT_CONNECTED;

        //
        // Address validation is done as part of building the details
        //
        Status = VMSrbIoControlBuildLunDetails(AdapterExtension,
                                               BusId,
                                               TargetId,
                                               LunId,
                                               IoctlDescriptor);

        IoctlDescriptor->SrbIoControl.ReturnCode = Status;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    default:
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;
    }

CompleteRequest:

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_IOCTL,
            "[%s]:AdpaterExtension:%p, Srb:%p, Ioctl:%!STORIOCTL!, SrbStatus:0x%08x, Srb:%!SRB!",
            __FUNCTION__,
            SrbExtension->Adapter,
            Srb,
            IoctlDescriptor->SrbIoControl.ControlCode,
            Srb->SrbStatus,
            Srb);

    //
    // Now that we are done with the request, complete the request
    //
    StorPortNotification(RequestComplete,
                         SrbExtension->Adapter,
                         Srb);
    return(Status);
}

//
// Functions to fill the output buffer for the user in reponse to
// ioctls
//

NTSTATUS
VMSrbIoControlBuildAdapterDetails(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlDescriptor
    )

/*++

Routine Description:

    Prepares the adapter details buffer if sufficient buffer is
    passed by the user

Arguments:

    AdapterExtension - Adapter extension for which query was passed

    IoctlDescriptor - pointer IOCTL to be filled in

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

NTSTATUS

    STATUS_SUCCESS
    Any other NTSTATUS

--*/

{
    NTSTATUS Status;
    ULONG BufferSize;
    ULONG Index, Index2;
    PVIRTUAL_MINIPORT_ADAPTER_DETAILS AdapterDetails;

    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || IoctlDescriptor == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    //
    // Fill the results to user
    //
    if ( VMLockAcquireShared(&(AdapterExtension->AdapterLock)) == TRUE ) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        BufferSize = sizeof(GUID) +(sizeof(UCHAR) * (AdapterExtension->BusCount));
        if ( BufferSize <= IoctlDescriptor->SrbIoControl.Length ) {

            AdapterDetails = &(IoctlDescriptor->RequestResponse.AdapterDetails);
            RtlZeroMemory(&(IoctlDescriptor->RequestResponse),
                          BufferSize);
            RtlCopyMemory(&(AdapterDetails->AdapterId),
                          &(AdapterExtension->UniqueId),
                          sizeof(GUID));
            
            AdapterDetails->MaxBusCount = AdapterExtension->MaxBusCount;
            AdapterDetails->BusCount = AdapterExtension->BusCount;

            for ( Index = 0, Index2 = 0; Index < AdapterExtension->MaxBusCount; Index++ ) {
                if ( AdapterExtension->Buses [Index] != VIRTUAL_MINIPORT_INVALID_POINTER ) {
                    AdapterDetails->Buses [Index2] = (UCHAR) Index;
                    Index2++;
                }
            }

            Status = STATUS_SUCCESS;
        }
        VMLockReleaseShared(&(AdapterExtension->AdapterLock));
    }

Cleanup:
    return(Status);

}

NTSTATUS
VMSrbIoControlBuildBusDetails(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlDescriptor
    )

/*++

Routine Description:

    Prepares the bus details buffer if sufficient buffer is
    passed by the user

Arguments:

    AdapterExtension - adapter extension this bus belongs to

    Bus - Bus ID for which query was passed

    IoctlDescriptor - pointer IOCTL to be filled in

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

NTSTATUS

    STATUS_SUCCESS
    Any other NTSTATUS

--*/

{
    NTSTATUS Status;
    ULONG BufferSize;
    UCHAR Index, Index2;
    PVIRTUAL_MINIPORT_BUS Bus;
    PVIRTUAL_MINIPORT_BUS_DETAILS BusDetails;

    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || IoctlDescriptor == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    Status = VMBusQueryById(AdapterExtension,
                            BusId,
                            &Bus,
                            FALSE);

    if ( NT_SUCCESS(Status) ) {
        if ( VMLockAcquireShared(&(AdapterExtension->AdapterLock)) == TRUE ) {
            if ( VMLockAcquireShared(&(Bus->BusLock)) == TRUE ) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                BufferSize = sizeof(GUID) +(sizeof(UCHAR) * (Bus->TargetCount));
                if ( BufferSize <= IoctlDescriptor->SrbIoControl.Length ) {

                    BusDetails = &(IoctlDescriptor->RequestResponse.BusDetails);

                    RtlZeroMemory(&(IoctlDescriptor->RequestResponse),
                                  BufferSize);
                    RtlCopyMemory(&(BusDetails->AdapterId),
                                  &(AdapterExtension->UniqueId),
                                  sizeof(GUID));
                    
                    BusDetails->MaxTargetCount = Bus->MaxTargetCount;
                    BusDetails->TargetCount = Bus->TargetCount;

                    for ( Index = 0, Index2 = 0; Index < Bus->MaxTargetCount; Index++ ) {
                        BusDetails->Targets [Index2] = Index;
                        Index2++;
                    }
                    Status = STATUS_SUCCESS;
                }
                VMLockReleaseShared(&(Bus->BusLock));
            }
            VMLockReleaseShared(&(AdapterExtension->AdapterLock));
        }
    }
   
Cleanup:
    return(Status);
}

NTSTATUS
VMSrbIoControlBuildTargetDetails(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _In_ UCHAR TargetId,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlDescriptor
    )

/*++

Routine Description:

    Prepares the Target details buffer if sufficient buffer is
    passed by the user

Arguments:

    AdapterExtension - adapter extension this target belongs to

    BusId - Bus on which this Target resides

    TargetId - Target for which this query has to be processed

    IoctlDescriptor - pointer IOCTL to be filled in

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

NTSTATUS

    STATUS_SUCCESS
    Any other NTSTATUS

--*/

{
    NTSTATUS Status;
    ULONG BufferSize;
    UCHAR Index, Index2;
    PVIRTUAL_MINIPORT_BUS Bus;
    PVIRTUAL_MINIPORT_TARGET Target;
    PVIRTUAL_MINIPORT_TARGET_DETAILS TargetDetails;

    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || IoctlDescriptor == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    Status = VMBusQueryById(AdapterExtension,
                            BusId,
                            &Bus,
                            FALSE);
    if ( NT_SUCCESS(Status) ) {
        Status = VMTargetQueryById(AdapterExtension,
                                   Bus,
                                   TargetId,
                                   &Target,
                                   FALSE);
        if ( NT_SUCCESS(Status) ) {

            //
            // At this time, we have Adapter, Bus, Target; Acquire the locks in order
            // and build the response to ioctl.
            //
            if ( VMLockAcquireShared(&(AdapterExtension->AdapterLock)) == TRUE ) {
                if ( VMLockAcquireShared(&(Bus->BusLock)) == TRUE ) {
                    if ( VMLockAcquireShared(&(Target->TargetLock)) == TRUE ) {

                        Status = STATUS_INSUFFICIENT_RESOURCES;
                        BufferSize = sizeof(VIRTUAL_MINIPORT_TARGET_DETAILS) + (sizeof(UCHAR) *Target->LunCount);
                        if ( BufferSize <= IoctlDescriptor->SrbIoControl.Length ) {

                            TargetDetails = &(IoctlDescriptor->RequestResponse.TargetDetails);

                            RtlZeroMemory(&(IoctlDescriptor->RequestResponse),
                                          BufferSize);
                            RtlCopyMemory(&(TargetDetails->AdapterId),
                                          &(AdapterExtension->UniqueId),
                                          sizeof(GUID));

                            TargetDetails->Bus = BusId;
                            TargetDetails->Target = TargetId;
                            TargetDetails->MaxLunCount = Target->MaxLunCount;
                            TargetDetails->LunCount = Target->LunCount;
                            for ( Index = 0, Index2 = 0; Index < Target->MaxLunCount; Index++ ) {
                                if ( Target->Luns [Index] != VIRTUAL_MINIPORT_INVALID_POINTER ) {
                                    TargetDetails->Luns [Index2] = Index;
                                    Index2++;
                                }
                            }
                            Status = VMDeviceBuildPhysicalDeviceDetails(&(Target->Device),
                                                                        &(TargetDetails->DeviceDetails));
                        }
                        VMLockReleaseShared(&(Target->TargetLock));
                    }
                    VMLockReleaseShared(&(Bus->BusLock));
                }
                VMLockReleaseShared(&(AdapterExtension->AdapterLock));
            }
        }
    }

Cleanup:
    return(Status);
}

NTSTATUS
VMSrbIoControlBuildLunDetails(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _In_ UCHAR TargetId,
    _In_ UCHAR LunId,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlDescriptor
    )

/*++

Routine Description:

    Prepares the Lun details buffer if sufficient buffer is
    passed by the user

Arguments:

    AdapterExtension - adapter extension this target belongs to

    BusId - Bus on which this Lun resides

    TargetId - Target which owns this Lun

    LunId - Lun for which we need to fetch the details

    IoctlDescriptor - pointer IOCTL to be filled in

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

NTSTATUS

    STATUS_SUCCESS
    Any other NTSTATUS

--*/

{
    NTSTATUS Status;
    PVIRTUAL_MINIPORT_BUS Bus;
    PVIRTUAL_MINIPORT_TARGET Target;
    PVIRTUAL_MINIPORT_LUN Lun;
    ULONG BufferSize;
    PVIRTUAL_MINIPORT_LUN_DETAILS LunDetails;

    Status = STATUS_UNSUCCESSFUL;

    if ( AdapterExtension == NULL || IoctlDescriptor == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }


    Status = VMBusQueryById(AdapterExtension,
                            BusId,
                            &Bus,
                            FALSE);
    if ( NT_SUCCESS(Status) ) {
        Status = VMTargetQueryById(AdapterExtension,
                                   Bus,
                                   TargetId,
                                   &Target,
                                   FALSE);
        if ( NT_SUCCESS(Status) ) {
        
            Status = VMLunQueryById(AdapterExtension,
                                    Bus,
                                    Target,
                                    LunId,
                                    &Lun,
                                    FALSE);

            if ( NT_SUCCESS(Status) ) {
                //
                // We have all the pointers, now acquire the locks in order
                //
                if ( VMLockAcquireShared(&(AdapterExtension->AdapterLock)) == TRUE ) {
                    if ( VMLockAcquireShared(&(Bus->BusLock)) == TRUE ) {
                        if ( VMLockAcquireShared(&(Target->TargetLock)) == TRUE ) {
                            if ( VMLockAcquireShared(&(Lun->LunLock)) == TRUE ) {

                                //
                                // All the component level locks are acquired in order.
                                // Now build the lun details
                                //
                                Status = STATUS_INSUFFICIENT_RESOURCES;
                                BufferSize = sizeof(VIRTUAL_MINIPORT_LUN_DETAILS);
                                if ( BufferSize < IoctlDescriptor->SrbIoControl.Length ) {

                                    LunDetails = &(IoctlDescriptor->RequestResponse.LunDetails);
                                    RtlZeroMemory(&(IoctlDescriptor->RequestResponse),
                                                  BufferSize);
                                    RtlCopyMemory(&(LunDetails->AdapterId),
                                                  &(AdapterExtension->UniqueId),
                                                  sizeof(GUID));
                                    LunDetails->Bus = BusId;
                                    LunDetails->Target = TargetId;
                                    LunDetails->Lun = LunId;
                                    Status = VMDeviceBuildLogicalDeviceDetails(&(Lun->Device),
                                                                               &(LunDetails->DeviceDetails));
                                }
                                VMLockReleaseShared(&(Lun->LunLock));
                            }
                            VMLockReleaseShared(&(Target->TargetLock));
                        }
                        VMLockReleaseShared(&(Bus->BusLock));
                    }
                    VMLockReleaseShared(&(AdapterExtension->AdapterLock));
                }           
            } // Lun
        } // Target
    } // Bus
Cleanup:
    return(Status);

}