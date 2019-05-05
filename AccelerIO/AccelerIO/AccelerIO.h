#pragma once
#ifndef _accelerio_h_
#define _accelerio_h_

//#include <ntddk.h>
#include <wdm.h>
#include <ntdef.h>
#include <ntddk.h>
#include <windef.h>
#include <storport.h>
#include <scsiwmi.h>
#include <ntddscsi.h>

#include "trace.h"

#define VENDOR_ID	L"ACCELERIO"
#define DEVICE_ID	L"FAST_DISK"
#define REVISION	L"0001"

#define AIO_POOL_TAG	'gOIA'


#define DEFAULT_BREAK_ON_ENTRY      0                // No break
#define DEFAULT_DEBUG_LEVEL         2               
#define DEFAULT_INITIATOR_ID        7
#define DEFAULT_VIRTUAL_DISK_SIZE   (1024 * 1024 * 1024)  // 1 GB
#define DEFAULT_PHYSICAL_DISK_SIZE  DEFAULT_VIRTUAL_DISK_SIZE
//#define DEFAULT_USE_LBA_LIST        0
//#define DEFAULT_NUMBER_OF_BUSES     1
#define DEFAULT_nVirtDisks			1
#define DEFAULT_nLUNsperHBA			1
#define DEFAULT_bCombineVirtDisks   FALSE


#define LUNInfoMax 8
#define DISK_DEVICE         0x00

typedef struct _AIO_REG_INFO			AIO_REG_INFO,			*pAIO_REG_INFO;
typedef struct _AIO_MASTER_INFO			AIO_MASTER_INFO,		*pAIO_MASTER_INFO;
typedef struct _AIO_HBA_EXTENSION		AIO_HBA_EXTENSION,		*pAIO_HBA_EXTENSION;
typedef struct _AIO_LU_EXTENSION_MPIO	AIO_LU_EXTENSION_MPIO,	*pAIO_LU_EXTENSION_MPIO;
typedef struct _AIO_LU_EXTENSION		AIO_LU_EXTENSION,		*pAIO_LU_EXTENSION;
typedef struct _AIO_SRB_EXTENSION		AIO_SRB_EXTENSION,		*pAIO_SRB_EXTENSION;
typedef struct _AIO_DEVICE_INFO			AIO_DEVICE_INFO,		*pAIO_DEVICE_INFO;
typedef struct _AIO_DEVICE_LIST			AIO_DEVICE_LIST,		*pAIO_DEVICE_LIST;
//
// Load this from Registry
//
typedef struct _AIO_REG_INFO {
	UNICODE_STRING   VendorId;
	UNICODE_STRING   DeviceId;
	UNICODE_STRING   RevisionId;
	ULONG            BreakOnEntry;       // Break into debugger
	ULONG            DebugLevel;         // Debug log level
	ULONG            InitiatorID;        // Adapter's target ID
	ULONG            VirtualDiskSize;    // Disk size to be reported
	ULONG            PhysicalDiskSize;   // Disk size to be allocated
	ULONG            nVirtDisks;       // Number of virtual disks.
	ULONG            nLUNsperHBA;      // Number of LUNs per HBA.
	ULONG            bCombineVirtDisks;  // 0 => do not combine virtual disks a la MPIO.
} AIO_REG_INFO, *pAIO_REG_INFO;


//
// Master object for miniport instance
//
typedef struct _AIO_MASTER_INFO {
	AIO_REG_INFO	RegInfo;
	KSPIN_LOCK      DrvInfoLock;
	KSPIN_LOCK      MPIOExtLock;	// Lock for ListMPIOExt, header of list of HW_LU_EXTENSION_MPIO objects, 
	LIST_ENTRY      ListHBAObj;		// Header of list of AIO_HBA_EXTENSION objects.
	LIST_ENTRY      ListMPIOExt;	// Header of list of HW_LU_EXTENSION_MPIO objects.
	PDRIVER_OBJECT  pDriverObj;
	ULONG           nHBAObj;		// Count of items in ListHBAObj.
	ULONG           nMPIOExtObj;	// Count of items in ListMPIOExt.
} AIO_MASTER_INFO, *pAIO_MASTER_INFO;



