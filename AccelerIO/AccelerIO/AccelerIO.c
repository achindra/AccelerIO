#include <AccelerIO.h>
#include "AccelerIO.tmh"

#pragma warning(disable: 4100) // Unreferenced Parameters
#pragma warning(disable: 4152) // AioVmpFindAdapter

pAIO_MASTER_INFO pMasterDriverInfo = NULL;

/******************************************************************************
//
/*****************************************************************************/
_Use_decl_annotations_
ULONG DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	NTSTATUS NtStatus = STATUS_SUCCESS;
	HW_INITIALIZATION_DATA hwInitData;  // Storport will retain its copy, this will be discarded

	KdPrint(("DriverEntry Called \r\n"));

	DriverObject->DriverUnload = DriverUnload;


	pMasterDriverInfo = ExAllocatePoolWithTag(NonPagedPool, sizeof(AIO_MASTER_INFO), AIO_POOL_TAG);

	if (!pMasterDriverInfo)
	{
		NtStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto done;
	}

	RtlZeroMemory(pMasterDriverInfo, sizeof(AIO_MASTER_INFO));

	KeInitializeSpinLock(&pMasterDriverInfo->DrvInfoLock);
	KeInitializeSpinLock(&pMasterDriverInfo->MPIOExtLock);
	InitializeListHead(&pMasterDriverInfo->ListHBAObj);
	InitializeListHead(&pMasterDriverInfo->ListMPIOExt);
	pMasterDriverInfo->pDriverObj = DriverObject;

	AioQueryRegParams(RegistryPath, &pMasterDriverInfo->RegInfo);

	if (pMasterDriverInfo->RegInfo.nLUNsperHBA > LUNInfoMax)
		pMasterDriverInfo->RegInfo.nLUNsperHBA = LUNInfoMax;

	if (pMasterDriverInfo->RegInfo.VirtualDiskSize != pMasterDriverInfo->RegInfo.PhysicalDiskSize)
		pMasterDriverInfo->RegInfo.VirtualDiskSize = pMasterDriverInfo->RegInfo.PhysicalDiskSize;


	RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));

	hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

	hwInitData.AdapterInterfaceType = Internal;

	hwInitData.HwFindAdapter = AioVmpFindAdapter; // 2nd stage; PnP calls StartIO of StorPort, inturn lands here
	hwInitData.HwInitialize = AioVmpInitialize;  // 3rd stage initialization
	hwInitData.HwStartIo = AioVmpStartIo;     // 

	hwInitData.HwAdapterControl = AioVmpAdapterControl;
	hwInitData.HwResetBus = AioVmpResetBus;

	hwInitData.HwInitializeTracing = AioVmpInitTracing;
	hwInitData.HwProcessServiceRequest = AioVmpProcessServiceRequest;
	hwInitData.HwCompleteServiceIrp = AioVmpCompleteServiceIrp;

	hwInitData.HwFreeAdapterResources = AioVmpFreeAdapterResource;
	hwInitData.HwCleanupTracing = AioVmpCleanupTracing;

	hwInitData.FeatureSupport = STOR_FEATURE_VIRTUAL_MINIPORT;
	hwInitData.DeviceExtensionSize = sizeof(AIO_HBA_EXTENSION);
	hwInitData.SpecificLuExtensionSize = sizeof(AIO_LU_EXTENSION);
	hwInitData.SrbExtensionSize = sizeof(AIO_SRB_EXTENSION);

	NtStatus = StorPortInitialize(DriverObject,
		RegistryPath,
		&hwInitData,
		NULL
		);


done:

	if (STATUS_SUCCESS != NtStatus)
		ExFreePoolWithTag(pMasterDriverInfo, AIO_POOL_TAG);

	return NtStatus;
}

/******************************************************************************
//
/*****************************************************************************/
_Use_decl_annotations_
void DriverUnload(PDRIVER_OBJECT DriverObject)
{
	DoTraceMessage(AIOFastDiskDebugTrace, "Driver unloading\n");
	WPP_CLEANUP(DriverObject);
}



