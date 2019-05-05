/*++

Module Name:

    VirtualMiniportScheduler.C

Date:

    7-Mar-2014

Abstract:

    Module implements SRB scheduling.
    All routines start with VM - for 'V'irtual 'M'iniport

    Idea:

    SRB scheduler as of now is in its infancy stage. All that we want to do now is
    schedule an SRB to passive level. Will make use of a single thread to process
    the requests. Scheduler is attached to the adapter and adater start and stop will
    control the scheduler state machine.

        |<-------------------------------------------------------------
        |<---------------------------|           |---------------->|  |
    UnInitialized->Initializing->Initialized->Started->Stopping->Stopped
         |    ^         |             |                   ^         ^
         |    |---------|             |                   |         |
         |              |             |-------------------|---------|
         |              |---------------------------------|---------|
         |                                                          ^
         |----------------------------------------------------------|

    State Transition Rules:
    *   Transition to Stopped is possible when ActiveThreadCount is 0
    *   Transition to Stopping will block any new items to be queued
    *   Stopping state will allow queued workitems to be processed

    However as this scheduler matures I expect to evolve this scheduler with below
    attributes:

    *   Lun's have associated priority
    *   SCSI requests 
    *   SRB requests are queued to priority based scheduler database 
        (similar to thread scheduler).
    *   SRB processor threads will scan the priority based queue and
        process the SRBs
    
    NOTES: 

    *   Scheduler synchronization happens at DISPATCH_LEVEL
    *   This module has no knowledge of SCSI requests. It accesses only
        scheduler database and SRB extension blocks. SRB extension blocks
        are used as if they are work items.

--*/

//
// Headers
//

#include <VirtualMiniportScheduler.h>

//
// WPP based event trace
//

#include <VirtualMiniportScheduler.tmh>

//
// Forward declarations of private functions
//

static
NTSTATUS
VMSchedulerEvaluateState(
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase,
    _In_ VM_SCHEDULER_STATE SchedulerNewState,
    _Inout_ PVM_SCHEDULER_STATE EffectiveState
    );

static
VOID
VMSchedulerUpdateActiveThreadCount(
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase
    );

KSTART_ROUTINE VMSchedulerThread;

//
// Module specific globals
//

//
// Define the attributes of functions; declarations are in module
// specific header
//

#pragma alloc_text(NONPAGED, VMSchedulerInitialize)
#pragma alloc_text(NONPAGED, VMSchedulerUnInitialize)
#pragma alloc_text(NONPAGED, VMSchedulerChangeState)
#pragma alloc_text(NONPAGED, VMSchedulerQueryState)
#pragma alloc_text(NONPAGED, VMSchedulerInitializeWorkItem)
#pragma alloc_text(NONPAGED, VMSchedulerUnInitializeWorkItem)
#pragma alloc_text(NONPAGED, VMSchedulerScheduleWorkItem)

#pragma alloc_text(NONPAGED, VMSchedulerEvaluateState)
#pragma alloc_text(NONPAGED, VMSchedulerScheduleWorkItem)
#pragma alloc_text(NONPAGED, VMSchedulerThread)

//
// Driver specific routines
//

NTSTATUS
VMSchedulerInitialize (
    _Inout_ PVOID AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase
    )

/*++

Routine Description:

    Initializes the scheduler instance of the adapter

Arguments:

    AdapterExtension - Adapter instance that owns this scheduler

    SchedulerDatabase - Scheduler instance to be initialized

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    Any other NTSTATUS code from callee

--*/

