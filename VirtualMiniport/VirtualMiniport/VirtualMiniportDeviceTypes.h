/*++

Module Name:

    VirtualMiniportDevicetypes.h

Date:

    9-Feb-2014

Abstract:

    Module contains the type definitions of the device.

--*/

#ifndef __VIRTUAL_MINIPORT_DEVICE_TYPES_H_
#define __VIRTUAL_MINIPORT_DEVICE_TYPES_H_

#include <wdm.h>
#include <ntstrsafe.h>
#include <storport.h>

#include <VirtualMiniportWrapper.h>
#include <VirtualMiniportSupportRoutines.h>
#include <VirtualMiniportCommon.h>
#include <VirtualMiniportTrace.h>

//
// Device type definitions
//

//
// Minimum device size is 10MB, and max is 4GB
//

#define VIRTUAL_MINIPORT_MIN_DEVICE_SIZE (0x000A00000ULL)
#define VIRTUAL_MINIPORT_MAX_DEVICE_SIZE (0x0FFFFFFFFULL)

//
// Macro definitions for alignment
//

//#define VIRTUAL_MINIPORT_CEIL_ALIGN(_Size_, _Alignment_) (((_Size_)&~(_Alignment_-1))+(((_Size_)&1)*(_Alignment_)))
//#define VIRTUAL_MINIPORT_CEIL_ALIGN(_Size_, _Alignment_) (((_Size_)&~(_Alignment_-1))+((_Size_%_Alignment_)?_Alignment_:0)
#define VIRTUAL_MINIPORT_CEIL_ALIGN(_Size_, _Alignment_)(((ULONG_PTR) (_Size_) +_Alignment_ - 1) & ~(_Alignment_ - 1))

/*
    Represents a Lock for the block entries. This is an exclusive lock
    We cannot use VM_LOCK as it adds an overhead of merging all lock types
    Sometime later we can merge this lock type into VM_LOCK.
*/

typedef struct _VM_BLOCK_LOCK {
    // This is a auto-reset event - synchronization event.
    // If there are two threads blocked due to range locking
    // this will still be deadlock free. Block operations are
    // serialized
    //
    // Semantics:
    // Initial state - Set
    // Acquire - wait on event
    // Release - set the event
    //
    KEVENT LockEvent;

    //
    // Assist tracking ownership issues
    //
    PVOID OwnerThread;
    PVOID ReturnAddress;
}VM_BLOCK_LOCK, *PVM_BLOCK_LOCK;

#define VM_DEVICE_PHYSICAL_BLOCK_NUMBER(_Device_, _PhysicalBlockAddress_) (ULONGLONG)(((PUCHAR)_PhysicalBlockAddress_)-((PUCHAR)(_Device_->PhysicalBlocks))/sizeof(VIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY))
#define VM_DEVICE_PHYSICAL_BLOCK_ENTRY(_Device_, _PhysicalBlockNumber_) &(_Device_->PhysicalBlocks[_PhysicalBlockNumber_])

/*++
    Represents the logical block entry
--*/

typedef struct _VIRTUAL_MINIPORT_LOGICAL_BLOCK_ENTRY {
    //
    // Once allocated to a physical block number, this mapping
    // will never change
    //
    PVOID PhysicalBlockAddress;

    //
    // Any thread that attempts an access to this block
    // will have to wait on the acquire this lock and then
    // proceed.
    //
    VM_BLOCK_LOCK Lock;
    BOOLEAN Valid;
}VIRTUAL_MINIPORT_LOGICAL_BLOCK_ENTRY, *PVIRTUAL_MINIPORT_LOGICAL_BLOCK_ENTRY;

/*++
    Represents the Physical block entry
--*/

typedef struct _VIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY {
    
    //
    // List is meaningful when the 
    // - block entry is free
    // - bock entry translates to LRU tier
    //   
    LIST_ENTRY List;

    //
    // Tier Block number
    //
    PVOID TierBlockAddress;

    //
    // Any thread that attempts an access to this block
    // will have to wait on the acquire this lock and then
    // proceed.
    //
    VM_BLOCK_LOCK Lock;
    BOOLEAN Valid;
    VIRTUAL_MINIPORT_TIER Tier;
}VIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY, *PVIRTUAL_MINIPORT_PHYSICAL_BLOCK_ENTRY;


#define GUID_STRING_LENGTH sizeof(L"xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx")

/*++
    Represents the device
--*/

typedef struct _VIRTUAL_MINIPORT_TIERED_DEVICE {
    VM_LOCK DeviceLock;
    ULONGLONG Size;                        // Bytes
    ULONGLONG AllocatedSize;               // Bytes
    VIRTUAL_MINIPORT_BLOCK_SIZE BlockSize; // Bytes
    ULONGLONG MaxBlocks;

    ULONG LogicalDeviceCount;
    LIST_ENTRY LogicalDevices;             // List of all logical devices

    ULONG TierCount;

    //
    // RAM Tier description
    //
    ULONGLONG PhysicalMemoryTierSize;
    ULONGLONG PhysicalMemoryTierMaxBlocks;
    PVOID PhysicalMemoryTier;

    //
    // File Tier description
    //
    UNICODE_STRING FileTierFileName;
    HANDLE FileTier;
    ULONGLONG FileTierSize;
    ULONGLONG FileTierMaxBlocks;

    //
    // Physical blocks are needed to normalize the multiple logical device
    // mapping to physical device blocks.
    //
    PVOID PhysicalBlocks;

    //
    // - All block allocations starts at Physical memory FreeList
    // - Allocated blocks move to physical memory LRU list
    // - If the physical memory tier is full,
    //  - Pick an entry from the FileTier Free List
    //  - Block allocations will pick an LRU entry from physical memory tier
    //  - Moves the contents from Physical memory LRU entry to File tier entry from free list
    // - Allocates the free block to new block allocation request
    //
    // - If the block being requested is in file tier
    //  - Find an LRU entry from physical memory tier
    //  - swap the physical memory block and the file tier block
    //

    // Since we dont support thin provisioning we will always be able to satisfy the requests
    // and if the tiered device cannot hold more logical devices, we fail at the logical device
    // creation.

    ULONGLONG PhysicalMemoryFreeEntries;
    ULONGLONG FileTierFreeEntries;
    LIST_ENTRY PhysicalMemoryFreeList;
    LIST_ENTRY FileTierFreeList;

    ULONGLONG PhysicalMemoryLruEntries;
    LIST_ENTRY PhysicalMemoryLruList;
}VIRTUAL_MINIPORT_TIERED_DEVICE, *PVIRTUAL_MINIPORT_TIERED_DEVICE;

/*++
    Represents a logical device, that represents a LUN
--*/

typedef struct _VIRTUAL_MINIPORT_LOGICAL_DEVICE {
    LIST_ENTRY List;                                // List of all logical devices carved from the same physical device
    PVIRTUAL_MINIPORT_TIERED_DEVICE PhysicalDevice;
    VM_LOCK LogicalDeviceLock;

    VIRTUAL_MINIPORT_BLOCK_SIZE BlockSize;          // Bytes
    ULONGLONG Size;                                 // Bytes
    ULONGLONG MaxBlocks;                            // Block count

    BOOLEAN ThinProvison;
    PVOID LogicalBlocks;
}VIRTUAL_MINIPORT_LOGICAL_DEVICE, *PVIRTUAL_MINIPORT_LOGICAL_DEVICE;

#endif // __VIRTUAL_MINIPORT_DEVICE_TYPES_H_