/******************************************************************************
// Storport calls indirectly when handling an IRP_MJ_PnP, IRP_MN_START_DEVICE
/*****************************************************************************/
_Use_decl_annotations_
ULONG
AioVmpFindAdapter(
	pAIO_HBA_EXTENSION  pHbaDevExt,    // Per adapter storage area; HBA Extension
	PVOID  HwContext,          // Context
	PVOID  BusInformation,     // Miniport's FDO
	PCHAR  ArgumentString,
	PPORT_CONFIGURATION_INFORMATION  ConfigInfo,
	PBOOLEAN  Reserved3
	)
{
	UNREFERENCED_PARAMETER(HwContext);
	UNREFERENCED_PARAMETER(BusInformation);
	//UNREFERENCED_PARAMETER(LowerDO);
	UNREFERENCED_PARAMETER(ArgumentString);

	NTSTATUS Status = SP_RETURN_FOUND;
	PCHAR charPtr;
	KLOCK_QUEUE_HANDLE SaveIrql;
	ULONG i, len;

	DoTraceMessage(AIOFastDiskDebugTrace, "AioVmpFindAdapter: pHbaDevExt = 0x%p, ConfigInfo = 0x%p \n", pHbaDevExt, ConfigInfo);

	pHbaDevExt->pMasterDrvInfo = pMasterDriverInfo;  // Saved in HBA

	InitializeListHead(&pHbaDevExt->LuDevList);
	InitializeListHead(&pHbaDevExt->MpioLUList);
	KeInitializeSpinLock(&pHbaDevExt->HbaObjLock);
	KeInitializeSpinLock(&pHbaDevExt->LuDevListLock);

	pHbaDevExt->HostTargetId = (UCHAR)pHbaDevExt->pMasterDrvInfo->RegInfo.InitiatorID;
	pHbaDevExt->pDrvObj = pHbaDevExt->pMasterDrvInfo->pDriverObj;
	AioCreateDeviceList(pHbaDevExt, pHbaDevExt->pMasterDrvInfo->RegInfo.nLUNsperHBA);

	ConfigInfo->VirtualDevice = TRUE;
	ConfigInfo->WmiDataProvider = TRUE;
	ConfigInfo->MaximumTransferLength = SP_UNINITIALIZED_VALUE;
	ConfigInfo->AlignmentMask = FILE_LONG_ALIGNMENT; //DWORD Alignment
	ConfigInfo->CachesData = FALSE;  //Flush and Shutdown notification
	ConfigInfo->MaximumNumberOfTargets = SCSI_MAXIMUM_TARGETS_PER_BUS;
	ConfigInfo->NumberOfBuses = 1;
	ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
	ConfigInfo->ScatterGather = TRUE; // For OS < Win7 
	ConfigInfo->MapBuffers = TRUE;


	charPtr = (PCHAR)pHbaDevExt->pMasterDrvInfo->RegInfo.VendorId.Buffer;
	len = min(9, (pHbaDevExt->pMasterDrvInfo->RegInfo.VendorId.Length / 2));
	for (i = 0; i < len; i++, charPtr += 2)
		pHbaDevExt->VendorID[i] = *charPtr;

	charPtr = (PCHAR)pHbaDevExt->pMasterDrvInfo->RegInfo.DeviceId.Buffer;
	len = min(9, (pHbaDevExt->pMasterDrvInfo->RegInfo.DeviceId.Length / 2));
	for (i = 0; i < len; i++, charPtr += 2)
		pHbaDevExt->DeviceID[i] = *charPtr;

	charPtr = (PCHAR)pHbaDevExt->pMasterDrvInfo->RegInfo.RevisionId.Buffer;
	len = min(4, (pHbaDevExt->pMasterDrvInfo->RegInfo.RevisionId.Length / 2));
	for (i = 0; i < len; i++, charPtr += 2)
		pHbaDevExt->RevisionID[i] = *charPtr;

	pHbaDevExt->nLUNperHBA = pHbaDevExt->pMasterDrvInfo->RegInfo.nLUNsperHBA;

	// Only 64 bit has InStack queued spinlocks!
	KeAcquireInStackQueuedSpinLock(&pHbaDevExt->pMasterDrvInfo->DrvInfoLock, &SaveIrql);
	InsertTailList(&pHbaDevExt->pMasterDrvInfo->ListHBAObj, &pHbaDevExt->HbaList);
	pHbaDevExt->pMasterDrvInfo->nHBAObj++;
	KeReleaseInStackQueuedSpinLock(&SaveIrql);

	//InitializeWmiContext((AIO_HBA_EXTENSION)pHbaDevExt);
	*Reserved3 = FALSE;

	//TRACE_RETURN(Status);
	return Status;
}

/******************************************************************************
//Initialize miniport (HBA and find devices) after reboot,
//  called after *FindAdapter at DIRQL
/*****************************************************************************/
_Use_decl_annotations_
BOOLEAN
AioVmpInitialize(
	__in pAIO_HBA_EXTENSION  pHbaDevExt
	)
{
	UNREFERENCED_PARAMETER(pHbaDevExt);
	return TRUE;
}