{
    NTSTATUS Status, Status1;
    ULONG Index;
    VM_SCHEDULER_STATE OldState;
    OBJECT_ATTRIBUTES ThreadAttributes;
    HANDLE Thread;

    if ( AdapterExtension == NULL || SchedulerDatabase == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    SchedulerDatabase->SchedulerState = VMSchedulerUninitialized;

    RtlZeroMemory ( SchedulerDatabase, sizeof(VIRTUAL_MINIPORT_SCHEDULER_DATABASE) );

    SchedulerDatabase->SchedulerState = VMSchedulerInitializing;

    Status = VMLockInitialize ( &(SchedulerDatabase->SchedulerLock), LockTypeSpinlock );
    if ( !NT_SUCCESS ( Status ) ) {
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_SCHEDULER,
                "[%s]:VMLockInitialize failed, Status:%!STATUS!",
                __FUNCTION__,
                Status);
        goto Cleanup;
    }

    SchedulerDatabase->Adapter = AdapterExtension;
 
    SchedulerDatabase->ActiveThreadCount = 0;
    SchedulerDatabase->MaxThreads = VIRTUAL_MINIPORT_SCHEDULER_MAX_THREAD;

    InitializeListHead ( &(SchedulerDatabase->WorkItems) );
    SchedulerDatabase->WorkItemCount = 0;

    KeInitializeEvent ( &(SchedulerDatabase->WorkQueuedEvent),
                        NotificationEvent,
                        FALSE );
    KeInitializeEvent ( &(SchedulerDatabase->ShutdownEvent),
                        NotificationEvent,
                        FALSE );

    //
    // Now initialize the scheduler thread
    //

    InitializeObjectAttributes ( &ThreadAttributes,
                                 NULL,
                                 OBJ_KERNEL_HANDLE,
                                 NULL,
                                 NULL );

    for ( Index = 0; Index < SchedulerDatabase->MaxThreads; Index++ ) {

        SchedulerDatabase->SchedulerThread [Index].ControlItemInUse = FALSE;
        VMSchedulerInitializeWorkItem(&(SchedulerDatabase->SchedulerThread [Index].ControlItem),
                                      VMSchedulerHintStop,
                                      NULL);

        Status = PsCreateSystemThread(&Thread,
                                      GENERIC_ALL,
                                      &ThreadAttributes,
                                      NULL,
                                      NULL,
                                      VMSchedulerThread,
                                      SchedulerDatabase);
        if ( !NT_SUCCESS ( Status ) ) {
            VMTrace ( TRACE_LEVEL_ERROR,
                      VM_TRACE_SCHEDULER,
                      "[%s]:AdapterExtension:%p, PsCreateSystemThread failed with Status:%!STATUS!",
                      __FUNCTION__,
                      AdapterExtension,
                      Status
                      );
            VMRtlDebugBreak();
            goto Cleanup;
        }

        Status = ObReferenceObjectByHandle(Thread,
                                           GENERIC_ALL,
                                           *PsThreadType,
                                           KernelMode,
                                           &SchedulerDatabase->SchedulerThread [Index].Thread,
                                           NULL);
        if ( !NT_SUCCESS(Status) ) {
            VMTrace(TRACE_LEVEL_ERROR,
                    VM_TRACE_SCHEDULER,
                    "[%s]:Failed to reference thread object by handle, Status:%!STATUS!",
                    __FUNCTION__,
                    Status);
            VMRtlDebugBreak();
            ObCloseHandle(Thread,
                          KernelMode);
            goto Cleanup;
        }
         
        //
        // From this point onwards we are not alone
        //

        if ( VMLockAcquireExclusive(&(SchedulerDatabase->SchedulerLock)) == TRUE ) {
            SchedulerDatabase->SchedulerThread [Index].IsValid = TRUE;
            SchedulerDatabase->ActiveThreadCount++;
            VMLockReleaseExclusive(&(SchedulerDatabase->SchedulerLock));
        }
    }

    Status = VMSchedulerChangeState ( SchedulerDatabase,
                                      VMSchedulerInitialized,
                                      &OldState,
                                      TRUE);

    if ( VMLockAcquireExclusive(&(SchedulerDatabase->SchedulerLock)) == TRUE ) {
        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_SCHEDULER,
                "[%s]:AdapterExtension:%p, SchedulerDatabase:%p (State: %!VMSCHEDULERSTATE!), ActiveThreadCount:%d",
                __FUNCTION__,
                AdapterExtension,
                SchedulerDatabase,
                SchedulerDatabase->SchedulerState,
                SchedulerDatabase->ActiveThreadCount
                );
        VMLockReleaseExclusive(&(SchedulerDatabase->SchedulerLock));
    }

Cleanup:

    if ( !NT_SUCCESS( Status ) && SchedulerDatabase != NULL) {

        //
        // We can potentially get into infinite loop waiting for scheduler to stop.
        // If everything is ok, this should never happen. CAUTION!!!
        //
        Status1 = VMSchedulerChangeState(SchedulerDatabase,
                                         VMSchedulerStopped,
                                         &OldState,
                                         TRUE);
        if ( !NT_SUCCESS(Status1) ) {
            VMTrace(TRACE_LEVEL_ERROR,
                    VM_TRACE_SCHEDULER,
                    "[%s]:AdapterExtension:%p, SchedulerDatabase:%p Transition to State: %!VMSCHEDULERSTATE! failed, Status:%!STATUS!",
                    __FUNCTION__,
                    AdapterExtension,
                    SchedulerDatabase,
                    VMSchedulerStopped,
                    Status1
                    );
        }
    }

    return(Status);
}

