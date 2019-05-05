#include "AccelerIO.h"
#include "utils.tmh"

VOID
AioQueryRegParams(
	__in PUNICODE_STRING RegistryPath,
	__in pAIO_REG_INFO RegInfo)
{
	AIO_REG_INFO lclRegInfo;

	lclRegInfo.BreakOnEntry		= DEFAULT_BREAK_ON_ENTRY;       // Break into debugger
	lclRegInfo.DebugLevel		= DEFAULT_DEBUG_LEVEL;			// Debug log level
	lclRegInfo.InitiatorID		= DEFAULT_INITIATOR_ID;			// Adapter's target ID
	lclRegInfo.VirtualDiskSize	= DEFAULT_PHYSICAL_DISK_SIZE;   // Disk size to be reported
	lclRegInfo.PhysicalDiskSize	= DEFAULT_VIRTUAL_DISK_SIZE;	// Disk size to be allocated
	lclRegInfo.nVirtDisks		= DEFAULT_nVirtDisks;			// Number of virtual disks.
	lclRegInfo.nLUNsperHBA		= DEFAULT_nLUNsperHBA;			// Number of LUNs per HBA.
	lclRegInfo.bCombineVirtDisks= DEFAULT_bCombineVirtDisks;

	RtlInitUnicodeString(&lclRegInfo.VendorId, VENDOR_ID);
	RtlInitUnicodeString(&lclRegInfo.DeviceId, DEVICE_ID);
	RtlInitUnicodeString(&lclRegInfo.RevisionId, REVISION);

	{
		NTSTATUS status;

#pragma warning(push)
#pragma warning(disable : 4204)
#pragma warning(disable : 4221)

		RTL_QUERY_REGISTRY_TABLE lclRegQueryTable[] = {
			// Search under Parameters subkey
			{NULL, RTL_QUERY_REGISTRY_SUBKEY | RTL_QUERY_REGISTRY_NOEXPAND,  L"Parameters",		NULL,				(ULONG_PTR)NULL, NULL,				(ULONG_PTR)NULL},

			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"BreakOnEntry",     &RegInfo->BreakOnEntry,      REG_DWORD,       &lclRegInfo.BreakOnEntry,          sizeof(ULONG) },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"DebugLevel",       &RegInfo->DebugLevel,        REG_DWORD,       &lclRegInfo.DebugLevel,            sizeof(ULONG) },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"InitiatorID",      &RegInfo->InitiatorID,       REG_DWORD,       &lclRegInfo.InitiatorID,           sizeof(ULONG) },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"VirtualDiskSize",  &RegInfo->VirtualDiskSize,   REG_DWORD,       &lclRegInfo.VirtualDiskSize,       sizeof(ULONG) },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"PhysicalDiskSize", &RegInfo->PhysicalDiskSize,  REG_DWORD,       &lclRegInfo.PhysicalDiskSize,      sizeof(ULONG) },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"VendorId",         &RegInfo->VendorId,          REG_SZ,          lclRegInfo.VendorId.Buffer,        0 },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"DeviceId",         &RegInfo->DeviceId,          REG_SZ,          lclRegInfo.DeviceId.Buffer,        0 },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"RevisionId",		  &RegInfo->RevisionId,		   REG_SZ,          lclRegInfo.RevisionId.Buffer,	   0 },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"VirtDisks",        &RegInfo->nVirtDisks,		   REG_DWORD,       &lclRegInfo.nVirtDisks,            sizeof(ULONG) },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"LUNsperHBA",       &RegInfo->nLUNsperHBA,       REG_DWORD,       &lclRegInfo.nLUNsperHBA,           sizeof(ULONG) },
			{ NULL, RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"CombineVirtDisks", &RegInfo->bCombineVirtDisks, REG_DWORD,       &lclRegInfo.bCombineVirtDisks,     sizeof(ULONG) },

			// The null entry denotes the end of the array.                                                                    
			{ NULL, 0,                                                       NULL,                NULL,                        (ULONG_PTR)NULL, NULL,                              (ULONG_PTR)NULL },
		};

#pragma warning(pop)

		status = RtlQueryRegistryValues(
			RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
			RegistryPath->Buffer,
			lclRegQueryTable,
			NULL,
			NULL
			);

		// Tracing not enabled yet!
		// TRACE_RETURN(status);
		KdPrint(("AioQueryRegParams: RtlQueryRegistryValues() returned 0x0%p \r\n", status));

		if (STATUS_SUCCESS != status) {
			RegInfo->BreakOnEntry		= lclRegInfo.BreakOnEntry;
			RegInfo->DebugLevel			= lclRegInfo.DebugLevel;
			RegInfo->InitiatorID		= lclRegInfo.InitiatorID;
			RegInfo->PhysicalDiskSize	= lclRegInfo.PhysicalDiskSize;
			RegInfo->VirtualDiskSize	= lclRegInfo.VirtualDiskSize;
			RegInfo->nVirtDisks			= lclRegInfo.nVirtDisks;
			RegInfo->nLUNsperHBA		= lclRegInfo.nLUNsperHBA;
			RegInfo->bCombineVirtDisks	= lclRegInfo.bCombineVirtDisks;
			RtlCopyUnicodeString(&RegInfo->VendorId, &lclRegInfo.VendorId);
			RtlCopyUnicodeString(&RegInfo->DeviceId, &lclRegInfo.DeviceId);
			RtlCopyUnicodeString(&RegInfo->RevisionId, &lclRegInfo.RevisionId);
		}

	}
}


NTSTATUS
AioCreateDeviceList(
	__in pAIO_HBA_EXTENSION pHbaDevExt,
	__in ULONG nLUNs
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	ULONG i, len = FIELD_OFFSET(AIO_DEVICE_LIST, DeviceInfo) + (nLUNs*sizeof(AIO_DEVICE_INFO));

	if (pHbaDevExt->pDeviceList)
		ExFreePoolWithTag(pHbaDevExt->pDeviceList, AIO_POOL_TAG);

	pHbaDevExt->pDeviceList = ExAllocatePoolWithTag(NonPagedPool, len, AIO_POOL_TAG);

	if (!pHbaDevExt->pDeviceList) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto done;
	}

	RtlZeroMemory(pHbaDevExt->pDeviceList, len);
	pHbaDevExt->pDeviceList->DeviceCount = nLUNs;
	for (i = 0; i < nLUNs; i++)
		pHbaDevExt->pDeviceList->DeviceInfo[i].LunID = (UCHAR)i;

done:
	return status;
}