/******************************************************************************
//
/*****************************************************************************/
_Use_decl_annotations_
BOOLEAN
AioVmpStartIo(
	__in pAIO_HBA_EXTENSION  pHbaDevExt,
	__in __out PSCSI_REQUEST_BLOCK pSrb
	)
{
	UNREFERENCED_PARAMETER(pHbaDevExt);
	UNREFERENCED_PARAMETER(pSrb);

	UCHAR srbStatus = SRB_STATUS_INVALID_REQUEST;
	UCHAR Result = ResultDone;

	DoTraceMessage(AIOFastDiskDebugTrace, "AioVmpStartIo: pHbaDevExt = 0x%p, pSrb = 0x%p \n", pHbaDevExt, pSrb);

	_InterlockedExchangeAdd((volatile LONG *)&pHbaDevExt->nSRBSeen, 1);

	if (pHbaDevExt->bDontReport) {
		srbStatus = SRB_STATUS_INVALID_LUN;
		goto done;
	}

	switch (pSrb->Function) {

		case SRB_FUNCTION_EXECUTE_SCSI:
			//Execute SCSI
			srbStatus = SRB_STATUS_INVALID_REQUEST;
			break;

		case SRB_FUNCTION_WMI:
			//Handle WMI
			srbStatus = SRB_STATUS_INVALID_REQUEST;
			break;

		case SRB_FUNCTION_RESET_LOGICAL_UNIT:
			StorPortCompleteRequest(
				pHbaDevExt,
				pSrb->PathId,
				pSrb->TargetId,
				pSrb->Lun,
				SRB_STATUS_BUSY
				);
			srbStatus = SRB_STATUS_SUCCESS;
			break;

		case SRB_FUNCTION_RESET_DEVICE:
			//Complete Request
			StorPortCompleteRequest(pHbaDevExt,
				pSrb->PathId,
				pSrb->TargetId,
				SP_UNTAGGED,
				SRB_STATUS_BUSY
				);
			srbStatus = SRB_STATUS_SUCCESS;
			break;

		case SRB_FUNCTION_PNP:
			//Handle PnP
			srbStatus = SRB_STATUS_SUCCESS;
			break;

		case SRB_FUNCTION_POWER:
			srbStatus = SRB_STATUS_SUCCESS;
			break;

		case SRB_FUNCTION_SHUTDOWN:
			srbStatus = SRB_STATUS_SUCCESS;
			break;

		default:
			DoTraceMessage(AIOFastDiskDebugTrace, "AioVmpStartIo: Unknown srb function 0x%x \n", pSrb->Function);
			srbStatus = SRB_STATUS_INVALID_REQUEST;
			break;

	}

done:
	if (ResultDone == Result) {
		pSrb->SrbStatus = srbStatus;
		StorPortNotification(RequestComplete, pHbaDevExt, pSrb);
	}

	DoTraceMessage(AIOFastDiskDebugTrace, "AioVmpStartIo: Exit \n");

	return TRUE;
}



/******************************************************************************
// IRQL: DISPATCH_LEVEL
/*****************************************************************************/
_Use_decl_annotations_
BOOLEAN
AioVmpResetBus(
	__in pAIO_HBA_EXTENSION  pHbaDevExt,
	ULONG PathId
	)
{
	UNREFERENCED_PARAMETER(pHbaDevExt);
	UNREFERENCED_PARAMETER(PathId);

	//Flush Srbs

	return TRUE;
}

/******************************************************************************
//
/*****************************************************************************/
_Use_decl_annotations_
SCSI_ADAPTER_CONTROL_STATUS
AioVmpAdapterControl(
	__in pAIO_HBA_EXTENSION  pHbaDevExt,
	SCSI_ADAPTER_CONTROL_TYPE ControlType,
	PVOID Parameters
	)
{
	return ScsiAdapterControlSuccess;
}

/******************************************************************************
//
/*****************************************************************************/
_Use_decl_annotations_
VOID
AioVmpFreeAdapterResource(
	__in pAIO_HBA_EXTENSION  pHbaDevExt
	)
{
	//
}

/******************************************************************************
//
/*****************************************************************************/
_Use_decl_annotations_
VOID
AioVmpProcessServiceRequest(
	__in pAIO_HBA_EXTENSION  pHbaDevExt,
	PVOID Irp
	)
{
	//
}

/******************************************************************************
//
/*****************************************************************************/
_Use_decl_annotations_
VOID
AioVmpCompleteServiceIrp(
	__in pAIO_HBA_EXTENSION  pHbaDevExt
	)
{
	//
}

/******************************************************************************
//
/*****************************************************************************/
_Use_decl_annotations_
VOID
AioVmpInitTracing(
	PVOID Arg1,
	PVOID Arg2
	)
{
	WPP_INIT_TRACING(Arg1, Arg2);
	DoTraceMessage(AIOFastDiskDebugTrace, "AioVmpInitTracing: Tracing Initialized");
}

/******************************************************************************
//
/*****************************************************************************/
_Use_decl_annotations_
VOID
AioVmpCleanupTracing(
	PVOID Args1
	)
{
	DoTraceMessage(AIOFastDiskDebugTrace, "AioVmpCleanupTracing: Cleanup Tracing");
	WPP_CLEANUP(Args1);
}