NTSTATUS
VMSchedulerUnInitialize (
    _Inout_ PVOID AdapterExtension,
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase
    )

/*++

Routine Description:

    Uninitializes the scheduler

Arguments:

    AdapterExtension - AdapterExtension whose scheduler database needs to
                       be uninitialized

    SchedulerDatabase - Scheduler instance to be uninitialized

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    TRUE
    FALSE

--*/

{
    NTSTATUS Status;
    VM_SCHEDULER_STATE SchedulerState, SchedulerOldState;

    Status = VMSchedulerQueryState(SchedulerDatabase,
                                   &SchedulerState);
    if ( NT_SUCCESS(Status) ) {
        switch ( SchedulerState ) {
        case VMSchedulerUninitialized:
        case VMSchedulerStopped:
            Status = STATUS_SUCCESS;
            break;

        case VMSchedulerInitialized:
        case VMSchedulerInitializing:
        case VMSchedulerStarted:
        case VMSchedulerStopping:
            Status=  VMSchedulerChangeState(SchedulerDatabase,
                                            VMSchedulerStopped,
                                            &SchedulerOldState,
                                            TRUE);
            VMTrace(TRACE_LEVEL_ERROR,
                    VM_TRACE_SCHEDULER,
                    "[%s]:Undesirable Scheduler state:SchedulerDatabase:%p %!VMSCHEDULERSTATE!, Status:%!STATUS!",
                    __FUNCTION__,
                    SchedulerDatabase,
                    VMSchedulerStopped,
                    Status);
            break;

        default:
            Status = STATUS_INTERNAL_ERROR;
            VMTrace(TRACE_LEVEL_ERROR,
                    VM_TRACE_SCHEDULER,
                    "[%s]:Invalid state (%!VMSCHEDULERSTATE!) for Scheduler: SchedulerDatabase:%p, Status:%!STATUS!",
                    __FUNCTION__,
                    SchedulerState,
                    SchedulerDatabase,
                    Status);
            break;
        }
    }

//Cleanup:

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_SCHEDULER,
            "[%s]:AdapterExtension:%p SchedulerDatabase:%p, %!VMSCHEDULERSTATE!, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            SchedulerDatabase,
            SchedulerState,
            Status);

    return(Status);
}

static
VOID
VMSchedulerUpdateActiveThreadCount(
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase
)

/*++

Routine Description:

    Scans the thread list and udpates the status

    This does not acquire any locks. Caller is expected to acquire
    the scheduler lock.

Arguments:

    SchedulerDatabase - Scheduler instance that needs to be checked

Environment:

    IRQL - DISPATCH_LEVEL

Return Value:

    None

--*/

{
    NTSTATUS Status;
    ULONG Index;
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = 0;

    for ( Index = 0; Index < SchedulerDatabase->MaxThreads; Index++ ) {
        if ( SchedulerDatabase->SchedulerThread [Index].IsValid == TRUE ) {
            
            //
            // This wait is done with 0 timeout; system will return immediately
            // but will tell us the signalled state of the object.
            //
            Status = KeWaitForSingleObject(SchedulerDatabase->SchedulerThread [Index].Thread,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           &Timeout);
            if ( Status == STATUS_SUCCESS ) {
                ASSERT(SchedulerDatabase->SchedulerThread [Index].ControlItem.Status == STATUS_SUCCESS);
                SchedulerDatabase->SchedulerThread [Index].IsValid = FALSE;
                SchedulerDatabase->ActiveThreadCount--;
                ObDereferenceObject(SchedulerDatabase->SchedulerThread [Index].Thread);
                SchedulerDatabase->SchedulerThread [Index].Thread = VIRTUAL_MINIPORT_INVALID_POINTER;
                VMTrace(TRACE_LEVEL_INFORMATION,
                        VM_TRACE_SCHEDULER,
                        "[%s]:Scheduler Thread Terminated",
                        __FUNCTION__);
            }
        }
    } // For each thread

    VMTrace(TRACE_LEVEL_VERBOSE,
            VM_TRACE_SCHEDULER,
            "[%s]:SchedulerDatabase:%p, ActiveThreadCount:%d",
            __FUNCTION__,
            SchedulerDatabase,
            SchedulerDatabase->ActiveThreadCount);
}

static
NTSTATUS
VMSchedulerEvaluateState(
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase,
    _In_ VM_SCHEDULER_STATE SchedulerNewState,
    _Inout_ PVM_SCHEDULER_STATE EffectiveState
    )

