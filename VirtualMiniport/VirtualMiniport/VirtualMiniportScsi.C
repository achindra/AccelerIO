/*++

Module Name:

    scsi.c

Date:

    4-Jan-2014

Abstract:

    This file implements the scsi commands.

--*/


//
// Headers
//

#include <VirtualMiniportScsi.h>
#include <VirtualMiniportDevice.h>
#include <VirtualMiniportBus.h>
#include <VirtualMiniportAdapter.h>
#include <VirtualMiniportTarget.h>
#include <VirtualMiniportLun.h>

//
// WPP based event trace
//

#include <VirtualMiniportScsi.tmh>

//
// Forward declarations of private functions
//

static
NTSTATUS
VMSrbExecuteScsilWorker(
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ BOOLEAN AbortRequest
    );

static
NTSTATUS
VMSrbExecuteScsiNop(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    );

static
NTSTATUS
VMSrbBuildSenseBuffer(
    _Inout_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR ScsiStatus,
    _In_ UCHAR SenseKey,
    _In_ UCHAR AdditionalSenseCode,
    _In_ UCHAR AdditionalSenseCodeQualifier
    );

static
UCHAR
VMSrbExecuteScsiReportLuns(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    );

static
UCHAR
VMSrbExecuteScsiInquiry(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    );

static
UCHAR
VMSrbExecuteScsiReadCapacity(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    );

static
UCHAR
VMSrbExecuteScsiModeSense(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    );

static
UCHAR
VMSrbExecuteScsiReadWrite(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    );

//
// Define the attributes of functions; declarations are in module
// specific header
//

#pragma alloc_text(NONPAGED, VMSrbExecuteScsi)

#pragma alloc_text(NONPAGED, VMSrbExecuteScsilWorker)

#pragma alloc_text(PAGED, VMSrbExecuteScsiNop)
#pragma alloc_text(PAGED, VMSrbBuildSenseBuffer)
#pragma alloc_text(PAGED, VMSrbExecuteScsiReportLuns)
#pragma alloc_text(PAGED, VMSrbExecuteScsiInquiry)
#pragma alloc_text(PAGED, VMSrbExecuteScsiReadCapacity)
#pragma alloc_text(PAGED, VMSrbExecuteScsiModeSense)
#pragma alloc_text(PAGED, VMSrbExecuteScsiReadWrite)

//
// Driver specific routines
//

BOOLEAN
VMSrbExecuteScsi(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    Handles the SRB_FUNCTION_EXECUTE_SCSI from the user

Arguments:

    AdapterExtension - Adapter to which this request is directed to

    Srb - SCSI_REQUEST_BLOCK for executeing SCSI commands

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
    NTSTATUS NtStatus;
    PVIRTUAL_MINIPORT_SRB_EXTENSION SrbExtension;
    PCDB Cdb;

    Status = FALSE;
    CompleteRequest = FALSE;
    Queued = FALSE;

    //
    // We own the SRB extension
    //

    SrbExtension = Srb->SrbExtension;
    SrbExtension->Adapter = AdapterExtension;
    SrbExtension->Srb = Srb;
    Cdb = (PCDB) Srb->Cdb;

    NtStatus = VMSchedulerInitializeWorkItem((PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM) SrbExtension,
                                             VMSchedulerHintDefault,
                                             VMSrbExecuteScsilWorker);
    if ( !NT_SUCCESS(NtStatus) ) {
        Status = FALSE;
        goto Cleanup;
    }



    //
    // Switch on the SCSI function. We switch here in case there are any function codes that
    // need to be handles straight here than in worker.
    //
    switch ( Cdb->CDB6GENERIC.OperationCode ) {

        //
        // Directly handle SCSI opcode. Anything handled synchronously here will
        // have to mark CompleteRequest to TRUE so that request is completed later
        // in this same function.
        //  1. Mark CompleteRequest to TRUE
        //  2. Set Srb->SrbStatus to appropriate SCSI status
        //

    default:

        //
        // Dispatch all the functions to worker thread. 
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
            VM_TRACE_SCSI,
            "[%s]:AdpaterExtension:%p, Srb:%p, CdbOpCode:0x%02x, CdbLength:0x%0x, Status:%!bool!, Queued:%!bool!, Srb:%!SRB!",
            __FUNCTION__,
            AdapterExtension,
            Srb,
            Cdb->CDB6GENERIC.OperationCode,
            Srb->CdbLength,
            Status,
            Queued,
            Srb);

    return(Status);
}

