/*++

Module Name:

    VirtualMiniportScheduler.h

Date:

    6-Mar-2014

Abstract:

    Module contains the prototypes of scheduler

--*/

#ifndef __VIRTUAL_MINIPORT_SCHEDULER_H_
#define __VIRTUAL_MINIPORT_SCHEDULER_H_

#include <wdm.h>

#include <VirtualMiniportWrapper.h>
#include <VirtualMiniportSupportRoutines.h>
#include <VirtualMiniportTrace.h>

/*++

    Represents state of data structure.

--*/

typedef enum _VM_SCHEDULER_STATE {
    VMSchedulerUninitialized,   // Initial and can be Final state
    VMSchedulerInitializing,    // Intermediate state of initializing
    VMSchedulerInitialized,     // Initialized state, but cannot start processing yet
    VMSchedulerStarted,         // Read to process the requests
    VMSchedulerStopping,        // New requests will be rejected, queue requests being processed
    VMSchedulerStopped,         // All requests should be rejected
}VM_SCHEDULER_STATE, *PVM_SCHEDULER_STATE;


//
// Scheduler types
//


//
// Forward declarations of types for backward references
//

typedef struct _VIRTUAL_MINIPORT_SCHEDULER_WORKITEM *PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM;

/*++

    Type definition for scheduler worker routine;
    WorkItem - PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM

--*/

typedef NTSTATUS(*PVIRTUAL_MINIPORT_SCHEDULER_WORKER)(PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem, BOOLEAN AbortRequests);

/*++

    Scheduler hint will be passed in the SRB extension for the scheduler thread
    so as to control its behavior at passive level. This is the mechanism we use
    to dictate the scheduler worker's behavior.

    NOTE: This is a one way mechanism - Scheduler to Scheduler worker thread.

--*/

typedef enum _VM_SCHEDULER_HINT {
    VMSchedulerHintDefault,
    VMSchedulerHintMin = VMSchedulerHintDefault,
    VMSchedulerHintNone = VMSchedulerHintDefault,
    VMSchedulerHintStop,
    VMSchedulerHintMax = VMSchedulerHintStop
}VM_SCHEDULER_HINT, *PVM_SCHEDULER_HINT;

/*++

    Scheduler work item that scheduler processes.

--*/

typedef enum _VM_SCHEDULER_WORKITEM_STATUS {
    VMWorkItemNone,
    VMWorkItemRequestQueued,
    VMWorkItemRequestDequeued
}VM_SCHEDULER_WORKITEM_STATUS, *PVM_SCHEDULER_WORKITEM_STATUS;

#define VIRTUAL_MINIPORT_SIGNATURE_SCHEDULER_WORKITEM 'VSch'
typedef struct _VIRTUAL_MINIPORT_SCHEDULER_WORKITEM {
    LIST_ENTRY List;        // Used to link ourself into scheduler queue
    ULONG Signature;

    //
    // Hint to scheduler threads
    //

    VM_SCHEDULER_HINT SchedulerHint;

    //
    // I wanted to make this NTSTATUS, but with current scheme
    // we lose ownership of workitem as soon we pass it to worker
    // routine. So we use VM_SCHEDULER_WORKITEM_STATUS to track
    // Workitem status until we pass it to worker routine
    //
    // Current status of work item
    //  - STATUS_PENDING - queued
    //  - STATUS_SUCCESS - completed, or initialized
    //  - STATUS_REQUEST_ABORTED - if aborted
    //  - STATUS_REQUEST_CANCELLED - if cancelled
    //  - STATUS_REQUEST_NOT_ACCEPTED - if not queued
    //

    VM_SCHEDULER_WORKITEM_STATUS Status;

    PVIRTUAL_MINIPORT_SCHEDULER_WORKER Worker;
}VIRTUAL_MINIPORT_SCHEDULER_WORKITEM, *PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM;

/*

Scheduler thread binds a work item and a thread.

*/

typedef struct _VIRTUAL_MINIPORT_SCHEDULER_THREAD {
    BOOLEAN ControlItemInUse;
    BOOLEAN IsValid;
    PETHREAD Thread;
    VIRTUAL_MINIPORT_SCHEDULER_WORKITEM ControlItem;
}VIRTUAL_MINIPORT_SCHEDULER_THREAD, *PVIRTUAL_MINIPORT_SCHEDULER_THREAD;

/*++

Scheduler has its own lock
Scheduler state machine is described in scheduler module

--*/

//
// MAX threads is configured to 4 as of now.
//

#define VIRTUAL_MINIPORT_SCHEDULER_MAX_THREAD 16

typedef struct _VIRTUAL_MINIPORT_SCHEDULER_DATABASE {
    VM_LOCK SchedulerLock;                          // Should be spinlock
    PVOID Adapter;    // Backward pointer to adapter

    VM_SCHEDULER_STATE SchedulerState;
    volatile ULONG ActiveThreadCount;
    ULONG MaxThreads;

    LIST_ENTRY WorkItems;
    ULONGLONG WorkItemCount;

    //
    // Scheduler specific events.
    // -    Set at any level
    // -    Waited at only passive
    // -    These are manual reset events

    KEVENT WorkQueuedEvent;
    KEVENT ShutdownEvent;

    //
    // Each scheduler thread binds to a Control item. We can use the control item
    // to control the behavior of its owner thread. THreads are now embedded as they
    // are now controlled by VIRTUAL_MINIPORT_SCHEDULER_MAX_THREAD. Ideally it
    // would make sense to dynamically to the scheduler owner's configuration;
    //
    // i.e. if adapter owns the scheduler, we create scheduler threads based on
    // adapter's number of buses. If scheduler is owned by target, thread count
    // is configured by number of lun's on the target
    //

    VIRTUAL_MINIPORT_SCHEDULER_THREAD SchedulerThread [VIRTUAL_MINIPORT_SCHEDULER_MAX_THREAD];
}VIRTUAL_MINIPORT_SCHEDULER_DATABASE, *PVIRTUAL_MINIPORT_SCHEDULER_DATABASE;

//
// Forward declarations
//

NTSTATUS
VMSchedulerInitialize (
    _Inout_ PVOID AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase
    );

NTSTATUS
VMSchedulerUnInitialize (
    _Inout_ PVOID AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase
    );

NTSTATUS
VMSchedulerChangeState (
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase,
    _In_ VM_SCHEDULER_STATE SchedulerNewState,
    _Inout_ PVM_SCHEDULER_STATE SchedulerOldState,
    _In_ BOOLEAN Block
    );

NTSTATUS 
VMSchedulerQueryState (
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase,
    _Out_ PVM_SCHEDULER_STATE SchedulerState
    );

NTSTATUS
VMSchedulerInitializeWorkItem (
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ VM_SCHEDULER_HINT SchedulerHint,
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_WORKER Worker
    );

NTSTATUS
VMSchedulerUnInitializeWorkItem (
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem
    );

BOOLEAN
VMSchedulerScheduleWorkItem (
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase,
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ BOOLEAN AcquiredSchedulerLock
    );

#endif //__VIRTUAL_MINIPORT_SCHEDULER_H_