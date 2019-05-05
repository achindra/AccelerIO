/*++

Module Name:

    VirtualMiniportDevice.C

Date:

    15-Feb-2014

Abstract:

    Module contains the routines for implementing the overall Device
    specific functions.

    All routines start with VM - for 'V'irtual 'M'iniport
    All routines are further prefixed with component name

--*/

#include <VirtualMiniportDeviceTypes.h>
#include <VirtualMiniportDevice.h>
#include <VirtualMiniportBus.h>
#include <VirtualMiniportTarget.h>
#include <VirtualMiniportLun.h>

#include <VirtualMiniportFile.h>

//
// WPP based event trace
//

#include <VirtualMiniportDevice.tmh>

//
// Forward declarations of private functions
//

VOID 
VMDeviceStateChangeCallback(
    _In_      PVOID AdapterExtension,
    _In_opt_  PVOID Context,
    _In_      SHORT AddressType,
    _In_      PVOID Address,
    _In_      ULONG Status
    );

static
NTSTATUS
VMBlockLockInitialize(
    _Inout_ PVM_BLOCK_LOCK Lock
    );

static
NTSTATUS
VMBlockLockAcquire(
    _Inout_ PVM_BLOCK_LOCK Lock
    );

static
NTSTATUS
VMBlockLockRelease(
    _Inout_ PVM_BLOCK_LOCK Lock
    );

static
NTSTATUS
VMDeviceReadWritePhysicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_TIERED_DEVICE Device,
    _In_ BOOLEAN Read,
    _Inout_ PVOID DataBuffer,
    _In_ PVIRTUAL_MINIPORT_LOGICAL_BLOCK_ENTRY LogicalBlockEntry
    ) ;
//
// Define the attributes of functions; declarations are in module
// specific header
//

#pragma alloc_text(NONPAGED, VMDeviceValidateAddress)
#pragma alloc_text(PAGED, VMDeviceFindDeviceByAddress)
#pragma alloc_text(NONPAGED, VMDeviceReportStateChange)
#pragma alloc_text(NONPAGED, VMDeviceStateChangeCallback)

#pragma alloc_text(PAGED, VMDeviceCreatePhysicalDevice)
#pragma alloc_text(PAGED, VMDeviceDeletePhysicalDevice)
#pragma alloc_text(PAGED, VMDeviceBuildPhysicalDeviceDetails)

#pragma alloc_text(PAGED, VMDeviceCreateLogicalDevice)
#pragma alloc_text(PAGED, VMDeviceDeleteLogicalDevice)
#pragma alloc_text(PAGED, VMDeviceBuildLogicalDeviceDetails)

#pragma alloc_text(PAGED, VMBlockLockInitialize)
#pragma alloc_text(PAGED, VMBlockLockAcquire)
#pragma alloc_text(PAGED, VMBlockLockRelease)

#pragma alloc_text(PAGED, VMDeviceReadWritePhysicalDevice)
#pragma alloc_text(PAGED, VMDeviceReadWriteLogicalDevice)

//
// General device routines
//

UCHAR 
VMDeviceValidateAddress(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _In_ UCHAR TargetId,
    _In_ UCHAR LunId
    )

/*++

Routine Description:

    Checks if the given address is valid OR not.

    NOTE: I really wanted to implement this as part of VMDeviceReferenceAddress
    and VMDeviceDereferenceAddress. Intent was to reference the objects when we
    see an I/O on that address. But it needs changes at many places. So taking
    this short-cut to just validate the address.

    We acquire the locks in the shared mode in all hierarchy and check it at every
    level.

    If the device state also includes reset, we can include it here.

Arguments:
  
Environment:

    IRQL - PASSIVE_LEVEL. Note that we acquire waitable locks here

Return Value:

    SRB_STATUS_XXX

--*/

{
    UCHAR Status;
    PVIRTUAL_MINIPORT_BUS Bus;
    PVIRTUAL_MINIPORT_TARGET Target;
    PVIRTUAL_MINIPORT_LUN Lun;

    Status = SRB_STATUS_NO_DEVICE;

    if ( AdapterExtension == NULL ) {
        Status = SRB_STATUS_NO_HBA;
        goto Cleanup;
    }

    if ( VMLockAcquireShared(&(AdapterExtension->AdapterLock)) == TRUE ) {
    
        //
        // Validate adapter state. We dont have the adapter state machine implemented
        // thoroughly. So leaving it as is for now.
        //
        Status = SRB_STATUS_INVALID_PATH_ID;

        if ( BusId < AdapterExtension->MaxBusCount && 
             AdapterExtension->Buses [BusId] != VIRTUAL_MINIPORT_INVALID_POINTER) {

            Bus = AdapterExtension->Buses [BusId];
            if ( VMLockAcquireShared(&(Bus->BusLock)) == TRUE ) {
            
                if ( Bus->State == VMDeviceStarted ) {
                
                    Status = SRB_STATUS_INVALID_TARGET_ID;
                    if ( TargetId < Bus->MaxTargetCount && Bus->Targets [TargetId] != VIRTUAL_MINIPORT_INVALID_POINTER ) {
                        Target = Bus->Targets [TargetId];

                        if ( VMLockAcquireShared(&(Target->TargetLock)) == TRUE ) {
                        
                            if ( Target->State == VMDeviceStarted ) {
                                
                                Status = SRB_STATUS_INVALID_LUN;
                                if ( LunId < Target->MaxLunCount && Target->Luns [LunId] != VIRTUAL_MINIPORT_INVALID_POINTER ) {

                                    //
                                    // Check for valid Lun
                                    //
                                    Lun = Target->Luns [LunId];
                                    if ( VMLockAcquireShared(&(Lun->LunLock)) == TRUE ) {

                                        if ( Lun->State == VMDeviceStarted ) {
                                            Status = SRB_STATUS_SUCCESS;
                                        }
                                        VMLockReleaseShared(&(Lun->LunLock));
                                    }
                                }
                            }
                            VMLockReleaseShared(&(Target->TargetLock));
                        }                   
                    }
                }
                VMLockReleaseShared(&(Bus->BusLock));
            }        
        }
        VMLockReleaseShared(&(AdapterExtension->AdapterLock));
    }

    //
    // Now scrub the return status based on the device type we needed to validate
    //
Cleanup:
    return(Status);
}