static
NTSTATUS
VMSrbExecuteScsilWorker(
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ BOOLEAN Abort
    )

/*++

Routine Description:

    Handles SRB_FUNCTION_EXECUTE_SCSI offline

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
    PSCSI_REQUEST_BLOCK Srb;
    UCHAR SrbStatus;
    PCDB Cdb;


    Status = STATUS_SUCCESS;
    SrbExtension = (PVIRTUAL_MINIPORT_SRB_EXTENSION) WorkItem;
    Srb = SrbExtension->Srb;
    Cdb = (PCDB)Srb->Cdb;

    if ( Abort == TRUE ) {
        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_IOCTL,
                "[%s]:Srb:%p Aborting",
                __FUNCTION__,
                Srb);

        SrbStatus = SRB_STATUS_ABORTED;
        Srb->DataTransferLength = 0;
        goto CompleteRequest;
    }

    //
    // Validate the request destination
    //
    SrbStatus = VMDeviceValidateAddress(SrbExtension->Adapter,
                                        Srb->PathId,
                                        Srb->TargetId,
                                        Srb->Lun);

    if ( SrbStatus != SRB_STATUS_SUCCESS ) {
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_SCSI,
                "[%s]:AdapterExtension:%p, Srb:%p path invalid [%02d:%02d:%02d], Status:0x%08x",
                __FUNCTION__,
                SrbExtension->Adapter,
                Srb,
                Srb->PathId,
                Srb->TargetId,
                Srb->Lun,
                SrbStatus);
        if ( Cdb->CDB6GENERIC.OperationCode != SCSIOP_REPORT_LUNS ) {
            goto CompleteRequest;
        }

        if ( SrbStatus == SRB_STATUS_NO_DEVICE ||
             SrbStatus == SRB_STATUS_NO_HBA ||
             SrbStatus == SRB_STATUS_INVALID_PATH_ID ||
             SrbStatus == SRB_STATUS_INVALID_TARGET_ID ) {
            goto CompleteRequest;
        }
    }


    //
    // SCSI opcodes are handled here case by case basis.
    //
    switch ( Cdb->CDB6GENERIC.OperationCode ) {

    case SCSIOP_REPORT_LUNS:
        SrbStatus = VMSrbExecuteScsiReportLuns(SrbExtension->Adapter,
                                               Srb);
        break;

    case SCSIOP_INQUIRY:
        SrbStatus = VMSrbExecuteScsiInquiry(SrbExtension->Adapter,
                                            Srb);
        break;

    case SCSIOP_READ_CAPACITY16:
    case SCSIOP_READ_CAPACITY:
        SrbStatus = VMSrbExecuteScsiReadCapacity(SrbExtension->Adapter,
                                                 Srb);
        break;

    case SCSIOP_MODE_SENSE:
    case SCSIOP_MODE_SENSE10:
        SrbStatus = VMSrbExecuteScsiModeSense(SrbExtension->Adapter,
                                              Srb);
        break;

    case SCSIOP_READ:
    case SCSIOP_WRITE:
        SrbStatus = VMSrbExecuteScsiReadWrite(SrbExtension->Adapter,
                                              Srb);
        break;

    default:

        //
        // All other unhandled request are treated invalid request
        //
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;
    
    }

CompleteRequest:

    //
    // We can have the auto sense valid along with other status. SCSI handlers will
    // update the sense data if applicable. So we need to slap the SrbStatus onto
    // existing status. This is the only place where we update the SrbStatus to Srb
    // except the auto sense valid
    //

    Srb->SrbStatus |= SrbStatus;

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_SCSI,
            "[%s]:AdpaterExtension:%p, Srb:%p, CdbOpCode:0x%02x, CdbLength:0x%0x, SrbStatus:0x%08x, Srb:%!SRB!",
            __FUNCTION__,
            SrbExtension->Adapter,
            Srb,
            Cdb->CDB6GENERIC.OperationCode,
            Srb->CdbLength,
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

static
NTSTATUS
VMSrbExecuteScsiNop(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    )

/*
Routine Description:

    Processes the SCSI requests that we intent to treat as No-Op. Commands
    that need to just do no-op will wrap this under the command specific
    routines

Arguments:

    AdapterExtension - Adapter extension

    Srb - Srb to process

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    NTSTATUS
    
*/