/*++

Routine Description:

    Scheduler state machine evaluation. This routines does NOT
    change the state, but runs the state machine and returns the
    effective state and the status. Caller is responsible for
    committing the state changes.

    This does not acquire any locks. Caller is expected to acquire
    the scheduler lock.

Arguments:

    Scheduler - Scheduler instance

    SchedulerNewState - State to transition to

    EffectiveState - Returns the new state to be transitioned to

Environment:

    IRQL - DISPATCH_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_PENDING
    STATUS_INVALID_PARAMETER
    Any other NTSTATUS

--*/

{
    NTSTATUS Status;

    //
    // Update active thread status before running the state machine
    //

    VMSchedulerUpdateActiveThreadCount(SchedulerDatabase);

    switch ( SchedulerDatabase->SchedulerState ) {

    case VMSchedulerUninitialized:

        switch ( SchedulerNewState ) {
        case VMSchedulerUninitialized:
        case VMSchedulerInitializing:
        case VMSchedulerStopped:
            *EffectiveState = SchedulerNewState;
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        break; // VMSchedulerUninitialized

    case VMSchedulerInitializing:
        switch ( SchedulerNewState ) {
        case VMSchedulerInitializing:
        case VMSchedulerInitialized:
            *EffectiveState = SchedulerNewState;
            Status = STATUS_SUCCESS;
            break;


        case VMSchedulerUninitialized:
        case VMSchedulerStopping:
        case VMSchedulerStopped:

            //
            // If request is for Stopping state, if situation permits
            // state is updated to stopped. Similarly if request is for
            // Stopped/Uninitialize, if situation does not permit, state
            // is changed stopping
            //

            if ( SchedulerDatabase->ActiveThreadCount == 0 ) {
                *EffectiveState = (SchedulerNewState == VMSchedulerUninitialized) ?
                VMSchedulerUninitialized : VMSchedulerStopped;
                Status = STATUS_SUCCESS;
            } else {
                //
                // If there are threads around, 
                //
                *EffectiveState = VMSchedulerStopping;
                Status = STATUS_PENDING;
            }
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        break; // VMSchedulerInitializing

    case VMSchedulerInitialized:
        switch ( SchedulerNewState ) {
        case VMSchedulerStarted:
            *EffectiveState = SchedulerNewState;
            Status = STATUS_SUCCESS;
            break;

        case VMSchedulerUninitialized:
        case VMSchedulerStopping:
        case VMSchedulerStopped:

            //
            // If request is for Stopping state, if situation permits
            // state is updated to stopped. Similarly if request is for
            // Stopped/Uninitialize, if situation does not permit, state
            // is changed stopping
            //

            if ( SchedulerDatabase->ActiveThreadCount == 0 ) {
                *EffectiveState = (SchedulerNewState == VMSchedulerUninitialized) ?
                VMSchedulerUninitialized : VMSchedulerStopped;
                Status = STATUS_SUCCESS;
            } else {
                //
                // If there are threads around, 
                //
                *EffectiveState = VMSchedulerStopping;
                Status = STATUS_PENDING;
            }
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;

        }
        break; // VMSchedulerInitialized

    case VMSchedulerStarted:
        switch ( SchedulerNewState ) {
        case VMSchedulerStopping:
        case VMSchedulerStopped:
            if ( SchedulerDatabase->ActiveThreadCount == 0 ) {
                *EffectiveState = VMSchedulerStopped;
                Status = STATUS_SUCCESS;
            } else {
                *EffectiveState = VMSchedulerStopping;
                Status = STATUS_PENDING;
            }
            break;
        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        break; // VMSchedulerStarted

    case VMSchedulerStopping:
        switch ( SchedulerNewState ) {
        case VMSchedulerStopped:
            if ( SchedulerDatabase->ActiveThreadCount == 0 ) {
                *EffectiveState = SchedulerNewState;
                Status = STATUS_SUCCESS;
            } else {
                //
                // We will still be in stopping state.
                // Basically no state transition
                //
                Status = STATUS_PENDING;
            }
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        break; // VMSchedulerStopping

    case VMSchedulerStopped:
        switch ( SchedulerNewState ) {
        case VMSchedulerUninitialized:
            *EffectiveState = SchedulerNewState;
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        break; // VMSchedulerStopped

    default:
        // Unrecognized current state
        Status = STATUS_INVALID_PARAMETER;
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_SCHEDULER,
                "[%s]:Invalid scheduler state %!VMSCHEDULERSTATE!",
                __FUNCTION__,
                SchedulerDatabase->SchedulerState);
        break; // SchedulerState
    } //End of switch

    VMTrace(TRACE_LEVEL_VERBOSE,
            VM_TRACE_SCHEDULER,
            "[%s]:SchedulerDatabase:%p, CurrentState:%!VMSCHEDULERSTATE!, EffectiveNewState:%!VMSCHEDULERSTATE!, Status:%!STATUS!",
            __FUNCTION__,
            SchedulerDatabase,
            SchedulerDatabase->SchedulerState,
            *EffectiveState,
            Status);

    return(Status);
}

NTSTATUS
VMSchedulerChangeState(
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase,
    _In_ VM_SCHEDULER_STATE SchedulerNewState,
    _Inout_ PVM_SCHEDULER_STATE SchedulerOldState,
    _In_ BOOLEAN Block
    ) 

/*++

Routine Description:

    Scheduler state machine

Arguments:

    Scheduler - Scheduler instance

    SchedulerNewState - State to transition to

    SchedulerOldState - Return Old State

    Block - Indicates if we should block until the state change

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_PENDING
    STATUS_INVALID_PARAMETER
    Any other NTSTATUS

--*/

{
    NTSTATUS Status;
    ULONG Index;
    VM_SCHEDULER_STATE EffectiveNewState, PreviousState;
    LARGE_INTEGER DelayFiveSeconds;

    DelayFiveSeconds.QuadPart = -5000LL * 1000LL * 10LL; // 5 seconds

    //
    // Validate the paremeters
    //

    if (SchedulerDatabase == NULL ||
        !(SchedulerNewState >= VMSchedulerUninitialized  && SchedulerNewState <= VMSchedulerStopped) ||
        SchedulerOldState == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(SchedulerDatabase->SchedulerLock)) == TRUE ) {

        //
        // Grab the current state
        //

        *SchedulerOldState = SchedulerDatabase->SchedulerState;
        Status = VMSchedulerEvaluateState(SchedulerDatabase,
                                          SchedulerNewState,
                                          &EffectiveNewState);
    
        if ( Status != STATUS_INVALID_PARAMETER ) {
            SchedulerDatabase->SchedulerState = EffectiveNewState;
        }

        VMTrace(TRACE_LEVEL_VERBOSE,
                VM_TRACE_SCHEDULER,
                "[%s]:SchedulerDatabase:%p, TransitionedState %!VMSCHEDULERSTATE!->%!VMSCHEDULERSTATE!, Status:%!STATUS!",
                __FUNCTION__,
                SchedulerDatabase,
                *SchedulerOldState,
                EffectiveNewState,
                Status);

        //
        // Now that we had been through state transitions, see if there are any state
        // transition actions to be performed, before giving up the lock.
        //

        if ( Status == STATUS_PENDING ) {

            VMTrace(TRACE_LEVEL_VERBOSE,
                VM_TRACE_SCHEDULER,
                "[%s]:SchedulerDatabase:%p, Performing state transition actions",
                __FUNCTION__,
                SchedulerDatabase);

            //
            // We end with pending status only when we transition from any other state to
            // Stopping. This requires us to tell the scheduler threads to stop
            //

            //
            // Queue the shutdown work item too. This is not really not needed as we our
            // shutdown event will make sure to wake up the threads and exit. But queing
            // this work item will help us test the work item aborts.
            //
            // Work item should be queued only if thread is active and stop item has not
            // been queued.
            //
            for ( Index = 0; Index < SchedulerDatabase->MaxThreads; Index++ ) {
                if ( SchedulerDatabase->SchedulerThread[Index].IsValid == TRUE &&
                     SchedulerDatabase->SchedulerThread [Index].ControlItemInUse == FALSE ) {
                    
                    //
                    // Mark the work item busy, initialize with scheduler hint
                    // and queue the work item.
                    //
                    SchedulerDatabase->SchedulerThread [Index].ControlItemInUse = TRUE;
                    VMSchedulerInitializeWorkItem(&(SchedulerDatabase->SchedulerThread [Index].ControlItem),
                                                  VMSchedulerHintStop,
                                                  NULL);
                    if ( VMSchedulerScheduleWorkItem(SchedulerDatabase,
                                                     &(SchedulerDatabase->SchedulerThread [Index].ControlItem),
                                                     TRUE) == TRUE ) {

                        VMTrace(TRACE_LEVEL_VERBOSE,
                                VM_TRACE_SCHEDULER,
                                "[%s]:Queued work with ScheduleHint:%!VMSCHEDULERHINT!",
                                __FUNCTION__,
                                VMSchedulerHintStop);
                    }
                }
            }

            //
            // Signal the shutdown event
            //

            KeSetEvent(&(SchedulerDatabase->ShutdownEvent),
                       IO_NO_INCREMENT,
                       FALSE);
        }

        if ( VMLockReleaseExclusive(&(SchedulerDatabase->SchedulerLock)) != TRUE ) {
            VMTrace(TRACE_LEVEL_ERROR,
                    VM_TRACE_SCHEDULER,
                    "[%s]:VMLockReleaseExclusive failed",
                    __FUNCTION__);

            Status = STATUS_INTERNAL_ERROR;
        }
    } else {
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_SCHEDULER,
                "[%s]:VMLockAcquireExclusive failed",
                __FUNCTION__);

            Status = STATUS_INTERNAL_ERROR;
    }

    //
    // Caller wants us to transition to desired state before returning back to him
    //

    if ( Status == STATUS_PENDING && Block == TRUE ) {

        Index = 0;
        do {
            
            if ( Index % 10 == 0 ) {
                VMRtlDebugBreak();
            }

            //
            // We will not check for lock acquire/release failures here
            // They are overhead with else statement!!!
            //
            if ( VMLockAcquireExclusive(&(SchedulerDatabase->SchedulerLock)) == TRUE ) {
                
                //
                // Check and udpate the 
                //
                PreviousState = SchedulerDatabase->SchedulerState;
                Status = VMSchedulerEvaluateState(SchedulerDatabase,
                                                  SchedulerNewState,
                                                  &EffectiveNewState);
                if ( Status != STATUS_INVALID_PARAMETER ) {
                    SchedulerDatabase->SchedulerState = EffectiveNewState;
                }

                VMTrace(TRACE_LEVEL_VERBOSE,
                        VM_TRACE_SCHEDULER,
                        "[%s]:SchedulerDatabase:%p, TransitionedState %!VMSCHEDULERSTATE!->%!VMSCHEDULERSTATE!, Status:%!STATUS!",
                        __FUNCTION__,
                        SchedulerDatabase,
                        PreviousState,
                        EffectiveNewState,
                        Status);

                VMLockReleaseExclusive(&(SchedulerDatabase->SchedulerLock));
            }

            VMRtlDelayExecution(&DelayFiveSeconds);
            Index++;

        } while ( Status == STATUS_PENDING);
    }


Cleanup:
    return(Status);
}

NTSTATUS
VMSchedulerQueryState(
_Inout_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase,
_Out_ PVM_SCHEDULER_STATE SchedulerState
)
/*++

Routine Description:

    Query the scheduler state

Arguments:

    SchedulerDatabase - Scheduler instance to be queried

    SchedulerState - State information populated to the caller

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER
    Any oter NTSTATUS

--*/
{
    NTSTATUS Status;

    if ( SchedulerDatabase == NULL || SchedulerState == NULL ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if ( VMLockAcquireExclusive(&(SchedulerDatabase->SchedulerLock)) == TRUE ) {
        *SchedulerState = SchedulerDatabase->SchedulerState;
        VMLockReleaseExclusive(&(SchedulerDatabase->SchedulerLock));
    }

    Status = STATUS_SUCCESS;

Cleanup:

    return(Status);
}

NTSTATUS
VMSchedulerInitializeWorkItem(
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ VM_SCHEDULER_HINT SchedulerHint,
    _In_opt_ PVIRTUAL_MINIPORT_SCHEDULER_WORKER Worker
    )

/*++

Routine Description:

    Simply initializes a work item. Callers outside scheduler embed scheduler
    workitem as their header and call this from their wrapper

Arguments:

    WorkItem - Caller allocated Work item that needs to be initialized

    Schedulerhint - Scheduler hint

    Worker - Pointer to worker function; Can be NULL on some SchedulerHints

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER

--*/
{
    NTSTATUS Status;

    if ( WorkItem == NULL || 
        !(SchedulerHint >= VMSchedulerHintMin && SchedulerHint <= VMSchedulerHintMax) ||
        (Worker == NULL && SchedulerHint != VMSchedulerHintStop) ) {

        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    InitializeListHead(&(WorkItem->List));
    WorkItem->Signature = VIRTUAL_MINIPORT_SIGNATURE_SCHEDULER_WORKITEM;
    WorkItem->SchedulerHint = SchedulerHint;
    WorkItem->Status = VMWorkItemNone;
    WorkItem->Worker = Worker;

    Status = STATUS_SUCCESS;

Cleanup:
    return(Status);
}

NTSTATUS
VMSchedulerUnInitializeWorkItem(
    _Inout_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem
    )

/*++

Routine Description:

    WorkItem to be uninitialized

Arguments:

    WorkItem - Scheduler work item instance that needs to be queue

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_INVALID_PARAMETER

    NOTE: WorkItems are not thread safe themselves, its the callers responsibility to
          ensure a work item is inserted into the queue once at a time etc.

          It is expected that work item is not part of the queue when being uninitialized
--*/

{
    NTSTATUS Status;

    if ( WorkItem == NULL || WorkItem->Signature != VIRTUAL_MINIPORT_SIGNATURE_SCHEDULER_WORKITEM) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }


    Status = STATUS_SUCCESS;

Cleanup:

    return(Status);
}

BOOLEAN
VMSchedulerScheduleWorkItem(
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase,
    _In_ PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem,
    _In_ BOOLEAN AcquiredSchedulerLock
    )

/*++

Routine Description:

    Schedule an workitem; This functions only schedules a workitem

Arguments:

    SchedulerDatabase - Scheduler instance to which the work item should be queued

    WorkItem - Scheduler work item instance that needs to be queued

    AcquiredSchedulerLock - TRUE is lock is already acquired, else FALSE

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;
    BOOLEAN HeadInsert;

    UNREFERENCED_PARAMETER(SchedulerDatabase);
    UNREFERENCED_PARAMETER(WorkItem);
    UNREFERENCED_PARAMETER(AcquiredSchedulerLock);

    if ( SchedulerDatabase == NULL || WorkItem == NULL ) {
        Status = FALSE;
        goto Cleanup;
    }

    Status = FALSE;
    HeadInsert = FALSE;

    //
    // Acquire lock if caller has not acquired it already; in which case
    // we own the responsibility to release the lock
    //

    if ( AcquiredSchedulerLock == FALSE ) {
        if ( VMLockAcquireExclusive(&(SchedulerDatabase->SchedulerLock)) == FALSE) {
            goto Cleanup;
        }
    }

    //
    // 1. Validate the scheduler state
    // 2. Make queing decision based on ScheduleHint in workitem
    // 3. Schedule/Queue the work item into scheduler database
    // 4. Mark status os work item to be pending
    // 5. Update the tracking variables
    // 6. Wake up the scheduler threads to process the work
    //

    if ( SchedulerDatabase->SchedulerState == VMSchedulerStarted ) {
        HeadInsert = (WorkItem->SchedulerHint == VMSchedulerHintStop) ? TRUE : FALSE;

        if ( HeadInsert == TRUE ) {
            InsertHeadList(&(SchedulerDatabase->WorkItems),
                            &(WorkItem->List));
        } else {
            InsertTailList((PLIST_ENTRY)&(SchedulerDatabase->WorkItems),
                            &(WorkItem->List));
        }

        WorkItem->Status = VMWorkItemRequestQueued;

        SchedulerDatabase->WorkItemCount++;
        KeSetEvent(&(SchedulerDatabase->WorkQueuedEvent),
                    IO_NO_INCREMENT,
                    FALSE);
        Status = TRUE;
    } else {
        VMTrace(TRACE_LEVEL_VERBOSE,
                VM_TRACE_SCHEDULER,
                "[%s]:SchedulerDatabase:%p, Scheduler not ready, SchedulerState:%!VMSCHEDULERSTATE!",
                __FUNCTION__,
                SchedulerDatabase,
                SchedulerDatabase->SchedulerState);
    }

    if ( AcquiredSchedulerLock == FALSE ) {
        VMLockReleaseExclusive(&(SchedulerDatabase->SchedulerLock));
    }

Cleanup:

    return(Status);
}

VOID
VMSchedulerThread(
    _In_ PVOID Context
    )

/*++

Routine Description:

    Scheduler thread that processes the work items. This routine
    does not alter the state machine state. When scheduler thread
    is asked to stop, it simply completes the pending work and
    terminates. The responsibility to waiting until the scheduler
    has reached to halt is with invoker of state machine.

Arguments:

    Context - Scheduler that owns this thread

Environment:

    IRQL - <= DISPATCH_LEVEL

Return Value:

    NONE

--*/

{
    NTSTATUS Status, WorkItemStatus;
    PVIRTUAL_MINIPORT_SCHEDULER_DATABASE SchedulerDatabase;
    PKEVENT EventObjects [2] = {NULL, NULL};
    PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM WorkItem;
    ULONGLONG WorkItemCount;
    BOOLEAN StopScheduler;

    SchedulerDatabase = (PVIRTUAL_MINIPORT_SCHEDULER_DATABASE) Context;
    WorkItemCount = 0;
    StopScheduler = FALSE;

    EventObjects [0] = &SchedulerDatabase->ShutdownEvent;
    EventObjects [1] = &SchedulerDatabase->WorkQueuedEvent;

    //
    // Now get in a loop waiting, processing the work items
    //

    do {

        Status = KeWaitForMultipleObjects(2,
                                          EventObjects,
                                          WaitAny,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL,
                                          NULL);
        switch ( Status ) {
        case STATUS_WAIT_0:
            
            //
            // Shutdown event
            //
            StopScheduler = TRUE;

            break;

        case STATUS_WAIT_1:
            
            //
            // Workqueued event
            //
            WorkItem = NULL;
            if ( VMLockAcquireExclusive(&(SchedulerDatabase->SchedulerLock)) == TRUE ) {
            
                if ( SchedulerDatabase->WorkItemCount > 0 ) {
                    WorkItem = (PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM) RemoveHeadList(&(SchedulerDatabase->WorkItems));
                    if ( (PLIST_ENTRY) WorkItem == (PLIST_ENTRY) &SchedulerDatabase->WorkItems ) {
                        VMRtlDebugBreak();
                    }
                    SchedulerDatabase->WorkItemCount--;
                }

                //
                // Make a copy of workitem count to be used outside lock for logging
                //
                WorkItemCount = SchedulerDatabase->WorkItemCount;

                if ( SchedulerDatabase->WorkItemCount == 0 ) {
                    //
                    // Clear event so we go back to wait only when there are work items
                    //
                    KeClearEvent(EventObjects[1]);
                }
                VMLockReleaseExclusive(&(SchedulerDatabase->SchedulerLock));
            }

            if ( WorkItem != NULL ) {

                VMTrace(TRACE_LEVEL_INFORMATION,
                        VM_TRACE_SCHEDULER,
                        "[%s]:SchedulerDatabase:%p, WorkItemCount:%I64d, WorkItem:%p, SchedulerHint:%!VMSCHEDULERHINT!",
                        __FUNCTION__,
                        SchedulerDatabase,
                        WorkItemCount,
                        WorkItem,
                        WorkItem->SchedulerHint);
                
                WorkItem->Status = VMWorkItemRequestDequeued;
                switch ( WorkItem->SchedulerHint ) {
                
                case VMSchedulerHintDefault:
                    
                    WorkItemStatus = WorkItem->Worker(WorkItem,
                                                      FALSE);
                    break;

                case VMSchedulerHintStop:
                    //
                    // We should cancel/abort all the pending requests
                    //
                    StopScheduler = TRUE;
                    break;

                default:
                    break;
                }
            }
            break;

        default:
            break;
        } // End STATUS_WAIT_xx
    
    } while ( StopScheduler == FALSE);

    //
    // Now that we are here, we were asked to stop processing the work items.
    // We will issue cancel/abort requests to all the work items

    WorkItemCount = 0;
    do {
        //
        // Initial part of this is same as what we do in case of STATUS_WAIT_1
        // Howevere we need to duplicate this code here as we need to do this
        // in direct loop as opposed to a reaction of events in case of
        // STATUS_WAIT_1
        //
        WorkItem = NULL;
        if ( VMLockAcquireExclusive(&(SchedulerDatabase->SchedulerLock)) == TRUE ) {
            if ( SchedulerDatabase->WorkItemCount > 0 ) {
                WorkItem = (PVIRTUAL_MINIPORT_SCHEDULER_WORKITEM)RemoveHeadList(&(SchedulerDatabase->WorkItems));
                ASSERT((PLIST_ENTRY) WorkItem == &(SchedulerDatabase->WorkItems));
                SchedulerDatabase->WorkItemCount--;
            }
            WorkItemCount = SchedulerDatabase->WorkItemCount;
            if ( SchedulerDatabase->WorkItemCount == 0 ) {
                //
                // Clear event so we go back to wait only when there are work items
                //
                KeClearEvent(EventObjects [1]);
            }
            VMLockReleaseExclusive(&(SchedulerDatabase->SchedulerLock));

            if ( WorkItem != NULL ) {
                WorkItem->Status = VMWorkItemRequestDequeued;
                WorkItemStatus = WorkItem->Worker(WorkItem,
                                                  FALSE);
                //
                // It is illegal to access WorkItem from this point onwards. In our
                // model we embed the work item in the SRB, so the moment we complete
                // the SRB we give the ownership to storport.
                //

                //WorkItem->Status = WorkItemStatus;
            }
        }

    } while ( WorkItemCount != 0 );

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_SCHEDULER,
            "[%s]:SchedulerDatabase:%p, WorkItems during abort:%I64d",
            __FUNCTION__,
            SchedulerDatabase,
            WorkItemCount
            );

//Cleanup:

    PsTerminateSystemThread(Status);
}