/*++

Module Name:

    VirtualMiniportPnp.C

Date:

    7-Mar-2014

Abstract:

    Module implements PnP requests
    All routines start with VM - for 'V'irtual 'M'iniport

--*/

//
// Headers
//

#include <VirtualMiniportPnp.h>
#include <VirtualMiniportAdapter.h>

//
// WPP based event trace
//

#include <VirtualMiniportPnp.tmh>

//
// Forward declarations of private functions
//

NTSTATUS
VMSrbPnpWorker(
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ BOOLEAN AbortRequest
    );


//
// Module specific globals
//

//
// Define the attributes of functions; declarations are in module
// specific header
//

#pragma alloc_text(NONPAGED, VMSrbPnp)

#pragma alloc_text(NONPAGED, VMSrbPnpWorker)

//
// Driver specific routines
//

BOOLEAN
VMSrbPnp(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PSCSI_PNP_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    Entry point for PnP SCSI requests. The reason this routine exists is
    to ensure we can evaluate if there are any functions that should be
    handled immediately or should be delegated to a worker.

Arguments:

    AdapterExtension - Adapter extension for the adapter on which
    this request was issued

    Srb - SRB for the PNP request

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    Return value is BOOLEAN as the caller just needs to know if
    the request was accepted or rejected.

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;
    BOOLEAN CompleteRequest;
    BOOLEAN Queued;
    NTSTATUS NtStatus;
    PVIRTUAL_MINIPORT_SRB_EXTENSION SrbExtension;

    Status = FALSE;
    CompleteRequest = FALSE;
    Queued = FALSE;

    //
    // We own the SRB extension
    //

    SrbExtension = Srb->SrbExtension;
    SrbExtension->Adapter = AdapterExtension;
    //
    // Unlike other SCSI handling routines, this routine used PnP SRB request.
    //
    SrbExtension->Srb = (PSCSI_REQUEST_BLOCK) Srb;

    NtStatus = VMSchedulerInitializeWorkItem((PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM)SrbExtension,
                                             VMSchedulerHintDefault,
                                             VMSrbPnpWorker);
    if ( !NT_SUCCESS(NtStatus) ) {
        Status = FALSE;
        goto Cleanup;
    }

    //
    // Switch on the Pnp action. We switch here in case there are any function codes that
    // need to be handles straight here than in worker.
    //
    switch ( Srb->PnPAction ) {

        //
        // Directly handles Pnp function codes. Anything handled synchronously here
        // will have to mark CompleteRequest to TRUE so that request is complete later
        // in this same function.
        //  1. Mark CompleteRequest to TRUE
        //  2. Set Srb->SrbStatus to appropriate SCSI status
        //

    default:

        //
        // Dispatch all the Pnp actions to worker thread. 
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
            VM_TRACE_PNP,
            "[%s]:AdpaterExtension:%p, PnpSrb:%p, PnpAction:%!STORPNPACTION!, Status:%!bool!, Queued:%!bool!",
            __FUNCTION__,
            AdapterExtension,
            Srb,
            Srb->PnPAction,
            Status,
            Queued);

    return(Status);
}

NTSTATUS
VMSrbPnpWorker(
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ BOOLEAN Abort
    )

/*++

Routine Description:

    Pnp delayed worker. Any work that we could afford to do at passive
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
    PVIRTUAL_MINIPORT_SRB_EXTENSION SrbExtension;
    PSCSI_PNP_REQUEST_BLOCK PnpSrb;

    Status = STATUS_SUCCESS;
    SrbExtension = (PVIRTUAL_MINIPORT_SRB_EXTENSION)WorkItem;

    //
    // It is important to refer to PnpSrb as SRB in routine. Do not refer to Srb from the
    // SrbExtension which is simply a PSCSI_REQUEST_BLOCK!!!
    //
    PnpSrb = (PSCSI_PNP_REQUEST_BLOCK)SrbExtension->Srb;

    if ( Abort == TRUE ) {

        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_PNP,
                "[%s]:Srb:%p Aborting",
                __FUNCTION__,
                PnpSrb);

        PnpSrb->SrbStatus = SRB_STATUS_ABORTED;
        goto CompleteRequest;
    }

    switch ( PnpSrb->PnPAction ) {
    case StorRemoveDevice:
        PnpSrb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    default:
        PnpSrb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;
    }

CompleteRequest:

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_PNP,
            "[%s]:AdpaterExtension:%p, PnpSrb:%p, PnpAction:%!STORPNPACTION!, SrbStatus:0x%08x",
            __FUNCTION__,
            SrbExtension->Adapter,
            PnpSrb,
            PnpSrb->PnPAction,
            PnpSrb->SrbStatus);

    //
    // Now that we are done with the request, complete the request
    //
    StorPortNotification(RequestComplete,
                         SrbExtension->Adapter,
                         PnpSrb);
    return(Status);
}