// HBA Device Extension
typedef struct _AIO_HBA_EXTENSION {
	LIST_ENTRY					HbaList;	// List of Adapters
	KSPIN_LOCK					HbaObjLock;
	LIST_ENTRY					LuDevList;  // List of SCSI devices
	KSPIN_LOCK					LuDevListLock;
	LIST_ENTRY					MpioLUList; // List of MPIO Luns
	PDRIVER_OBJECT              pDrvObj;
	pAIO_MASTER_INFO			pMasterDrvInfo;
	pAIO_DEVICE_LIST			pDeviceList;
	PIRP                        pReverseCallIrp;
	UNICODE_STRING				DeviceInterface; 
	KEVENT						DeleteDeviceThreadStart;
	ULONG						nSRBSeen;
	ULONG						nMPIOLuns;
	ULONG						nLUNperHBA;
	UCHAR                       HostTargetId;
	UCHAR                       AdapterState;	
	UCHAR						VendorID[10];
	UCHAR						DeviceID[10];
	UCHAR						RevisionID[5];
	BOOLEAN						bDontReport;
} AIO_HBA_EXTENSION, *pAIO_HBA_EXTENSION;


// LUNs as one pseudo LUN by MPIO
typedef struct _AIO_LU_EXTENSION_MPIO {
	LIST_ENTRY		List;					// Pointers to next and previous HW_LU_EXTENSION_MPIO objects.
	LIST_ENTRY      LUExtList;              // Header of list of HW_LU_EXTENSION objects.
	KSPIN_LOCK      LUExtMPIOLock;			// This object lock
	ULONG           nRealLUNs;
	SCSI_ADDRESS    ScsiAddr;
	PUCHAR          pDiskBuf;
	USHORT          MaxBlocks;
	BOOLEAN         bIsMissingOnAnyPath;    // At present, this is set only by a kernel debugger, for testing.
} AIO_LU_EXTENSION_MPIO, *pAIO_LU_EXTENSION_MPIO;


// Logical Unit Extension tracks individual SCSI devices
//  allocated by StorPort
typedef struct _AIO_LU_EXTENSION {
	LIST_ENTRY				LuExtList;	// Used in HBA_EXT
	LIST_ENTRY				MPIOList;	// Used in LU_Extension_MPIO
	pAIO_LU_EXTENSION_MPIO	pLUMPIOExt;	// associated MPIO extension object
	PUCHAR					pDiskBuf;
	ULONG					LUFlags;
	USHORT					MaxBlocks;
	USHORT					BlocksUsed;
	BOOLEAN					IsMissing;	// Device Reported Missing?
	UCHAR					DeviceType;
	UCHAR					TargetId;
	UCHAR					Lun;
} AIO_LU_EXTENSION, *pAIO_LU_EXTENSION;


// Stores request specific information
typedef struct _AIO_SRB_EXTENSION {
	SCSIWMI_REQUEST_CONTEXT WmiRequestContext;
} AIO_SRB_EXTENSION, *pAIO_SRB_EXTENSION;


typedef struct _AIO_DEVICE_INFO {
	UCHAR    DeviceType;
	UCHAR    TargetID;
	UCHAR    LunID;
} AIO_DEVICE_INFO, *pAIO_DEVICE_INFO;

typedef struct _AIO_DEVICE_LIST {
	ULONG          DeviceCount;
	AIO_DEVICE_INFO DeviceInfo[1];
} AIO_DEVICE_LIST, *pAIO_DEVICE_LIST;

typedef struct _AIO_DEVICE_INFO {
	UCHAR    DeviceType;
	UCHAR    TargetID;
	UCHAR    LunID;
} AIO_DEVICE_INFO, *pAIO_DEVICE_INFO;

typedef struct _AIO_DEVICE_LIST {
	ULONG          DeviceCount;
	AIO_DEVICE_INFO DeviceInfo[1];
} AIO_DEVICE_LIST, *pAIO_DEVICE_LIST;


enum ResultType {
	ResultDone,
	ResultQueued
};


__declspec(dllexport)
sp_DRIVER_INITIALIZE		DriverEntry;
DRIVER_UNLOAD				DriverUnload;

HW_INITIALIZE				AioVmpInitialize;
HW_STARTIO					AioVmpStartIo;
HW_FIND_ADAPTER				AioVmpFindAdapter;
HW_RESET_BUS				AioVmpResetBus;
HW_ADAPTER_CONTROL			AioVmpAdapterControl;
HW_FREE_ADAPTER_RESOURCES	AioVmpFreeAdapterResource;
HW_PROCESS_SERVICE_REQUEST	AioVmpProcessServiceRequest;
HW_COMPLETE_SERVICE_IRP		AioVmpCompleteServiceIrp;
HW_INITIALIZE_TRACING		AioVmpInitTracing;
HW_CLEANUP_TRACING			AioVmpCleanupTracing;


VOID AioQueryRegParams(PUNICODE_STRING, pAIO_REG_INFO);
NTSTATUS AioCreateDeviceList(pAIO_HBA_EXTENSION, ULONG);

#endif