/*
    Hook to test selection timeout
*/

volatile BOOLEAN gLunHook = FALSE;
static
UCHAR
VMDeviceStatusHook(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _In_ UCHAR TargetId,
    _In_ UCHAR LunId,
    _In_ UCHAR SrbStatus
    )
{
    static BOOLEAN PnpNotified = FALSE;

    if ( gLunHook == TRUE ) {
        SrbStatus = SRB_STATUS_SELECTION_TIMEOUT;
        if ( PnpNotified == FALSE ) {
            PnpNotified = TRUE;
            VMDeviceReportStateChange(AdapterExtension, BusId, TargetId, LunId, STATE_CHANGE_LUN);
        }
    } else {
        PnpNotified = FALSE;
    }

    return(SrbStatus);
}

UCHAR 
VMDeviceFindDeviceByAddress(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR BusId,
    _In_ UCHAR TargetId,
    _In_ UCHAR LunId,
    _In_ VM_TYPE DeviceType,
    _Inout_ PVOID *Device
    )

/*++

Routine Description:

    Find the device by address, and returns the pointer to the
    asked device type

Arguments:
  
  AdapterExtension - Adapter

  Bus - Bus ID of the address

  Target - Target Id of the address

  Lun - Lun Id of the address

  DeviceType - type that needs to be looked up

  Device - pointer that receives the device

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STOR_STATUS_XXX

--*/

{
    UCHAR SrbStatus;
    NTSTATUS Status;
    PVIRTUAL_MINIPORT_BUS Bus;
    PVIRTUAL_MINIPORT_TARGET Target;
    PVIRTUAL_MINIPORT_LUN Lun;

    SrbStatus = SRB_STATUS_NO_HBA;
    Status = STATUS_UNSUCCESSFUL;
    Bus = NULL;
    Target = NULL;
    Lun = NULL;

    if ( Device == NULL || !(DeviceType >= VMTypeAdapter && DeviceType <= VMTypeLun) ) {
        SrbStatus = SRB_STATUS_ERROR;
        goto Cleanup;
    }

    if ( AdapterExtension == NULL ) {
        SrbStatus = SRB_STATUS_NO_HBA;
        goto Cleanup;
    }

    *Device = NULL;

    if ( DeviceType == VMTypeAdapter ) {
        *Device = (PVOID) AdapterExtension;
        SrbStatus = SRB_STATUS_SUCCESS;
        goto Cleanup;
    }

    Status = VMBusQueryById(AdapterExtension, BusId, &Bus, FALSE);
    if ( !NT_SUCCESS(Status) ) {
        SrbStatus = SRB_STATUS_INVALID_PATH_ID;
        goto Cleanup;
    } else if ( DeviceType == VMTypeBus ) {
        *Device = (PVIRTUAL_MINIPORT_BUS) Bus;
        SrbStatus = SRB_STATUS_SUCCESS;
        goto Cleanup;
    }

    Status = VMTargetQueryById(AdapterExtension, Bus, TargetId, &Target, FALSE);
    if ( !NT_SUCCESS(Status) ) {
        SrbStatus = SRB_STATUS_INVALID_TARGET_ID;
        goto Cleanup;
    } else if ( DeviceType == VMTypeTarget ) {
        *Device = (PVIRTUAL_MINIPORT_TARGET) Target;
        SrbStatus = SRB_STATUS_SUCCESS;
        goto Cleanup;
    }

    Status = VMLunQueryById(AdapterExtension, Bus, Target, LunId, &Lun, FALSE);
    if ( !NT_SUCCESS(Status) ) {
        SrbStatus = SRB_STATUS_INVALID_LUN;
        goto Cleanup;
    } else if ( DeviceType == VMTypeLun ) {
        *Device = (PVIRTUAL_MINIPORT_LUN) Lun;
        SrbStatus = SRB_STATUS_SUCCESS;
        goto Cleanup;
    }

    SrbStatus = SRB_STATUS_ERROR;

Cleanup:

    return(SrbStatus);
}

ULONG
VMDeviceReportStateChange(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ UCHAR Bus,
    _In_ UCHAR Target,
    _In_ UCHAR Lun,
    _In_ ULONG ChangedEntity
    )

/*++

Routine Description:

    Wraps the device change notification so that all the entities can call into
    this function. This handles the callback too.

Arguments:
  
  AdapterExtension - Adapter on which the change was detected

  Bus - Bus ID for the change

  Target - Target Id for the change

  Lun - Lun Id for the change

  ChangedEntity - Bit mask of entities that changed


Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    STOR_STATUS_XXX

--*/

{
    ULONG Status;
    PSTOR_ADDR_BTL8 StorAddress;

    
    Status = STOR_STATUS_UNSUCCESSFUL;
    StorAddress = NULL;

    if ( AdapterExtension == NULL || (ChangedEntity & ~(STATE_CHANGE_LUN | STATE_CHANGE_TARGET | STATE_CHANGE_BUS)) != 0 ) {
        Status = STOR_STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    Status = StorPortAllocatePool(AdapterExtension,
                                  sizeof(STOR_ADDR_BTL8),
                                  VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                  &StorAddress);
    if ( Status == STOR_STATUS_SUCCESS ) {

        StorAddress->Type = STOR_ADDRESS_TYPE_BTL8;
        StorAddress->AddressLength = STOR_ADDR_BTL8_ADDRESS_LENGTH;

        Status = StorPortGetSystemPortNumber(AdapterExtension,
                                             (PSTOR_ADDRESS)StorAddress);
        if ( Status == STOR_STATUS_SUCCESS ) {
            
            StorAddress->Path = Bus;
            StorAddress->Target = Target;
            StorAddress->Lun = Lun;

            Status = StorPortStateChangeDetected(AdapterExtension,
                                                 ChangedEntity,
                                                 (PSTOR_ADDRESS)StorAddress,
                                                 0,
                                                 VMDeviceStateChangeCallback,
                                                 NULL);
        }
    }

Cleanup:
    if ( Status != STOR_STATUS_SUCCESS && StorAddress != NULL ) {
        //
        // TODO: Log an message for failure
        //
        StorPortFreePool(AdapterExtension,
                         StorAddress);
    }
    return(Status);
}

VOID VMDeviceStateChangeCallback(
    _In_      PVOID AdapterExtension,
    _In_opt_  PVOID Context,
    _In_      SHORT AddressType,
    _In_      PVOID Address,
    _In_      ULONG Status
    )

/*++

Routine Description:

    Callback called after the completion of the change notification

Arguments:

    AdapterExtension - Adapter extension that was associated with the change notification

    Context - Context passed by the notify routine; NULL in out case

    AddressType - type of address

    Address - Should be STOR_ADDR_BTL8

    Status - Change notification processing status
  
Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    STOR_STATUS_XXX

--*/

{
    PSTOR_ADDR_BTL8 StorAddress;

    StorAddress = (PSTOR_ADDR_BTL8) Address;

    UNREFERENCED_PARAMETER(Context);

    if ( AddressType == STOR_ADDRESS_TYPE_BTL8 ) {
        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_DEVICE,
                "[%s]AdapterExtension:%p, Port:%d, Address: [%02d:%02d:%02d], Status:0x%08x",
                __FUNCTION__,
                AdapterExtension,
                StorAddress->Port,
                StorAddress->Path,
                StorAddress->Target,
                StorAddress->Lun,
                Status);
    }

    StorPortFreePool(AdapterExtension,
                     Address);
}