{
    NTSTATUS Status;
    PVIRTUAL_MINIPORT_LUN_EXTENSION LunExtension;
    PVOID DataBuffer;
    ULONG StorStatus;

    Status = STATUS_UNSUCCESSFUL;
    LunExtension = NULL;
    DataBuffer = NULL;

    LunExtension = StorPortGetLogicalUnit(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun);
    if ( LunExtension == NULL ) {
        goto Cleanup;
    }

    StorStatus = StorPortGetSystemAddress(AdapterExtension, Srb, &DataBuffer);
    if ( StorStatus != STOR_STATUS_SUCCESS ) {
        goto Cleanup;
    }

    RtlZeroMemory(Srb->DataBuffer, Srb->DataTransferLength);
    Srb->DataTransferLength = 0;
    Srb->ScsiStatus = SCSISTAT_GOOD;
    Status = STATUS_SUCCESS;

Cleanup:
    return(Status);
}

static
NTSTATUS
VMSrbBuildSenseBuffer(
    _Inout_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR ScsiStatus,
    _In_ UCHAR SenseKey,
    _In_ UCHAR AdditionalSenseCode,
    _In_ UCHAR AdditionalSenseCodeQualifier
    )

/*

Routine Description:

    Constructs the sense buffer

Arguments:

    Srb - Srb to process

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    NTSTATUS

*/

{
    NTSTATUS Status;
    PSENSE_DATA SenseData;

    Status = STATUS_UNSUCCESSFUL;
    SenseData = Srb->SenseInfoBuffer;

    if ( SenseData != NULL && Srb->SenseInfoBufferLength != 0  && !(Srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE)) {
        RtlZeroMemory(SenseData, sizeof(SENSE_DATA));
        SenseData->SenseKey = SenseKey;
        SenseData->AdditionalSenseCode = AdditionalSenseCode;
        SenseData->AdditionalSenseCodeQualifier = AdditionalSenseCodeQualifier;
        SenseData->Valid = TRUE;
        Srb->ScsiStatus = ScsiStatus;
        Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
        Status = STATUS_SUCCESS;
    }

    return(Status);
}

static
UCHAR
VMSrbExecuteScsiReportLuns(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    Handles SCSIOP_REPORT_LUNS

Arguments:

    AdapterExtension - Adapter to which this request is queued

    Srb - Srb to process

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    SRB_STATUS_XXX

--*/

{
    UCHAR SrbStatus;
    ULONG BufferLength, LunListLength;
    PLUN_LIST LunList;
    PVIRTUAL_MINIPORT_TARGET Target;
    UCHAR BusId, TargetId, LunId, LunListIndex;
    PVIRTUAL_MINIPORT_LUN_EXTENSION LunExtension;
    PVOID DataBuffer;
    
    SrbStatus = SRB_STATUS_ERROR;
    Target = NULL;
    BusId = 0;
    TargetId = 0;
    LunListIndex = 0;
    LunExtension = NULL;
    DataBuffer = NULL;

    SrbStatus = VMDeviceFindDeviceByAddress(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun, VMTypeTarget, &Target);
    if ( SrbStatus != SRB_STATUS_SUCCESS ) {
        goto Cleanup;
    }

    LunExtension = StorPortGetLogicalUnit(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun);
    if ( LunExtension == NULL ) {
        SrbStatus = SRB_STATUS_NO_DEVICE;
        goto Cleanup;
    }

    if ( StorPortGetSystemAddress(AdapterExtension, Srb, &DataBuffer) != STOR_STATUS_SUCCESS ) {
        SrbStatus = SRB_STATUS_ERROR;
        goto Cleanup;
    }

    if ( VMLockAcquireShared(&(Target->TargetLock)) == TRUE ) {

        RtlZeroMemory(DataBuffer, Srb->DataTransferLength);
        LunList = DataBuffer;

        //
        // Note that we only report buffer length for actual Luncount
        // and not MaxLunCount.
        //
        BufferLength = sizeof(LUN_LIST) + (8 * Target->LunCount);
        
        if ( Srb->DataTransferLength >= sizeof(ULONG) ) {
            //
            // Byte swap the length
            //
            LunListLength = (8 * Target->LunCount);
            *((PULONG) &(LunList->LunListLength)) = _byteswap_ulong(LunListLength);
        }

        if ( Srb->DataTransferLength >= BufferLength ) {
            for ( LunId = 0, LunListIndex = 0; LunId < Target->MaxLunCount; LunId++ ) {
                if ( Target->Luns [LunId] != VIRTUAL_MINIPORT_INVALID_POINTER ) {
                    LunList->Lun [LunListIndex][0] = 0;
                    LunList->Lun [LunListIndex][1] = LunId;
                    LunList->Lun [LunListIndex][2] = 0;
                    LunList->Lun [LunListIndex][3] = 0;
                    LunList->Lun [LunListIndex][4] = 0;
                    LunList->Lun [LunListIndex][5] = 0;
                    LunList->Lun [LunListIndex][6] = 0;
                    LunList->Lun [LunListIndex][7] = 0;
                    LunListIndex++;
                }

                //
                // If we managed to fill in the Luns, then succeed the SRB
                //
                Srb->DataTransferLength = BufferLength;
                Srb->ScsiStatus = SCSISTAT_GOOD;
                SrbStatus = SRB_STATUS_SUCCESS;
            }
        }
        VMLockReleaseShared(&(Target->TargetLock));
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_SCSI,
            "[%s]:AdapterExtension:%p, Target:%p [%02d.%02d], Srb:%p, Reported lun count:%d, SrbStatus:0x%08x",
            __FUNCTION__,
            AdapterExtension,
            Target,
            BusId,
            TargetId,
            Srb,
            LunListIndex,
            SrbStatus);
    return(SrbStatus);
}

static
UCHAR
VMSrbExecuteScsiInquiry(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    Handles SCSIOP_INQUIRY

Arguments:

    AdapterExtension - Adapter to which this request is queued

    Srb - Srb to process

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    SRB_STATUS_XXX

--*/

{
    UCHAR SrbStatus;
    PINQUIRYDATA InquiryData;
    PVIRTUAL_MINIPORT_CONFIGURATION Configuration;
    PVIRTUAL_MINIPORT_LUN Lun;
    PCDB Cdb;
    PVIRTUAL_MINIPORT_LUN_EXTENSION LunExtension;
    PVOID DataBuffer;

    SrbStatus = SRB_STATUS_ERROR;
    Lun = NULL;
    Cdb = (PCDB)Srb->Cdb;
    Configuration = &AdapterExtension->DeviceExtension->Configuration;
    LunExtension = NULL;
    DataBuffer = NULL;

    SrbStatus = VMDeviceFindDeviceByAddress(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun, VMTypeLun, &Lun);
    if ( SrbStatus != SRB_STATUS_SUCCESS ) {
        Srb->DataTransferLength = 0;
        goto Cleanup;
    }

    LunExtension = StorPortGetLogicalUnit(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun);
    if ( LunExtension == NULL ) {
        SrbStatus = SRB_STATUS_NO_DEVICE;
        goto Cleanup;
    }

    if ( StorPortGetSystemAddress(AdapterExtension, Srb, &DataBuffer) != STOR_STATUS_SUCCESS ) {
        SrbStatus = SRB_STATUS_ERROR;
        goto Cleanup;
    }
   
    RtlZeroMemory(DataBuffer, Srb->DataTransferLength);
    InquiryData = DataBuffer;

    if ( Cdb->CDB6INQUIRY3.EnableVitalProductData != TRUE ) {
        
        //
        // Standard inquiry
        //
        if ( Srb->DataTransferLength < sizeof(INQUIRYDATABUFFERSIZE) ) {

            Srb->DataTransferLength = 0;
            SrbStatus = SRB_STATUS_DATA_OVERRUN;
            goto Cleanup;
        }

        if ( Cdb->CDB6INQUIRY3.PageCode != 0 ) {

            //
            // Check condition, Illigeal request, Invalid Field in CDB
            //
            VMSrbBuildSenseBuffer(Srb, SCSISTAT_CHECK_CONDITION, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
            SrbStatus = SRB_STATUS_ERROR;
            goto Cleanup;
        }

        InquiryData->DeviceType = DIRECT_ACCESS_DEVICE;
        InquiryData->DeviceTypeQualifier = DEVICE_CONNECTED;
        InquiryData->DeviceTypeModifier = 0;
        InquiryData->RemovableMedia = FALSE;
        InquiryData->Versions = 4;

        RtlCopyMemory(InquiryData->VendorId, Configuration->AnsiVendorID.Buffer, VIRTUAL_MINIPORT_ID_STRING_LENGTH);
        RtlCopyMemory(InquiryData->ProductId, Configuration->AnsiProductID.Buffer, VIRTUAL_MINIPORT_ID_STRING_LENGTH);
        RtlCopyMemory(InquiryData->ProductRevisionLevel, Configuration->AnsiProductRevision.Buffer, VIRTUAL_MINIPORT_PRODUCT_REVISION_STRING_LENGTH);
    
    } else {

        //
        // Page specific inquiry
        //
    
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_SCSI,
            "[%s]:AdapterExtension:%p, [%02d.%02d.%02d]Lun:%p, Srb:%p, SrbStatus:0x%08x, ScsiStatus:0x%08x",
            __FUNCTION__,
            AdapterExtension,
            Srb->PathId,
            Srb->TargetId,
            Srb->Lun,
            Lun,
            Srb,
            SrbStatus,
            Srb->ScsiStatus);

    return(SrbStatus);
}

static
UCHAR
VMSrbExecuteScsiReadCapacity(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    Handles SCSIOP_READ_CAPACITY

Arguments:

    AdapterExtension - Adapter to which this request is queued

    Srb - Srb to process

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    SRB_STATUS_XXX

--*/

{
    UCHAR SrbStatus;
    NTSTATUS Status;
    PVIRTUAL_MINIPORT_LUN Lun;
    VIRTUAL_MINIPORT_LOGICAL_DEVICE_DETAILS LogicalDeviceDetails;
    PREAD_CAPACITY_DATA ReadCapacity;
    PREAD_CAPACITY_DATA_EX ReadCapacityEx;
    PCDB Cdb;
    PVIRTUAL_MINIPORT_LUN_EXTENSION LunExtension;
    PVOID DataBuffer;

    SrbStatus = SRB_STATUS_ERROR;
    Lun = NULL;
    Cdb = (PCDB) Srb->Cdb;
    LunExtension = NULL;
    DataBuffer = NULL;

    RtlZeroMemory(&LogicalDeviceDetails, sizeof(VIRTUAL_MINIPORT_LOGICAL_DEVICE_DETAILS));

    SrbStatus = VMDeviceFindDeviceByAddress(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun, VMTypeLun, &Lun);
    if ( SrbStatus != SRB_STATUS_SUCCESS ) {
        goto Cleanup;
    }

    LunExtension = StorPortGetLogicalUnit(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun);
    if ( LunExtension == NULL ) {
        SrbStatus = SRB_STATUS_NO_DEVICE;
        goto Cleanup;
    }

    if ( StorPortGetSystemAddress(AdapterExtension, Srb, &DataBuffer) != STOR_STATUS_SUCCESS ) {
        SrbStatus = SRB_STATUS_ERROR;
        goto Cleanup;
    }

    if ( VMLockAcquireShared(&(Lun->LunLock)) == TRUE ) {
 
        Status = VMDeviceBuildLogicalDeviceDetails(&(Lun->Device), &LogicalDeviceDetails);
         if ( NT_SUCCESS(Status) ) {
            SrbStatus = SRB_STATUS_SUCCESS;
        }
        VMLockReleaseShared(&(Lun->LunLock));
    }
    
    if ( SrbStatus == SRB_STATUS_SUCCESS ) {

        //
        // Check the type of read capacity type
        //
        if ( Cdb->CDB6GENERIC.OperationCode == SCSIOP_READ_CAPACITY ) {

            ReadCapacity = DataBuffer;
            ReadCapacity->BytesPerBlock = _byteswap_ulong(LogicalDeviceDetails.BlockSize);

            if ( LogicalDeviceDetails.MaxBlocks > 0xFFFFFFFFULL ) {
                ReadCapacity->LogicalBlockAddress = _byteswap_ulong(~0x0ULL);
            } else {
                ReadCapacity->LogicalBlockAddress = _byteswap_ulong((ULONG) (LogicalDeviceDetails.MaxBlocks & 0xFFFFFFFFULL));
            }

            Srb->ScsiStatus = SCSISTAT_GOOD;
            SrbStatus = SRB_STATUS_SUCCESS;
        } else {

            ReadCapacityEx = DataBuffer;
            ReadCapacityEx->BytesPerBlock = _byteswap_ulong(LogicalDeviceDetails.BlockSize);
            ReadCapacityEx->LogicalBlockAddress.QuadPart = _byteswap_uint64(LogicalDeviceDetails.MaxBlocks);

            Srb->ScsiStatus = SCSISTAT_GOOD;
            SrbStatus = SRB_STATUS_SUCCESS;
        }     
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_SCSI,
            "[%s]:AdapterExtension:%p, [%02d.%02d.%02d]Lun:%p, Srb:%p, SrbStatus:0x%08x, ScsiStatus:0x%08x",
            __FUNCTION__,
            AdapterExtension,
            Srb->PathId,
            Srb->TargetId,
            Srb->Lun,
            Lun,
            Srb,
            SrbStatus,
            Srb->ScsiStatus);
    return(SrbStatus);
}

static
UCHAR
VMSrbExecuteScsiModeSense(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    Handles SCSIOP_MODESENSE

Arguments:

    AdapterExtension - Adapter to which this request is queued

    Srb - Srb to process

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    SRB_STATUS_XXX

--*/

{
    UCHAR SrbStatus;
    NTSTATUS Status;
    PVIRTUAL_MINIPORT_LUN Lun;

    SrbStatus = SRB_STATUS_ERROR;
    Status = STATUS_UNSUCCESSFUL;
    Lun = NULL;

    SrbStatus = VMDeviceFindDeviceByAddress(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun, VMTypeLun, &Lun);
    if ( SrbStatus != SRB_STATUS_SUCCESS ) {
        goto Cleanup;
    }

    Status = VMSrbExecuteScsiNop(AdapterExtension, Srb);
    if ( NT_SUCCESS(Status) ) {
        SrbStatus = SRB_STATUS_SUCCESS;
    } else {
        SrbStatus = SRB_STATUS_ERROR;
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_SCSI,
            "[%s]:AdapterExtension:%p, [%02d.%02d.%02d]Lun:%p, Srb:%p, SrbStatus:0x%08x, ScsiStatus:0x%08x",
            __FUNCTION__,
            AdapterExtension,
            Srb->PathId,
            Srb->TargetId,
            Srb->Lun,
            Lun,
            Srb,
            SrbStatus,
            Srb->ScsiStatus);
    return(SrbStatus);
}

static
UCHAR
VMSrbExecuteScsiReadWrite(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PSCSI_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    Handles SCSIOP_READ(X)/SCSIOP_WRITE(X)

Arguments:

    AdapterExtension - Adapter to which this request is queued

    Srb - Srb to process

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    SRB_STATUS_XXX

--*/

{
    UCHAR SrbStatus;
    UCHAR ScsiStatus;
    NTSTATUS Status;
    PVIRTUAL_MINIPORT_LUN Lun;
    PCDB Cdb;
    BOOLEAN Read;
    ULONGLONG LogicalBlockNumber;
    ULONG BlockCount;
    ULONG TransferredBytes;
    PVIRTUAL_MINIPORT_LUN_EXTENSION LunExtension;
    PVOID DataBuffer;


    SrbStatus = SRB_STATUS_ERROR;
    ScsiStatus = SCSISTAT_CHECK_CONDITION;
    Status = STATUS_UNSUCCESSFUL;   
    Lun = NULL;
    Cdb = (PCDB)Srb->Cdb;
    Read = TRUE;
    LogicalBlockNumber = 0;
    BlockCount = 0;
    TransferredBytes = 0;
    LunExtension = NULL;
    DataBuffer = NULL;

    SrbStatus = VMDeviceFindDeviceByAddress(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun, VMTypeLun, &Lun);
    if ( SrbStatus != SRB_STATUS_SUCCESS ) {
        goto Cleanup;
    }

    LunExtension = StorPortGetLogicalUnit(AdapterExtension, Srb->PathId, Srb->TargetId, Srb->Lun);
    if ( LunExtension == NULL ) {
        SrbStatus = SRB_STATUS_NO_DEVICE;
        goto Cleanup;
    }
    
    if ( StorPortGetSystemAddress(AdapterExtension, Srb, &DataBuffer) != STOR_STATUS_SUCCESS ) {
        SrbStatus = SRB_STATUS_ERROR;
        goto Cleanup;
    }

    switch ( Cdb->CDB10.OperationCode ) {
    case SCSIOP_READ:
        Read = TRUE;
        break;

    case SCSIOP_WRITE:
        Read = FALSE;
        break;
    }

    LogicalBlockNumber = Cdb->CDB10.LogicalBlockByte0 << 24 |
                         Cdb->CDB10.LogicalBlockByte1 << 16 |
                         Cdb->CDB10.LogicalBlockByte2 << 8 |
                         Cdb->CDB10.LogicalBlockByte3;                       
    BlockCount = Cdb->CDB10.TransferBlocksLsb | Cdb->CDB10.TransferBlocksMsb << 8;

    Status = VMDeviceReadWriteLogicalDevice(AdapterExtension,
                                            &Lun->Device,
                                            Read,
                                            DataBuffer,
                                            LogicalBlockNumber,
                                            BlockCount,
                                            &TransferredBytes);
    Srb->DataTransferLength = TransferredBytes;
    if ( NT_SUCCESS(Status) ) {
        
        SrbStatus = SRB_STATUS_SUCCESS;
    } else {

        SrbStatus = SRB_STATUS_ERROR;
        switch ( Status ) {
        case STATUS_RANGE_NOT_FOUND:
        case STATUS_DISK_FULL:
            VMSrbBuildSenseBuffer(Srb, SCSISTAT_CHECK_CONDITION, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_BLOCK, 0);
            break;
        
        case STATUS_INSUFFICIENT_RESOURCES:
            VMSrbBuildSenseBuffer(Srb, SCSISTAT_CHECK_CONDITION, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_RESOURCE_FAILURE, 0);
            break;

        default:
            VMSrbBuildSenseBuffer(Srb, SCSISTAT_CHECK_CONDITION, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_DATA_TRANSFER_ERROR, 0);
            break;
        }       
    }

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_SCSI,
            "[%s]:AdapterExtension:%p, [%02d.%02d.%02d]Lun:%p, Srb:%p, SrbStatus:0x%08x, ScsiStatus:0x%08x, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Srb->PathId,
            Srb->TargetId,
            Srb->Lun,
            Lun,
            Srb,
            SrbStatus,
            Srb->ScsiStatus,
            Status);
    return(SrbStatus);
}