static
NTSTATUS
VMBlockLockInitialize(
    _Inout_ PVM_BLOCK_LOCK Lock
    )

/*++

Routine Description:

    Initializes the caller allocated block lock

Arguments:

    Lock - lock to be initialized

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;

    KeInitializeEvent(&Lock->LockEvent, SynchronizationEvent, TRUE);
    Lock->OwnerThread = NULL;
    Lock->ReturnAddress = NULL;
    Status = STATUS_SUCCESS;
    return(Status);
}

static
NTSTATUS
VMBlockLockAcquire(
    _Inout_ PVM_BLOCK_LOCK Lock
    )

/*++

Routine Description:

    Acquires the block lock. Blocks until the lock ownership is granted.

Arguments:

    Lock - lock to be acquired

Environment:

    IRQL < DISPATCH_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;
    volatile ULONGLONG RetryCount;

    Status = STATUS_UNSUCCESSFUL;
    RetryCount = 0;

Retry:
    Status = KeWaitForSingleObject(&Lock->LockEvent, Executive, KernelMode, FALSE, NULL);
    if ( Status != STATUS_SUCCESS ) {
        //
        // We should never hit this situation
        //
        RetryCount++;
        goto Retry;
    }

    Lock->OwnerThread = KeGetCurrentThread();
    Lock->ReturnAddress = _ReturnAddress();
    return(Status);
}

static
NTSTATUS
VMBlockLockRelease(
    _Inout_ PVM_BLOCK_LOCK Lock
    )

/*++

Routine Description:

    Releases the block lock.

Arguments:

    Lock - lock to be released

Environment:

    IRQL < DISPATCH_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;
    ULONG SignalledStatus;

    Status = STATUS_UNSUCCESSFUL;
    SignalledStatus = 0;

    Lock->OwnerThread = NULL;
    Lock->ReturnAddress = NULL;
    SignalledStatus = KeSetEvent(&Lock->LockEvent, IO_NO_INCREMENT, FALSE);

    Status = STATUS_SUCCESS;
    return(Status);
}

NTSTATUS
VMDeviceCreatePhysicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_CREATE_TARGET_DESCRIPTOR TargetCreateDescriptor,
    _Inout_ PVIRTUAL_MINIPORT_TIERED_DEVICE Device
    )

/*++

Routine Description:

    Initializes the caller allocated device with target description

Arguments:

    AdapterExtension - Adapter extension needed if we needed for stor allocations

    Device - pointer to device to be initialized, caller allocated
    
    TargetCreateDescriptor - descriptor for device creation

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;
    ULONGLONG Size;
    ULONG TierIndex;
    ULONGLONG BlockIndex, FileTierBaseIndex;
    ULONGLONG PhysicalMemoryTierSize, FileTierSize;
    LARGE_INTEGER AllocationSize;
    PVIRTUAL_MINIPORT_CONFIGURATION Configuration;
    PVOID Buffer;
    USHORT BufferLength;
    GUID FileNameGuid;
    PVIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY PhysicalBlockEntry;
    ULONG PhysicalBlockSize;


    Status = STATUS_UNSUCCESSFUL;
    PhysicalMemoryTierSize = 0;
    FileTierSize = 0;
    Configuration = &(AdapterExtension->DeviceExtension->Configuration);
    Buffer = NULL;
    BufferLength = 0;

    if ( Device == NULL || TargetCreateDescriptor == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( !(TargetCreateDescriptor->BlockSize != VMBlockSize512 ||
        TargetCreateDescriptor->BlockSize != VMBlockSize1024 ||
        TargetCreateDescriptor->BlockSize != VMBlockSize4096 )) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( TargetCreateDescriptor->TierCount == 0 || TargetCreateDescriptor->TierCount > VIRTUAL_MINIPORT_MAX_TIERS ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    for ( TierIndex = 0; TierIndex < TargetCreateDescriptor->TierCount; TierIndex++ ) {
 
        switch ( TargetCreateDescriptor->TierDescription[TierIndex].Tier ) {

        case VMTierPhysicalMemory:
            PhysicalMemoryTierSize = TargetCreateDescriptor->TierDescription [TierIndex].TierSize;
            break;

        case VMTierFile:
            FileTierSize = TargetCreateDescriptor->TierDescription [TierIndex].TierSize;
            break;

        default:
            //
            // We should never come here. We have validated the tier count
            //
            VMRtlDebugBreak();
            break; 
        }
    }

    if ( PhysicalMemoryTierSize == 0 && FileTierSize != 0 ) {
        //
        // XXX: For now preventing having only file tier. If File tier is present
        // we exepect physical memory tier to be present as well
        //
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    //
    // We do check against individual size parameter and again the cummulative size of all tiers
    // This is needed as each tiers should be block aligned too and we need to ceil them to blocksize
    // and total device size should be inclusive of ceil aligned size of each tier
    //
    Size = VIRTUAL_MINIPORT_CEIL_ALIGN(TargetCreateDescriptor->Size, TargetCreateDescriptor->BlockSize);
    if ( VIRTUAL_MINIPORT_CEIL_ALIGN((PhysicalMemoryTierSize + FileTierSize), TargetCreateDescriptor->BlockSize) != Size ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    PhysicalMemoryTierSize = VIRTUAL_MINIPORT_CEIL_ALIGN(PhysicalMemoryTierSize, TargetCreateDescriptor->BlockSize);
    FileTierSize = VIRTUAL_MINIPORT_CEIL_ALIGN(FileTierSize, TargetCreateDescriptor->BlockSize);
    Size = PhysicalMemoryTierSize + FileTierSize;

    if ( !(Size >= VIRTUAL_MINIPORT_MIN_DEVICE_SIZE && Size <= VIRTUAL_MINIPORT_MAX_DEVICE_SIZE) ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( Size > Configuration->DeviceSizeMax ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    RtlZeroMemory(Device, sizeof(VIRTUAL_MINIPORT_TIERED_DEVICE));

    //
    // Start configuring the physical device
    //
    Device->BlockSize = TargetCreateDescriptor->BlockSize;
    Device->Size = Size;
    Device->AllocatedSize = 0;
    Device->LogicalDeviceCount = 0;
    Device->MaxBlocks = Size / Device->BlockSize;

    //
    // Initialize the lists
    //
    InitializeListHead(&Device->PhysicalMemoryFreeList);
    InitializeListHead(&Device->PhysicalMemoryLruList);
    InitializeListHead(&Device->FileTierFreeList);

    Device->PhysicalMemoryFreeEntries = 0;
    Device->PhysicalMemoryLruEntries = 0;
    Device->FileTierFreeEntries = 0;

    PhysicalBlockSize = (sizeof(VIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY) * (ULONG) Device->MaxBlocks);
    if ( StorPortAllocatePool(AdapterExtension,
                              PhysicalBlockSize,
                              VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                              &Device->PhysicalBlocks) != STOR_STATUS_SUCCESS ) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    //
    // Initialize the Physical block entries
    //
    PhysicalBlockEntry = Device->PhysicalBlocks;
    RtlZeroMemory(PhysicalBlockEntry, PhysicalBlockSize);
    for ( BlockIndex = 0; BlockIndex < Device->MaxBlocks; BlockIndex++ ) {
        PhysicalBlockEntry [BlockIndex].Valid = FALSE;
        PhysicalBlockEntry [BlockIndex].Tier = VMTierNone;
        VMBlockLockInitialize(&PhysicalBlockEntry [BlockIndex].Lock);
        InitializeListHead(&PhysicalBlockEntry [BlockIndex].List);
    }

    //
    // Configure the Tiers that are specified by the descriptor. If we are here
    // it implies atleast one tier is specified.
    //

    BlockIndex = 0;

    if ( PhysicalMemoryTierSize != 0 ) {
        
        //
        // Physical memory tier
        //
        Device->PhysicalMemoryTierSize = PhysicalMemoryTierSize;
        Device->PhysicalMemoryTierMaxBlocks = PhysicalMemoryTierSize / Device->BlockSize;
        
        //
        // XXX: StorPortAllocatePool allocates a max of ULONG sized allocations. This is
        // sufficient until we have 4GB physical memory tier. If we go beyond it, we need
        // to build mechanisms to allocate beyond (ULONG) 4GB memory.
        //
        if ( StorPortAllocatePool(AdapterExtension,
                                  (ULONG)PhysicalMemoryTierSize,
                                  VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                  &Device->PhysicalMemoryTier) != STOR_STATUS_SUCCESS ) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;       
        }
        
        RtlZeroMemory(Device->PhysicalMemoryTier, PhysicalMemoryTierSize);

        for ( BlockIndex; BlockIndex < Device->PhysicalMemoryTierMaxBlocks; BlockIndex++ ) {

            PhysicalBlockEntry [BlockIndex].Valid = TRUE;
            PhysicalBlockEntry [BlockIndex].Tier = VMTierPhysicalMemory;
            PhysicalBlockEntry [BlockIndex].TierBlockAddress = (PVOID) ((PUCHAR) Device->PhysicalMemoryTier + (BlockIndex*Device->BlockSize));
            InsertTailList(&(Device->PhysicalMemoryFreeList), &(PhysicalBlockEntry [BlockIndex].List));
            Device->PhysicalMemoryFreeEntries++;
        }

        //
        // Update tier count on the device
        //
        Device->TierCount++;
    }


    if ( FileTierSize != 0 ) {

        //
        // File tier
        //
        Device->FileTierSize = FileTierSize;
        Device->FileTierMaxBlocks = FileTierSize / Device->BlockSize;

        BufferLength = Configuration->MetadataLocation.MaximumLength + GUID_STRING_LENGTH;
        if ( StorPortAllocatePool(AdapterExtension,
                                  BufferLength,
                                  VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                  &Buffer) != STOR_STATUS_SUCCESS ) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        //
        // We expect the metadata path to contain a \ to demarkate the folder. And we
        // use size specifier while converting GUID to string as each field has fixed 
        // size and we accounted that in GUID_STRING_LENGTH
        //
        VMRtlCreateGUID(&FileNameGuid);
        RtlInitEmptyUnicodeString(&Device->FileTierFileName, Buffer, BufferLength);
        RtlUnicodeStringPrintf(&Device->FileTierFileName,
                               L"%s%x-%x-%x-%x%x-%x%x%x%x%x%x",
                               Configuration->MetadataLocation.Buffer,
                               FileNameGuid.Data1,
                               FileNameGuid.Data2,
                               FileNameGuid.Data3,
                               FileNameGuid.Data4 [0],
                               FileNameGuid.Data4 [1],
                               FileNameGuid.Data4 [2],
                               FileNameGuid.Data4 [3],
                               FileNameGuid.Data4 [4],
                               FileNameGuid.Data4 [5],
                               FileNameGuid.Data4 [6],
                               FileNameGuid.Data4 [7]);
        //
        // Now that we have the file location for backing the FileTier
        // open and initialize the file
        //

        Device->FileTier = NULL;
        AllocationSize.QuadPart = Device->FileTierSize;
        Status = VMFileCreate(&(Device->FileTierFileName),
                              GENERIC_ALL,
                              FILE_ATTRIBUTE_NORMAL,
                              0,
                              FILE_OPEN_IF,
                              //FILE_WRITE_THROUGH | 
                              FILE_RANDOM_ACCESS,
                              &AllocationSize,
                              TRUE,
                              &Device->FileTier);

        if ( !NT_SUCCESS(Status) ) {
            goto Cleanup;
        }

        //
        // Block index continues from previous tier's index
        //
        FileTierBaseIndex = BlockIndex;
        for ( BlockIndex = 0; BlockIndex < Device->FileTierMaxBlocks; BlockIndex++ ) {
            
            PhysicalBlockEntry [FileTierBaseIndex+BlockIndex].Valid = TRUE;
            PhysicalBlockEntry [FileTierBaseIndex+BlockIndex].Tier = VMTierFile;

            //
            // File offset starting at 0the byte in the file
            //
            PhysicalBlockEntry [FileTierBaseIndex+BlockIndex].TierBlockAddress = (PVOID) (BlockIndex*Device->BlockSize);
            InsertTailList(&(Device->FileTierFreeList), &(PhysicalBlockEntry [FileTierBaseIndex+BlockIndex].List));
            Device->FileTierFreeEntries++;
        }

        //
        // Update tier count on the device
        //
        Device->TierCount++;
    }
  
    InitializeListHead(&(Device->LogicalDevices));
    Device->LogicalDeviceCount = 0;

    VMLockInitialize(&(Device->DeviceLock), LockTypeExecutiveResource);

    Status = STATUS_SUCCESS;

Cleanup:

    if ( !NT_SUCCESS(Status) ) {
        if ( Buffer != NULL ) {
            StorPortFreePool(AdapterExtension, Buffer);
        }

        if ( Device != NULL && Device->PhysicalMemoryTier != NULL ) {
            StorPortFreePool(AdapterExtension, Device->PhysicalMemoryTier);
        }

        if ( Device->FileTier != NULL ) {
            VMFileClose(Device->FileTier);
        }

        if ( Device != NULL && Device->PhysicalBlocks != NULL ) {
            StorPortFreePool(AdapterExtension, Device->PhysicalBlocks);
        }
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DEVICE,
            "[%s]:Device:%p, create status:%!STATUS!",
            __FUNCTION__,
            Device,
            Status);

    return(Status);
}

NTSTATUS
VMDeviceDeletePhysicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_TIERED_DEVICE Device
    )

/*++

Routine Description:

    Cleans up the caller allocated device with target description

Arguments:

    AdapterExtension - Adapter extension needed to free stor allocations

    Device - pointer to device to be cleaned up
  
Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(AdapterExtension);
    Status = STATUS_UNSUCCESSFUL;

    if ( Device == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(Device->DeviceLock)) == TRUE ) {
        
        if (Device->FileTierFileName.Buffer != NULL ) {
            StorPortFreePool(AdapterExtension, Device->FileTierFileName.Buffer);
        }

        if ( Device->PhysicalMemoryTier != NULL ) {
            StorPortFreePool(AdapterExtension, Device->PhysicalMemoryTier);
        }

        if ( Device->FileTier != NULL ) {
            VMFileClose(Device->FileTier);
        }

        if ( Device->PhysicalBlocks != NULL ) {
            StorPortFreePool(AdapterExtension, Device->PhysicalBlocks);
        }

        //
        // Its mandatory all the logical devices are removed by this time.
        // Just assert incase we see this ever.
        //

        if ( Device->LogicalDeviceCount != 0 ) {
            VMRtlDebugBreak();
        }
        VMLockReleaseExclusive(&(Device->DeviceLock));
    }

    VMLockUnInitialize(&(Device->DeviceLock));
    Status = STATUS_SUCCESS;

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DEVICE,
            "[%s]:Device:%p, delete status:%!STATUS!",
            __FUNCTION__,
            Device,
            Status);
    return(Status);
}

NTSTATUS
VMDeviceBuildPhysicalDeviceDetails(
    _In_ PVIRTUAL_MINIPORT_TIERED_DEVICE Device,
    _Inout_ PVIRTUAL_MINIPORT_TARGET_DEVICE_DETAILS DeviceDetails
    )

/*++

Routine Description:

    Fills the details of the device details

Arguments:

    Device - pointer to device from which we need to pick the details
  
    DeviceDetails - pointer to caller allocated device details that will be filled

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;

    Status = STATUS_UNSUCCESSFUL;

    if ( Device == NULL || DeviceDetails == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(Device->DeviceLock)) == TRUE ) {
        DeviceDetails->Size = Device->Size;
        DeviceDetails->TierCount = Device->TierCount;
        DeviceDetails->BlockSize = Device->BlockSize;
        DeviceDetails->MaxBlocks = Device->MaxBlocks;
        DeviceDetails->LogicalDeviceCount = Device->LogicalDeviceCount;
        Status = STATUS_SUCCESS;
        VMLockReleaseExclusive(&(Device->DeviceLock));
    }

Cleanup:

    return(Status);

}

NTSTATUS
VMDeviceCreateLogicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_TIERED_DEVICE PhysicalDevice,
    _Inout_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE LogicalDevice,
    _In_ PVIRTUAL_MINIPORT_CREATE_LUN_DESCRIPTOR LunCreateDescriptor
    )

/*++

Routine Description:

    Initializes the caller allocated device with logical device description

Arguments:

    AdapterExtension - Adapter extension needed if we needed for stor allocations

    PhysicalDevice - pointer to physical device on which we will carve this logical
                     device

    LogicalDevice - pointer to device to be initialized, caller allocated
    
    LunCreateDescriptor - descriptor for device creation

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;
    BOOLEAN LockInitialized;
    ULONGLONG Blocks;
    ULONGLONG Size;
    ULONGLONG LogicalBlockCount;
    ULONGLONG LogicalBlockEntrySize;
    ULONGLONG BlockIndex;
    PVIRTUAL_MINIPORT_LOGICAL_BLOCK_ENTRY LogicalBlockEntry;

    UNREFERENCED_PARAMETER(AdapterExtension);
    Status = STATUS_UNSUCCESSFUL;
    LockInitialized = FALSE;
    Blocks = 0;

    if ( PhysicalDevice == NULL || LogicalDevice == NULL || LunCreateDescriptor == NULL || LunCreateDescriptor->ThinProvision == TRUE) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    RtlZeroMemory(LogicalDevice, sizeof(VIRTUAL_MINIPORT_LOGICAL_DEVICE));
    InitializeListHead(&(LogicalDevice->List));
    VMLockInitialize(&(LogicalDevice->LogicalDeviceLock), LockTypeExecutiveResource);
    LockInitialized = TRUE;

    if ( VMLockAcquireExclusive(&(PhysicalDevice->DeviceLock)) == TRUE ) {

        Status = STATUS_INSUFFICIENT_RESOURCES;

        Size = VIRTUAL_MINIPORT_CEIL_ALIGN(LunCreateDescriptor->Size, PhysicalDevice->BlockSize);

        //
        // Validate if we can accomodate the space for this Logical device on the physical device
        //
        if ( Size <= (PhysicalDevice->Size - PhysicalDevice->AllocatedSize) ) {

            LogicalDevice->LogicalBlocks = NULL;
            LogicalBlockCount = Size / PhysicalDevice->BlockSize;
            LogicalBlockEntrySize = sizeof(VIRTUAL_MINIPORT_LOGICAL_BLOCK_ENTRY) * LogicalBlockCount;
            if ( StorPortAllocatePool(AdapterExtension,
                                      (ULONG)LogicalBlockEntrySize, // This is ok for now.
                                      VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG,
                                      &LogicalDevice->LogicalBlocks) == STOR_STATUS_SUCCESS ) {

                RtlZeroMemory(LogicalDevice->LogicalBlocks, LogicalBlockEntrySize);
                LogicalBlockEntry = LogicalDevice->LogicalBlocks;
                for ( BlockIndex = 0; BlockIndex < LogicalBlockCount; BlockIndex++ ) {
                    LogicalBlockEntry [BlockIndex].Valid = FALSE;
                    VMBlockLockInitialize(&LogicalBlockEntry [BlockIndex].Lock);
                }

                PhysicalDevice->AllocatedSize = PhysicalDevice->AllocatedSize + Size;
                LogicalDevice->Size = Size;
                LogicalDevice->BlockSize = PhysicalDevice->BlockSize;
                LogicalDevice->MaxBlocks = LogicalBlockCount;
                LogicalDevice->PhysicalDevice = PhysicalDevice;

                InsertTailList(&(PhysicalDevice->LogicalDevices), &(LogicalDevice->List));
                PhysicalDevice->LogicalDeviceCount++;

                LogicalDevice->ThinProvison = FALSE;

                Status = STATUS_SUCCESS;
            } else {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        VMLockReleaseExclusive(&(PhysicalDevice->DeviceLock));
    }

    Status = STATUS_SUCCESS;

Cleanup:
    if ( !NT_SUCCESS(Status) ) {
        if ( LockInitialized == TRUE ) {
            VMLockUnInitialize(&(LogicalDevice->LogicalDeviceLock));
        }
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DEVICE,
            "[%s]:PhysicalDevice:%p, LogicalDevice:%p, create status:%!STATUS!",
            __FUNCTION__,
            PhysicalDevice,
            LogicalDevice,
            Status);
    return(Status);
}

NTSTATUS
VMDeviceDeleteLogicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE LogicalDevice
    )
/*++

Routine Description:

    Cleans up the caller allocated device

Arguments:

    AdapterExtension - Adapter extension needed if we need to free Stor allocations

    Device - pointer to device to be cleaned up
  
Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{

    NTSTATUS Status;
    PVIRTUAL_MINIPORT_TIERED_DEVICE PhysicalDevice;
    
    UNREFERENCED_PARAMETER(AdapterExtension);
    Status = STATUS_UNSUCCESSFUL;
    PhysicalDevice = NULL;

    if ( LogicalDevice == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(LogicalDevice->LogicalDeviceLock)) == TRUE ) {
        
        PhysicalDevice = LogicalDevice->PhysicalDevice;
        
        if ( VMLockAcquireExclusive(&(PhysicalDevice->DeviceLock)) == TRUE ) {
            
            //
            // Remove the logical device from the physical device list, and 
            // make the accounting.
            //
            RemoveEntryList(&(LogicalDevice->List));
            PhysicalDevice->LogicalDeviceCount--;
            PhysicalDevice->AllocatedSize = PhysicalDevice->AllocatedSize - LogicalDevice->Size;
            LogicalDevice->PhysicalDevice = NULL;

            if ( LogicalDevice->LogicalBlocks != NULL ) {
                StorPortFreePool(AdapterExtension, LogicalDevice->LogicalBlocks);
                LogicalDevice->LogicalBlocks = NULL;
            }
            LogicalDevice->Size = 0;
            LogicalDevice->BlockSize = 0;
            VMLockReleaseExclusive(&(PhysicalDevice->DeviceLock));
        }
        VMLockReleaseExclusive(&(LogicalDevice->LogicalDeviceLock));
    }

    VMLockUnInitialize(&(LogicalDevice->LogicalDeviceLock));
    Status = STATUS_SUCCESS;

Cleanup:
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DEVICE,
            "[%s]:PhysicalDevice:%p, LogicalDevice:%p, delete status:%!STATUS!",
            __FUNCTION__,
            PhysicalDevice,
            LogicalDevice,
            Status);
    return(Status);

}

NTSTATUS
VMDeviceBuildLogicalDeviceDetails(
    _In_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE LogicalDevice,
    _Inout_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE_DETAILS DeviceDetails
    )
/*++

Routine Description:

    Fills the details of the device details

Arguments:

    Device - pointer to device from which we need to pick the details
  
    DeviceDetails - pointer to caller allocated device details that will be filled

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{

    NTSTATUS Status;
    Status = STATUS_UNSUCCESSFUL;

    if ( LogicalDevice == NULL || DeviceDetails == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(LogicalDevice->LogicalDeviceLock)) == TRUE ) {
        DeviceDetails->BlockSize = LogicalDevice->BlockSize;
        DeviceDetails->MaxBlocks = LogicalDevice->Size / LogicalDevice->BlockSize;
        DeviceDetails->Size = LogicalDevice->Size;
        DeviceDetails->ThinProvison = LogicalDevice->ThinProvison;
        VMLockReleaseExclusive(&(LogicalDevice->LogicalDeviceLock));
    }
    Status = STATUS_SUCCESS;

Cleanup:
    return(Status);
}

static
NTSTATUS
VMDeviceReadWritePhysicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_TIERED_DEVICE Device,
    _In_ BOOLEAN Read,
    _Inout_ PVOID DataBuffer,
    _In_ PVIRTUAL_MINIPORT_LOGICAL_BLOCK_ENTRY LogicalBlockEntry
    ) 

/*++

Routine Description:

    Implements the read/write from the tiered device

Arguments:

    AdapterExtension - Adapter extension

    Device - pointer to tiered device

    Read - Indicates if the operation is a read or write

    DataBuffer - Buffer for read/write

    LogicalBlockEntry - Logical block entry this I/O is directed to
                        Caller owns this lock

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;
    PVIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY PhysicalBlockEntry, PhysicalLruBlockEntry;
    VIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY TempBlockEntry;
    PVOID Buffer;
    ULONG BlockSize;

    Status = STATUS_UNSUCCESSFUL;
    PhysicalBlockEntry = NULL;
    PhysicalLruBlockEntry = NULL;
    Buffer = NULL;
    BlockSize = 0;

    if ( VMLockAcquireExclusive(&Device->DeviceLock) == TRUE ) {
        BlockSize = Device->BlockSize;
        VMLockReleaseExclusive(&Device->DeviceLock);
    }

    if ( BlockSize != 0 && StorPortAllocatePool(AdapterExtension, BlockSize, VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG, &Buffer) != STOR_STATUS_SUCCESS ) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    if ( LogicalBlockEntry->Valid == FALSE ) {
        if ( VMLockAcquireExclusive(&Device->DeviceLock) == TRUE ) {

            //
            // Its not required to acquire the physical block entry lock when
            // the entry is being moved out of free list.
            //

            //
            // We dont have a valid mapping of LBA to PBA. Find a free physical block entry
            // and associate a mapping
            //
            if ( Device->PhysicalMemoryTierSize != 0 ) {
                PhysicalBlockEntry = (PVIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY) RemoveHeadList(&Device->PhysicalMemoryFreeList);
                if ( (PLIST_ENTRY) PhysicalBlockEntry == &Device->PhysicalMemoryFreeList ) {
                    PhysicalBlockEntry = NULL;
                } else {
                    Device->PhysicalMemoryFreeEntries--;
                }
            }

            if ( PhysicalBlockEntry == NULL && Device->FileTierSize != 0 ) {
                PhysicalBlockEntry = (PVIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY) RemoveHeadList(&Device->FileTierFreeList);
                if ( (PLIST_ENTRY) PhysicalBlockEntry == &Device->PhysicalMemoryFreeList ) {
                    PhysicalBlockEntry = NULL;
                } else {
                    Device->FileTierFreeEntries--;
                }
            }

            //
            // This can happen in case we haev done a thin provision. Else this should
            // never happen.
            //
            if ( PhysicalBlockEntry == NULL ) {
                Status = STATUS_DISK_FULL;
            } else {
                LogicalBlockEntry->PhysicalBlockAddress = PhysicalBlockEntry;
                LogicalBlockEntry->Valid = TRUE;
                if ( PhysicalBlockEntry->Tier == VMTierPhysicalMemory ) {
                    InsertTailList(&Device->PhysicalMemoryLruList, &PhysicalBlockEntry->List);
                    Device->PhysicalMemoryLruEntries++;
                }
                Status = STATUS_SUCCESS;
            }
            VMLockReleaseExclusive(&Device->DeviceLock);
        }

        //
        // If we failed to map a LBA to PBA, better quit
        //
        if ( !NT_SUCCESS(Status) ) {
            goto Cleanup;
        }
    }

    PhysicalBlockEntry = LogicalBlockEntry->PhysicalBlockAddress;

    //
    // At this time we have a valid LBA to PBA mapping. Its possible that if we have RAM tier
    // the target PBA is in lower tier in which case we need to bring that to upper tier.
    //
    VMBlockLockAcquire(&PhysicalBlockEntry->Lock);

    if ( PhysicalBlockEntry->Tier == VMTierFile ) {

        //
        // We need to find an LRU entry to push it to File tier from the physical memory
        //
        if ( VMLockAcquireExclusive(&Device->DeviceLock) == TRUE ) {
            PhysicalLruBlockEntry = (PVIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY)RemoveHeadList(&Device->PhysicalMemoryLruList);
            Device->PhysicalMemoryLruEntries--;
            VMLockReleaseExclusive(&Device->DeviceLock);
        }

        VMBlockLockAcquire(&PhysicalLruBlockEntry->Lock);

        //
        // - Read the data from file for PhysicalBlockEntry into Buffer
        // - Write the data from PhysicalLruBlockEntry to the file, update the PhysicalLruBlockEntry
        // - Copy data from Buffer to PhysicalBlockEntry, update the PhysicalBlockEntry
        //
        TempBlockEntry = *PhysicalLruBlockEntry;

        //
        // XXX Optimize the read. We need to read only if this block was ever allocated
        // needs updating the ->Valid during initialization
        //
        Status = VMFileReadWrite(Device->FileTier, Buffer, BlockSize, (ULONGLONG) PhysicalBlockEntry->TierBlockAddress, TRUE);
        if ( !NT_SUCCESS(Status) ) {
            VMRtlDebugBreak();
            goto ReadWriteFailed;
        }

        Status = VMFileReadWrite(Device->FileTier, PhysicalLruBlockEntry->TierBlockAddress, BlockSize, (ULONGLONG) PhysicalBlockEntry->TierBlockAddress, FALSE);
        if ( !NT_SUCCESS(Status) ) {
            VMRtlDebugBreak();
            goto ReadWriteFailed;
        }
        
        //
        // Update file offset now that we have copied the data
        //
        PhysicalLruBlockEntry->TierBlockAddress = PhysicalBlockEntry->TierBlockAddress;
        PhysicalLruBlockEntry->Tier = VMTierFile;
        InitializeListHead(&PhysicalLruBlockEntry->List);

        PhysicalBlockEntry->TierBlockAddress = TempBlockEntry.TierBlockAddress;
        PhysicalBlockEntry->Tier = VMTierPhysicalMemory;
        RtlCopyMemory(PhysicalBlockEntry->TierBlockAddress, Buffer, BlockSize);

        // Insert the physical memory tiery entry to LRU list
        if ( VMLockAcquireExclusive(&Device->DeviceLock) == TRUE ) {
            InsertTailList(&Device->PhysicalMemoryLruList, &PhysicalBlockEntry->List);
            Device->PhysicalMemoryLruEntries++;
            VMLockReleaseExclusive(&Device->DeviceLock);
        }

    ReadWriteFailed:
        if ( !NT_SUCCESS(Status) ) {
            
            //
            // Failure to exchange the data between the tiers will need us to insert back
            // the lru rntry we picked out.
            //
            InsertHeadList(&Device->PhysicalMemoryLruList, &PhysicalLruBlockEntry->List);
            Device->PhysicalMemoryLruEntries++;
        }
        VMBlockLockRelease(&PhysicalLruBlockEntry->Lock);

        if ( !NT_SUCCESS(Status) ) {
            goto SkipIO;
        }
    }

    //
    // At this point we are guranteed to have a block entry that is moved
    // into physical memory tier. We can now proceed with the I/O
    //

    if ( VMLockAcquireExclusive(&Device->DeviceLock) == TRUE ) {
        RemoveEntryList(&PhysicalBlockEntry->List);
        Device->PhysicalMemoryLruEntries--;
        VMLockReleaseExclusive(&Device->DeviceLock);
    }

    VMTrace(TRACE_LEVEL_VERBOSE,
            VM_TRACE_DEVICE,
            "[%s]:PhysicalBlockEntry:%p, TierBlockAddress:%p DataBuffer:%p, Read:%!bool!, BlockSize:%d",
            __FUNCTION__,
            PhysicalBlockEntry,
            PhysicalBlockEntry == NULL ? NULL : PhysicalBlockEntry->TierBlockAddress,
            DataBuffer,
            Read,
            BlockSize);
    if ( Read ) {
        RtlCopyMemory(DataBuffer, PhysicalBlockEntry->TierBlockAddress, BlockSize);
    } else {
        RtlCopyMemory(PhysicalBlockEntry->TierBlockAddress, DataBuffer, BlockSize);
    }

    if ( VMLockAcquireExclusive(&Device->DeviceLock) == TRUE ) {
        InsertTailList(&Device->PhysicalMemoryLruList, &PhysicalBlockEntry->List);
        Device->PhysicalMemoryLruEntries++;
        VMLockReleaseExclusive(&Device->DeviceLock);
    }

    Status = STATUS_SUCCESS;

SkipIO:
    VMBlockLockRelease(&PhysicalBlockEntry->Lock);

Cleanup:
    if ( Buffer != NULL ) {
        StorPortFreePool(AdapterExtension, Buffer);
    }
    return(Status);
}

NTSTATUS
VMDeviceReadWriteLogicalDevice(
    _In_ PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension,
    _In_ PVIRTUAL_MINIPORT_LOGICAL_DEVICE LogicalDevice,
    _In_ BOOLEAN Read,
    _Inout_ PVOID Buffer,
    _In_ ULONGLONG LogicalBlockNumber,
    _In_ ULONG BlockCount,
    _Inout_ PULONG TransferredBytes
    )

/*++

Routine Description:

    Implements the read/write from the logical device

Arguments:

    AdapterExtension - Adapter extension

    Device - pointer to logical device

    Read - Indicates if the operation is a read or write

    Buffer - Buffer for read/write

    LogicalBlockNumber - Starting block for the opoeration

    BlockCount - Number of subsequent blocks for the operation

    TransferredBytes - Bytes transferred

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;
    ULONGLONG BlockIndex;
    PVIRTUAL_MINIPORT_LOGICAL_BLOCK_ENTRY LogicalBlocks;

    Status = STATUS_UNSUCCESSFUL;
    UNREFERENCED_PARAMETER(Read);

    if ( LogicalDevice == NULL || Buffer == NULL || BlockCount == 0 || TransferredBytes == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    *TransferredBytes = 0;
    if ( VMLockAcquireShared(&(LogicalDevice->LogicalDeviceLock)) == TRUE ) {

        if ( (LogicalBlockNumber + BlockCount) > LogicalDevice->MaxBlocks ) {

            //
            // If we have an invalid range, dont proceed further
            //
            *TransferredBytes = 0;
            Status = STATUS_RANGE_NOT_FOUND;
        } else {
            
            //
            // Operate on one block at a time. We are safe in acquiring the block locks individually
            // as the lock ordering is guranteed across other places.
            //
            *TransferredBytes = 0;
            LogicalBlocks = LogicalDevice->LogicalBlocks;
            for ( BlockIndex = LogicalBlockNumber; BlockIndex < (LogicalBlockNumber + BlockCount); BlockIndex++ ) {

                VMBlockLockAcquire((&LogicalBlocks [BlockIndex].Lock));

                //
                // Issue a physical block I/O. I/O to physical device is issued 1
                // unit at a time.
                //               
                Status = VMDeviceReadWritePhysicalDevice(AdapterExtension,
                                                         LogicalDevice->PhysicalDevice,
                                                         Read,
                                                         Buffer,
                                                         &LogicalBlocks[BlockIndex]);
                if ( NT_SUCCESS(Status) ) {
                    
                    //
                    // Update the byte count, and progress the buffer to next block
                    //
                    *TransferredBytes = *TransferredBytes + LogicalDevice->BlockSize;
                    Buffer = (PUCHAR) Buffer + LogicalDevice->BlockSize;
                }

                VMBlockLockRelease(&(LogicalBlocks [BlockIndex].Lock));

                if ( !NT_SUCCESS(Status) ) {
                    break;
                }
            } // for loop
        }
        VMLockReleaseShared(&(LogicalDevice->LogicalDeviceLock));
    }

Cleanup:
    return(Status);
}