/*++

 Module Name:

      VirtualMiniportInit.C

 Date:

     4-Jan-2014

 Abstract:

    Module contains the initialization code for implementing
    the virtual miniport driver. There are two categories of
    functions
        a. Storport support functions
        b. IRP handling functions

    All routines start with VM - for 'V'irtual 'M'iniport

--*/

//
// Headers
//

#include <wdm.h>
#include <ntstrsafe.h>
#include <storport.h>
#include <initguid.h>

//
// Driver specific files
//

#include <VirtualMiniportProduct.h>
#include <VirtualMiniportDefinitions.h>
#include <VirtualMiniportCommon.h>
#include <VirtualMiniportConfig.h>
#include <VirtualMiniportIoctl.h>
#include <VirtualMiniportPnp.h>
#include <VirtualMiniportScsi.h>

//
// Implementation specific headers
//

#include <VirtualMiniportAdapter.h>

//
// Events generated from the manifest
//

#include <VirtualMiniportEvents.h>

//
// WPP based event trace
//

#include <VirtualMiniportTrace.h>
#include <VirtualMiniportInit.tmh>

//
// Driver specific entry points
//

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD VMDriverUnload;

//
// Driver dispatch routine
//

DRIVER_DISPATCH VMDriverDispatchCreate;
DRIVER_DISPATCH VMDriverDispatchDeviceControl;
DRIVER_DISPATCH VMDriverDispatchClose;


//
// Private Storport based routines
//

NTSTATUS
VMDriverStorInitialize(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

//
// Define the attributes of functions
//

#pragma alloc_text(PAGED, VMDriverStorInitialize)

//
// Forward declarations
// Storport specific entry points
//

VIRTUAL_HW_FIND_ADAPTER VMHwStorFindAdapter;
HW_INITIALIZE VMHwStorInitialize;

HW_STARTIO VMHwStorStartIo;
HW_RESET_BUS VMHwStorResetBus;
HW_ADAPTER_CONTROL VMHwStorAdapterControl;
HW_FREE_ADAPTER_RESOURCES VMHwFreeAdapterResources;
HW_PROCESS_SERVICE_REQUEST VMHwProcessServiceRequest;
HW_COMPLETE_SERVICE_IRP VMHwCompleteServiceIrp;
HW_INITIALIZE_TRACING VMHwInitializeTracing;
HW_CLEANUP_TRACING VMHwCleanupTracing;
HW_TRACING_ENABLED VMHwTracingEnabled;
//HW_UNIT_CONTROL VsHwUnitControl;

//
// Global
//

PDEVICE_OBJECT gVMControlDevice;

_Use_decl_annotations_
NTSTATUS
DriverEntry(
    _In_  PDRIVER_OBJECT DriverObject,
    _In_  PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    Entry point into the driver. It registers itself with storport
    as a virtual miniport. Initialization to allow regular access
    to the driver for a control program is done here.

Arguments:

    DriverObject - pointer to the driver object

    RegistryPath - pointer to a unicode string representing the path
    to driver-specific key in the registry

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL

--*/

{
    NTSTATUS Status, Status1;
    PWCHAR Buffer;
    PVIRTUAL_MINIPORT_DEVICE_EXTENSION VMDeviceExtension;
    UNICODE_STRING DeviceName;
    BOOLEAN SymbolicNameCreated;

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    Status = STATUS_SUCCESS;
    gVMControlDevice = NULL;
    VMDeviceExtension = NULL;
    SymbolicNameCreated = FALSE;

    //
    // Enable WPP Tracing
    //
    WPP_INIT_TRACING(DriverObject,
        RegistryPath);

    VMTrace(TRACE_LEVEL_INFORMATION,
        VM_TRACE_DRIVER,
        "[%s]:Driver loading; WPP Tracing enabled",
        __FUNCTION__);

    //
    // Setup any driver specific entry points
    //

    DriverObject->DriverUnload = VMDriverUnload;

    //
    // Initialize the dispatch routines
    //

    DriverObject->MajorFunction [IRP_MJ_CREATE] = VMDriverDispatchCreate;
    DriverObject->MajorFunction [IRP_MJ_DEVICE_CONTROL] = VMDriverDispatchDeviceControl;
    DriverObject->MajorFunction [IRP_MJ_CLOSE] = VMDriverDispatchClose;

    //
    // Create the control device for our driver
    //
    RtlInitUnicodeString(&DeviceName,
        VIRTUAL_MINIPORT_CONTROL_DEVICE_NAME);

    Status = IoCreateDevice(DriverObject,
                            sizeof(VIRTUAL_MINIPORT_DEVICE_EXTENSION),
                            &DeviceName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            TRUE,
                            &gVMControlDevice
                            );
    if( !NT_SUCCESS(Status) ) {

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_DRIVER,
                "[%s]:IoCreateDevice failed for control device (Device: %wZ), Status:%!STATUS!",
                __FUNCTION__,
                &DeviceName,
                Status);
        goto Cleanup;
    }

    //
    // Set the device characterstics
    //

    gVMControlDevice->Flags |= DO_BUFFERED_IO;
    gVMControlDevice->Flags &= (~DO_DEVICE_INITIALIZING); // This is really not needed

    //
    // Initialize the global state of the driver
    //

    VMDeviceExtension = gVMControlDevice->DeviceExtension;
    RtlZeroMemory(VMDeviceExtension, sizeof(VIRTUAL_MINIPORT_DEVICE_EXTENSION));
    VMDeviceExtension->Signature = VIRTUAL_MINIPORT_SIGNATURE;
    InitializeListHead(&(VMDeviceExtension->Adapters));
    Status = VMLockInitialize(&(VMDeviceExtension->ExtensionLock),
                              LockTypeDefault);
    if( !NT_SUCCESS(Status) ) {
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_DRIVER,
                "[%s]:VMLockInitialize failed, Status:%!STATUS!",
                __FUNCTION__,
                Status);
        goto Cleanup;
    }
    RtlInitUnicodeString(&(VMDeviceExtension->ControlDeviceName),
        VIRTUAL_MINIPORT_CONTROL_DEVICE_NAME);

    //
    // Take a reference on the driver object while we hold it
    //

    ObReferenceObject(DriverObject);
    VMDeviceExtension->DriverObject = DriverObject;

    //
    // Save the registry path
    //

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   RegistryPath->MaximumLength,
                                   VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG);
    if( Buffer == NULL ) {
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_DRIVER,
                "[%s]:Failed to allocate registry buffer",
                __FUNCTION__);
        goto Cleanup;
    }

    RtlInitEmptyUnicodeString(&(VMDeviceExtension->RegistryPath),
                              Buffer,
                              RegistryPath->MaximumLength);

    RtlCopyUnicodeString(&(VMDeviceExtension->RegistryPath),
                         RegistryPath);
 
    //
    // Read the configuration from the registry
    //

    Status = VMDriverLoadConfig(&(VMDeviceExtension->RegistryPath),
                                &(VMDeviceExtension->Configuration));
    if( !NT_SUCCESS(Status) ) {
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_DRIVER,
                "[%s]:VMDriverLoadConfiguration failed, Status:%!STATUS!",
                __FUNCTION__,
                Status);
        goto Cleanup;
    }

    //
    // Create the symbolic link to our control device.
    // This is used by our control program to control
    // the device. Keep this at last. If we fail here,
    // we dont have to delete it.
    //
    RtlInitUnicodeString(&(VMDeviceExtension->ControlDeviceSymbolicName),
                         VIRTUAL_MINIPORT_CONTROL_DEVICE_SYMBOLIC_NAME);
    Status = IoCreateSymbolicLink(&(VMDeviceExtension->ControlDeviceSymbolicName),
                                  &(VMDeviceExtension->ControlDeviceName));
    if( !NT_SUCCESS(Status) ) {
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_DRIVER,
                "[%s]:IoCreateSymbolicLink for control device failed (Symbolic link: %wZ), Status:%!STATUS!",
                __FUNCTION__,
                &(VMDeviceExtension->ControlDeviceSymbolicName),
                Status);
        goto Cleanup;
    }

    SymbolicNameCreated = TRUE;

    //
    // Register the ETW provider
    //

    EventRegisterVirtualMiniportProvider();

    //
    // Save all other details we would need for our driver
    //

    //
    // Register with storport
    //

    Status = VMDriverStorInitialize(DriverObject,
                                    RegistryPath);
    if( !NT_SUCCESS(Status) ) {
        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_DRIVER,
                "[%s]:VMDriverStorInitialize failed, Status:%!STATUS!",
                __FUNCTION__,
                Status);
        goto Cleanup;
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:Successful, Status:%!STATUS!",
            __FUNCTION__,
            Status);
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:ControlDevice: %wZ, SymbolicLink: %wZ",
            __FUNCTION__,
            &DeviceName,
            &(VMDeviceExtension->ControlDeviceSymbolicName));

Cleanup:

    if ( Status != STATUS_SUCCESS ) {
        if( VMDeviceExtension != NULL ) {

            //
            // Unregister from storport
            //

            if( SymbolicNameCreated == TRUE ) {
                IoDeleteSymbolicLink(&(VMDeviceExtension->ControlDeviceSymbolicName));
            }

            //
            // Free registry buffer if we had allocated it
            //

            if( VMDeviceExtension->RegistryPath.Buffer != NULL ) {
                ExFreePoolWithTag(VMDeviceExtension->RegistryPath.Buffer,
                                  VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG);
            }

            //
            // Dereference driver object
            //

            if( VMDeviceExtension->DriverObject != NULL ) {
                ObfDereferenceObject(VMDeviceExtension->DriverObject);
            }

            //
            // Cleanup the lock; Validity of the lock is checked inside the
            // VMLockUnInitialize.
            //

            Status1 = VMLockUnInitialize(&(VMDeviceExtension->ExtensionLock));
            if( !NT_SUCCESS(Status1) ) {
                //
                // Do nothing; probably log an error
                //
            }

            //
            // Delete the control device
            //
            if( gVMControlDevice != NULL ) {
                IoDeleteDevice(gVMControlDevice);
            }

        }

        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_DRIVER,
                "[%s]:Disabling WPP Tracing",
                __FUNCTION__);

        //
        // Cleanup tracing on failure
        //

        WPP_CLEANUP(DriverObject);
    }

    return(Status);
}


_Use_decl_annotations_
VOID
VMDriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )

/*++

Routine Description:

    Called during driver unload

Arguments:

    DriverObject - pointer to the driver object

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

--*/

{
    NTSTATUS Status;
    PVIRTUAL_MINIPORT_DEVICE_EXTENSION VMDeviceExtension;

    UNREFERENCED_PARAMETER(DriverObject);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:Driver Unloading",
            __FUNCTION__);

    if( gVMControlDevice != NULL ) {

        VMDeviceExtension = gVMControlDevice->DeviceExtension;

        //
        // Cleanup anything that was done in DriverLoad
        //
        Status = VMDriverUnLoadConfig(&VMDeviceExtension->Configuration);

        //
        // Delete the symbolic name for our device
        //

        Status = IoDeleteSymbolicLink(&(VMDeviceExtension->ControlDeviceSymbolicName));
        if( !NT_SUCCESS(Status) ) {
            // Do nothing, but log an error
            VMTrace(TRACE_LEVEL_ERROR,
                    VM_TRACE_DRIVER,
                    "[%s]:IoDeleteSymbolicLink failed (SymbolicLink: %wZ), Status:%!STATUS!",
                    __FUNCTION__,
                    &(VMDeviceExtension->ControlDeviceSymbolicName),
                    Status);
        }

        if( VMDeviceExtension->RegistryPath.Buffer ) {
            ExFreePoolWithTag(VMDeviceExtension->RegistryPath.Buffer,
                              VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG);
        }

        //
        // Release the reference on the driver object
        //

        ObDereferenceObject(VMDeviceExtension->DriverObject);

        //
        // Cleanup the lock; Validity of the lock is checked inside the
        // VMLockUnInitialize.
        //

        Status = VMLockUnInitialize(&(VMDeviceExtension->ExtensionLock));
        if( !NT_SUCCESS(Status) ) {
            //
            // Do nothing; probably log an error
            //
            VMTrace(TRACE_LEVEL_ERROR,
                    VM_TRACE_DRIVER,
                    "[%s]:VMLockUnInitialize failed, Status:%!STATUS!",
                    __FUNCTION__,
                    Status);
        }

        //
        // Delete the control device
        //

        IoDeleteDevice(gVMControlDevice);
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:Disabling WPP Tracing",
            __FUNCTION__);

    WPP_CLEANUP(DriverObject);

    return;
}

//
// Dispatch Routines
//

_Use_decl_annotations_
NTSTATUS
VMDriverDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )

/*++

Routine Description:

    Handles IRP_MJ_CREATE on the control device

Arguments:

    DeviceObject - Pointer to device object; should be
                   control device

    Irp - IRP describing the I/O request

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_INVALID_PARAMETER
    STATUS_SUCCESS

--*/

{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    if( DeviceObject != gVMControlDevice ) {

        Status = STATUS_INVALID_PARAMETER_1;
    } else {

        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;

    IoCompleteRequest(Irp,
                      IO_NO_INCREMENT);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:DeviceObject:%p, Irp:%p, Status:%!STATUS!",
            __FUNCTION__,
            DeviceObject,
            Irp,
            Status);

    return(Status);
}

_Use_decl_annotations_
NTSTATUS
VMDriverDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )

/*++

Routine Description:

    Handles IRP_MJ_CLOSE on the control device

Arguments:

    DeviceObject - Pointer to device object; should be
                   control device

    Irp - IRP describing the I/O request

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_INVALID_PARAMETER
    STATUS_SUCCESS

--*/

{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    if( DeviceObject != gVMControlDevice ) {

        Status = STATUS_INVALID_PARAMETER_1;
    } else {

        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;

    IoCompleteRequest(Irp,
                      IO_NO_INCREMENT);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:DeviceObject:%p, Irp:%p, Status:%!STATUS!",
            __FUNCTION__,
            DeviceObject,
            Irp,
            Status);

    return(Status);
}

_Use_decl_annotations_
NTSTATUS
VMDriverDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )

/*++

Routine Description:

    Handles IRP_MJ_DEVICE_CONTROL on the control device

Arguments:

    DeviceObject - Pointer to device object; should be
                   control device

    Irp - IRP describing the I/O request

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_INVALID_PARAMETER
    STATUS_SUCCESS

--*/

{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    if( DeviceObject != gVMControlDevice ) {

        Status = STATUS_INVALID_PARAMETER_1;
    } else {

        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;

    IoCompleteRequest(Irp,
                      IO_NO_INCREMENT);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:DeviceObject:%p, Irp:%p, Status:%!STATUS!",
            __FUNCTION__,
            DeviceObject,
            Irp,
            Status);

    return(Status);
}

//
// Storport callback routines
//

_Use_decl_annotations_
NTSTATUS
VMDriverStorInitialize(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    Our miniport specific function that will wraps the Stage 1
    initialization. Intent of not including the whole of this
    in DriverEntry is to allow us to control when we register
    with Storport. (AND/OR possibly allowing multiple HBA creation
    if the framework allows).

Arguments:

    DriverObject - Pointer to driver object passed to the
                   DriverEntry
    
    RegistryPath - Pointer to counted UNICODE string passed
                   to the DriverEntry

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    Pass through status from StorPortInitialize
    STATUS_INVALID_PARAMETER
    STATUS_SUCCESS
    STATUS_NO_MEMORY
    STATUS_REVISION_MISMATCH
    STATUS_INSUFFICENT_RESOURCES

--*/

{
    HW_INITIALIZATION_DATA hwInitData;
    NTSTATUS Status;

    //
    // Initialize the init data for the storport and 
    // register with storport
    //

    RtlZeroMemory(&hwInitData,
        sizeof(HW_INITIALIZATION_DATA));
    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);
    hwInitData.AdapterInterfaceType = Internal;

    /*
    PHW_INITIALIZE              HwInitialize;
    PHW_STARTIO                 HwStartIo;
    PHW_INTERRUPT               HwInterrupt; // Virtual Miniports does not handle interrupts
    PHW_FIND_ADAPTER            HwFindAdapter;
    PHW_RESET_BUS               HwResetBus;
    PHW_DMA_STARTED             HwDmaStarted; // Virtual Miniports does not support DMA
    PHW_ADAPTER_STATE           HwAdapterState; // Should be NULL
    */
    //
    // Setup the storport entry points
    //

    hwInitData.HwInitialize = VMHwStorInitialize;
    hwInitData.HwStartIo = VMHwStorStartIo;
#pragma warning(push)
#pragma warning(disable:4152)
#pragma warning(disable:4054)
    hwInitData.HwFindAdapter = (PVOID) VMHwStorFindAdapter;
#pragma warning(pop)
    hwInitData.HwResetBus = VMHwStorResetBus;

    //
    // Extension size
    //
    /*
    ULONG                       DeviceExtensionSize;
    ULONG                       SpecificLuExtensionSize;
    ULONG                       SrbExtensionSize;
    */

    hwInitData.DeviceExtensionSize = sizeof(VIRTUAL_MINIPORT_ADAPTER_EXTENSION);
    hwInitData.SpecificLuExtensionSize = sizeof(VIRTUAL_MINIPORT_LUN_EXTENSION);
    hwInitData.SrbExtensionSize = sizeof(VIRTUAL_MINIPORT_SRB_EXTENSION);

    //
    // Some more members to be initialized
    //
    /*
    ULONG                       NumberOfAccessRanges; // Not used by Virtual Miniport
    PVOID                       Reserved;
    UCHAR                       MapBuffers; // Not used by Virtual Miniport
    BOOLEAN                     NeedPhysicalAddresses; // Not used by Virtual Miniport
    BOOLEAN                     TaggedQueuing;
    BOOLEAN                     AutoRequestSense;
    BOOLEAN                     MultipleRequestPerLu;
    BOOLEAN                     ReceiveEvent;
    */

    hwInitData.TaggedQueuing = TRUE;
    hwInitData.AutoRequestSense = TRUE;
    hwInitData.MultipleRequestPerLu = TRUE;
    hwInitData.ReceiveEvent = TRUE;

    //
    // Vendor and Device identification
    //
    /*
    USHORT                      VendorIdLength; // Not used by storport per MSDN
    PVOID                       VendorId;
    union {
    USHORT ReservedUshort;
    USHORT PortVersionFlags;
    };
    USHORT                      DeviceIdLength; // Not used by storport per MSDN
    PVOID                       DeviceId;
    */

    hwInitData.PortVersionFlags = SP_VER_TRACE_SUPPORT;

    //
    // Additional callbacks
    //
    /*
    PHW_STOP_ADAPTER            HwAdapterControl;
    PHW_BUILDIO                 HwBuildIo;       // Not used by Virtual Miniport
    #if (NTDDI_VERSION >= NTDDI_WIN8)
    PHW_FREE_ADAPTER_RESOURCES  HwFreeAdapterResources;
    PHW_PROCESS_SERVICE_REQUEST HwProcessServiceRequest;
    PHW_COMPLETE_SERVICE_IRP    HwCompleteServiceIrp;
    PHW_INITIALIZE_TRACING      HwInitializeTracing;
    PHW_CLEANUP_TRACING         HwCleanupTracing;
    PHW_TRACING_ENABLED         HwTracingEnabled;
    ULONG                       FeatureSupport;
    ULONG                       SrbTypeFlags;
    ULONG                       AddressTypeFlags;
    ULONG                       Reserved1;
    PHW_UNIT_CONTROL            HwUnitControl;
    #endif
    */

    hwInitData.HwAdapterControl = VMHwStorAdapterControl;
    hwInitData.HwFreeAdapterResources = VMHwFreeAdapterResources;
    hwInitData.HwProcessServiceRequest = VMHwProcessServiceRequest;
    hwInitData.HwCompleteServiceIrp = VMHwCompleteServiceIrp;
    hwInitData.HwInitializeTracing = VMHwInitializeTracing;
    hwInitData.HwCleanupTracing = VMHwCleanupTracing;
    hwInitData.HwTracingEnabled = VMHwTracingEnabled;

    hwInitData.FeatureSupport = STOR_FEATURE_VIRTUAL_MINIPORT;
    hwInitData.SrbTypeFlags = SRB_TYPE_FLAG_SCSI_REQUEST_BLOCK; // Dont set SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK for now
    hwInitData.AddressTypeFlags = ADDRESS_TYPE_FLAG_BTL8;

    //hwInitData.HwUnitControl = VsHwUnitControl; // We dont need this for Virtual Miniport

    //
    // Register with strorport
    //

    Status = StorPortInitialize(DriverObject,
                                RegistryPath,
                                &hwInitData,
                                NULL);

    /*
        Enabling storport tracing needs us to fill in STORAGE_TRACE_INIT_INFO and then
        initialize the WPP tracing. This will allow storport to call cleanup when all
        adapters are tron down. But this has to be enabled only after StorPortInitialize
        is successfully called. Problem with this is, I cannot trace anything from driver
        entry until this point. Hence going without storport specific changes for tracing 
    */

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:StorPortInitialize completed with Status:%!STATUS!",
            __FUNCTION__,
            Status);

    return(Status);
}

_Acquires_lock_(DeviceExtension->ExtensionLock)
_Releases_lock_(DeviceExtension->ExtensionLock)
_Use_decl_annotations_
ULONG
VMHwStorFindAdapter(
    _In_ PVOID DeviceExtension,
    _In_ PVOID VMHwContext,
    _In_ PVOID BusInformation,
    _In_ PVOID LowerDevice,
    _In_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION PortConfigInfo,
    _Out_ PBOOLEAN Reserved
    )

/*++

Routine Description:

    Stage 2 Initialization of storage virtual miniport driver.
    
    TODO:
    Adapter may have been already initialized and we are being called
    back on the same adapter. In these cases we may be thrashing the
    adapter. We should query the existing adapter list, if it already
    exists we dont rebuild it.

Arguments:

    DeviceExtension - Storport allocated non-paged device extension;
                      For us its PVIRTUAL_MINIPORT_ADAPTER_EXTENSION

    VMHwContext - PDO of the PnP managed for virtual miniport

    BusInformation - pointer to miniport's FDO

    LowerDevice - pointer to miniport controllerd FDO

    ArgumentString - NULL-terminated ASCII string; Device information
                     from registry such as a base parameter

    ConfigInfo - port configuration

    pReserved - Not used

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    SP_RETURN_FOUND
    SP_RETURN_ERROR
    SP_RETURN_BAD_CONFIG
    SP_RETURN_NOT_FOUND

--*/

{
    ULONG Status;
    NTSTATUS NtStatus;
    BOOLEAN AdapterInitialized;
    PVIRTUAL_MINIPORT_ADAPTER_EXTENSION AdapterExtension;
    PVIRTUAL_MINIPORT_DEVICE_EXTENSION VMDeviceExtension;


    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(VMHwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(LowerDevice);
    UNREFERENCED_PARAMETER(ArgumentString);
    UNREFERENCED_PARAMETER(PortConfigInfo);
    UNREFERENCED_PARAMETER(Reserved);

    Status = SP_RETURN_NOT_FOUND;
    NtStatus = STATUS_DEVICE_DOES_NOT_EXIST;
    AdapterInitialized = FALSE;

    AdapterExtension = (PVIRTUAL_MINIPORT_ADAPTER_EXTENSION) DeviceExtension;
    VMDeviceExtension = gVMControlDevice->DeviceExtension;

    //
    // Initialize port configuration. We update only those fields that
    // are allowed to be updated.
    //

    PortConfigInfo->MaximumTransferLength = SP_UNINITIALIZED_VALUE; // No restriction on the transfer length
    PortConfigInfo->AlignmentMask = FILE_BYTE_ALIGNMENT;
    PortConfigInfo->NumberOfAccessRanges = 0; // No AccessRanges
    PortConfigInfo->ScatterGather = TRUE;     // This is must for miniports
    PortConfigInfo->CachesData = FALSE;       // We are virtual miniport; so tell storport that we dont cache data
    PortConfigInfo->MapBuffers = STOR_MAP_ALL_BUFFERS;
    PortConfigInfo->WmiDataProvider = FALSE;  // Settting this to FALSE for now, until I understand what this means
    PortConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    PortConfigInfo->VirtualDevice = TRUE;

    /*
        We use the default values for below settings
        MaxNumberOfIO
        MaxIOsPerLun
        InitialLunQueueDepth - also set by StorPortSetDeviceQueueDepth
    */

    PortConfigInfo->BusResetHoldTime = 0;

    //
    // We need stop unit; We set NeedsSystemShutdownNotification in Inf file too
    //

    PortConfigInfo->FeatureSupport = STOR_ADAPTER_FEATURE_STOP_UNIT_DURING_POWER_DOWN;

    PortConfigInfo->NumberOfPhysicalBreaks = VMDeviceExtension->Configuration.PhysicalBreaks;
    PortConfigInfo->NumberOfBuses = VMDeviceExtension->Configuration.BusesPerAdapter;
    PortConfigInfo->MaximumNumberOfTargets = VMDeviceExtension->Configuration.TargetsPerBus;
    PortConfigInfo->MaximumNumberOfLogicalUnits = VMDeviceExtension->Configuration.LunsPerTarget;

    //
    // Now that we have setup the port configuration, initialize the adapter and attempt to
    // insert this HBA into our global list
    //

    //
    // XXX: We should first check if we know this adapter extension already and
    // if we are being called in again for re-initialize; If its re-initialize
    // we are ok to go ahead and attempt re-initialize. But if this is a new
    // adapter, we should allow creation of new adapter only if this under our
    // permitted limits of number of adapters. As of now, we dont restrict the
    // number of adapters that we allow.
    //

    NtStatus = VMAdapterInitialize(VMDeviceExtension,
                                   AdapterExtension,
                                   VMHwContext,
                                   BusInformation,
                                   LowerDevice,
                                   ArgumentString);

    if( !NT_SUCCESS(NtStatus) ) {

        Status = (NtStatus == STATUS_DEVICE_DOES_NOT_EXIST) ? SP_RETURN_NOT_FOUND : SP_RETURN_ERROR;

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_DRIVER,
                "[%s]:VMAdapterInitialize failed, Status:%!STATUS!",
                __FUNCTION__,
                NtStatus);
        goto Cleanup;
    }

    AdapterInitialized = TRUE;

    if( VMLockAcquireExclusive(&(VMDeviceExtension->ExtensionLock)) == TRUE ) {
        if( VMDeviceExtension->AdapterCount < VMDeviceExtension->Configuration.NumberOfAdapters ) {
            InsertTailList(&(VMDeviceExtension->Adapters),
                           &(AdapterExtension->List));
            VMDeviceExtension->AdapterCount++;
        }
        if( VMLockReleaseExclusive(&(VMDeviceExtension->ExtensionLock)) == FALSE ) {
            //
            // We had an error releasing the lock; Log an error
            // But since we succeeded in adding the adapter into
            // the list by now, we return success.
            // XXX - this should never happen!!!
            VMTrace(TRACE_LEVEL_ERROR,
                    VM_TRACE_DRIVER,
                    "[%s]:VMLockReleaseExclusive failed",
                    __FUNCTION__);
        }
    } else {
        //
        // We had an error acquiring the lock; Log an error
        //
        Status = SP_RETURN_ERROR;

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_DRIVER,
                "[%s]:VMLockAcquireExclusive failed",
                __FUNCTION__);
        goto Cleanup;
    }

    //
    // Report success
    //
    Status = SP_RETURN_FOUND;

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:Successfully found adapter (AdapterExtension:%p)",
            __FUNCTION__,
            AdapterExtension);

Cleanup:

    if( Status != SP_RETURN_FOUND ) {
        if( AdapterInitialized ) {
            VMAdapterUnInitialize(VMDeviceExtension,
                                  AdapterExtension);
        }
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:AdapterExtension:%p, Status:%!STATUS!",
            __FUNCTION__,
            AdapterExtension,
            Status);
    return(Status);
}

_Use_decl_annotations_
BOOLEAN VMHwStorInitialize(
    _In_ PVOID DeviceExtension
    )

/*++

Routine Description:

    Stage 3 Initialization of storage virtual miniport driver.
    Initialize any remaining fields in the device extension

Arguments:

    DeviceExtension - Storport allocated non-paged device extension;
                      For us its PVIRTUAL_MINIPORT_ADAPTER_EXTENSION

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;
    NTSTATUS NtStatus;
    
    UNREFERENCED_PARAMETER ( DeviceExtension );
    
    NtStatus = VMAdapterStart(((PVIRTUAL_MINIPORT_ADAPTER_EXTENSION) DeviceExtension)->DeviceExtension,
                              DeviceExtension);

    if ( NT_SUCCESS(NtStatus) ) {
        Status = TRUE;
    } else {
        Status = FALSE;
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:AdapterExtension:%p, Status:%!bool!",
            __FUNCTION__,
            DeviceExtension,
            Status);
    return(Status);
}

_Use_decl_annotations_
BOOLEAN
VMHwStorStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    This routine processes the SRB. Return value merely returns if the
    request was accepted for processing or not. SRBs are expected to
    complete within timeout value.

    Synchronization is expected to be handled by this routine.

Arguments:

    DeviceExtension - Storport allocated non-paged device extension;
                      For us its PVIRTUAL_MINIPORT_ADAPTER_EXTENSION

    Srb - SCSI request block representing the request

Environment:

    IRQL <= DISPATCH_LEVEL

Return Value:

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;
    NTSTATUS NtStatus;
    UCHAR SrbStatus;
    BOOLEAN CompleteHere;

    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(Srb);

    Status = FALSE;
    NtStatus = STATUS_INVALID_DEVICE_REQUEST;
    SrbStatus = SRB_STATUS_INVALID_REQUEST;
    CompleteHere = TRUE;

    /*
        Set of SRB functions that we are supposed to handle

        //
        // SRB Functions
        //

        #define SRB_FUNCTION_EXECUTE_SCSI           0x00
        #define SRB_FUNCTION_CLAIM_DEVICE           0x01
        #define SRB_FUNCTION_IO_CONTROL             0x02
        #define SRB_FUNCTION_RECEIVE_EVENT          0x03
        #define SRB_FUNCTION_RELEASE_QUEUE          0x04
        #define SRB_FUNCTION_ATTACH_DEVICE          0x05
        #define SRB_FUNCTION_RELEASE_DEVICE         0x06
        #define SRB_FUNCTION_SHUTDOWN               0x07
        #define SRB_FUNCTION_FLUSH                  0x08
        #define SRB_FUNCTION_ABORT_COMMAND          0x10
        #define SRB_FUNCTION_RELEASE_RECOVERY       0x11
        #define SRB_FUNCTION_RESET_BUS              0x12
        #define SRB_FUNCTION_RESET_DEVICE           0x13
        #define SRB_FUNCTION_TERMINATE_IO           0x14
        #define SRB_FUNCTION_FLUSH_QUEUE            0x15
        #define SRB_FUNCTION_REMOVE_DEVICE          0x16
        #define SRB_FUNCTION_WMI                    0x17
        #define SRB_FUNCTION_LOCK_QUEUE             0x18
        #define SRB_FUNCTION_UNLOCK_QUEUE           0x19
        #define SRB_FUNCTION_QUIESCE_DEVICE         0x1a
        #define SRB_FUNCTION_RESET_LOGICAL_UNIT     0x20
        #define SRB_FUNCTION_SET_LINK_TIMEOUT       0x21
        #define SRB_FUNCTION_LINK_TIMEOUT_OCCURRED  0x22
        #define SRB_FUNCTION_LINK_TIMEOUT_COMPLETE  0x23
        #define SRB_FUNCTION_POWER                  0x24
        #define SRB_FUNCTION_PNP                    0x25
        #define SRB_FUNCTION_DUMP_POINTERS          0x26
        #define SRB_FUNCTION_FREE_DUMP_POINTERS     0x27

        //
        // Define extended SRB function that will be used to identify a new
        // type of SRB that is not a SCSI_REQUEST_BLOCK. A
        // SRB_FUNCTION_STORAGE_REQUEST_BLOCK will use a SRB that is of type
        // STORAGE_REQUEST_BLOCK.
        //
        #define SRB_FUNCTION_STORAGE_REQUEST_BLOCK  0x28

    */
    switch( Srb->Function ) {
    case SRB_FUNCTION_SHUTDOWN: // System shutdown
    case SRB_FUNCTION_RESET_BUS:
    case SRB_FUNCTION_RESET_DEVICE:
    case SRB_FUNCTION_RESET_LOGICAL_UNIT:
    case SRB_FUNCTION_POWER:
        //
        // Log this at verbose level only.
        //
        SrbStatus = SRB_STATUS_INVALID_PATH_ID;
        VMTrace(TRACE_LEVEL_VERBOSE,
                VM_TRACE_DRIVER,
                "[%s]:AdapterExtension:%p, Srb:%p, SrbStatus:0x%08x, Srb:%!SRB!",
                __FUNCTION__,
                DeviceExtension,
                Srb,
                SrbStatus,
                Srb);

        break;

    case SRB_FUNCTION_EXECUTE_SCSI:
        if ( VMSrbExecuteScsi(DeviceExtension,
                              Srb) == TRUE ) {
            CompleteHere = FALSE;
            Status = TRUE;
        }
        break;

    case SRB_FUNCTION_IO_CONTROL:
        if ( VMSrbIoControl(DeviceExtension,
                            Srb) == TRUE ) {
            CompleteHere = FALSE;
            Status = TRUE;
        }
        break;

    case SRB_FUNCTION_PNP:       
        if ( VMSrbPnp(DeviceExtension,
                      (PSCSI_PNP_REQUEST_BLOCK) Srb) == TRUE ) {
            CompleteHere = FALSE;
            Status = TRUE;
        }
        break;

    case SRB_FUNCTION_STORAGE_REQUEST_BLOCK:
    default:
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;
    }

    if ( CompleteHere ) {
        Srb->SrbStatus = SrbStatus;
        StorPortNotification(RequestComplete,
                             DeviceExtension,
                             Srb);
        Status = TRUE;
    }

    return(Status);
}

_Use_decl_annotations_
BOOLEAN
VMHwStorResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId
    )

/*++

Routine Description:

    Resets the bus (PathId) that is requested to be reset.
    As part of reset processing all the pending SRBs that
    VMHwStorStartIo accepted but not completed yet will be
    completed with SRB_STATUS_BUS_RESET if applicable.

Arguments:

    DeviceExtension - Storport allocated non-paged device extension;
                      For us its PVIRTUAL_MINIPORT_ADAPTER_EXTENSION

    PathId - SCSI bus ID that needs to be reset

Environment:

    IRQL - DISPATCH_LEVEL

Return Value:

    TRUE
    FALSE

--*/

{
    BOOLEAN Status;

    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(PathId);

    Status = FALSE;

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:AdapterExtension:%p, PathID:0x%08x, Status:%!bool!",
            __FUNCTION__,
            DeviceExtension,
            PathId,
            Status);

    return(Status);
}

_Use_decl_annotations_
SCSI_ADAPTER_CONTROL_STATUS
VMHwStorAdapterControl(
    _In_  PVOID DeviceExtension,
    _In_  SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_  PVOID Parameters
    )

/*++

Routine Description:

    Synchronous operations to control the state or behavior of
    the HBA are processed here.

Arguments:

    DeviceExtension - Storport allocated non-paged device extension;
                      For us its PVIRTUAL_MINIPORT_ADAPTER_EXTENSION

    ControlType - Adapter's control operation

    Parameters - ControlType specific parameter

Environment:

    IRQL - PASSIVE_LEVEL, DISPATCH_LEVEL, DIRQL

Return Value:

    ScsiAdapterControlSuccess
    ScsiAdapterControlUnsuccessful

--*/

{
    SCSI_ADAPTER_CONTROL_STATUS Status;
    BOOLEAN LockStatus;
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST SupportedControlTypeList;
    PVIRTUAL_MINIPORT_DEVICE_EXTENSION VMDeviceExtension;

    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(ControlType);
    UNREFERENCED_PARAMETER(Parameters);

    Status = ScsiAdapterControlUnsuccessful;
    LockStatus = FALSE;
    VMDeviceExtension = ((PVIRTUAL_MINIPORT_ADAPTER_EXTENSION) DeviceExtension)->DeviceExtension;

    switch( ControlType ) {
    case ScsiQuerySupportedControlTypes: // PASSIVE_LEVEL
        
        //
        // Mark the supported control types
        //

        SupportedControlTypeList = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
        SupportedControlTypeList->SupportedTypeList [ScsiStopAdapter] = TRUE;
        SupportedControlTypeList->SupportedTypeList [ScsiRestartAdapter] = TRUE;
        SupportedControlTypeList->SupportedTypeList [ScsiSetBootConfig] = TRUE;
        SupportedControlTypeList->SupportedTypeList [ScsiSetRunningConfig] = TRUE;
        Status = ScsiAdapterControlSuccess;
        break;

    //
    // We support below types. For now they fall through until we implement them
    //
    case ScsiStopAdapter: // DIRQL, When we want to stop the adapter while running

        if( VMLockAcquireExclusive(&(VMDeviceExtension->ExtensionLock)) == TRUE ) {
            RemoveEntryList(DeviceExtension);
            VMDeviceExtension->AdapterCount--;

            if( VMLockReleaseExclusive(&(VMDeviceExtension->ExtensionLock)) == FALSE ) {
                VMTrace(TRACE_LEVEL_INFORMATION,
                        VM_TRACE_DRIVER,
                        "[%s]:Failed to release the lock",
                        __FUNCTION__);
            }
        }

        //
        // We cannot defer the deletion of the adapter as its done at PASSIVE_LEVEL
        //
        Status = ScsiAdapterControlSuccess;
        break;

    case ScsiRestartAdapter: // DIRQL, Resume from hibernate etc.
    case ScsiSetBootConfig: // PASSIVE_LEVEL
    case ScsiSetRunningConfig: // PASSIVE_LEVEL
        Status = ScsiAdapterControlSuccess;
        break;
    //
    // Unsupported control type come here. I am leaving these here in case we need
    // to implement any of these we know which ones
    //

    case ScsiPowerSettingNotification:
    case ScsiAdapterPower:
    case ScsiAdapterPoFxPowerRequired:
    case ScsiAdapterPoFxPowerActive:
    case ScsiAdapterPoFxPowerSetFState:
    case ScsiAdapterPoFxPowerControl:
    case ScsiAdapterPrepareForBusReScan:
    case ScsiAdapterSystemPowerHints:
    default:
        break;
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:AdapterExtension:%p, ControlType:[%!ADPATERCONTROL!], Status:%!ADAPTERCONTROLSTATUS!",
            __FUNCTION__,
            DeviceExtension,
            ControlType,
            Status);

    return(Status);
}

_Use_decl_annotations_
VOID
VMHwFreeAdapterResources(
    _In_  PVOID DeviceExtension
    )

/*++

Routine Description:

    This is the last callback from the storport. Free up any storport
    specific resources here.

Arguments:

    DeviceExtension - Storport allocated non-paged device extension;
                      For us its PVIRTUAL_MINIPORT_ADAPTER_EXTENSION

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    NONE

--*/

{
    UNREFERENCED_PARAMETER(DeviceExtension);

    VMAdapterStop(((PVIRTUAL_MINIPORT_ADAPTER_EXTENSION) DeviceExtension)->DeviceExtension,
                  DeviceExtension,
                  TRUE);
    VMAdapterUnInitialize(((PVIRTUAL_MINIPORT_ADAPTER_EXTENSION) DeviceExtension)->DeviceExtension,
                          DeviceExtension);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:AdapterExtension:%p",
            __FUNCTION__,
            DeviceExtension);
    return;
}

_Use_decl_annotations_
VOID
VMHwProcessServiceRequest(
    _In_ PVOID DeviceExtension,
    _In_ PIRP Irp
    )

/*++

Routine Description:

    Processes the device control IRP for IOCTL_MINIPORT_PROCESS_SERVICE_IRP
    to tell the caller that requested a reverse-callback. Irp is completed
    by StorPortCompleteServiceIrp either in this routine or later.

Arguments:

    DeviceExtension - Storport allocated non-paged device extension;
                      For us its PVIRTUAL_MINIPORT_ADAPTER_EXTENSION

    Irp - Pointer to IRP

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    NONE

--*/

{

    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(Irp);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:AdapterExtension:%p, Irp:%p",
            __FUNCTION__,
            DeviceExtension,
            Irp);
    return;
}

_Use_decl_annotations_
VOID
VMHwCompleteServiceIrp(
    _In_ PVOID DeviceExtension
    )

/*++

Routine Description :

    Called when Virtual Miniport is being removed. Complete any
    reverse-callback IRPs received in VMHwProcessServiceRequest.


Arguments :

    DeviceExtension - Storport allocated non - paged device extension;
                      For us its PVIRTUAL_MINIPORT_ADAPTER_EXTENSION

Environment :

    IRQL - PASSIVE_LEVEL

Return Value :

    NONE

--*/
{
    UNREFERENCED_PARAMETER(DeviceExtension);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:AdapterExtension:%p",
            __FUNCTION__,
            DeviceExtension);
    return;
}

_Use_decl_annotations_
VOID
VMHwInitializeTracing(
    _In_ PVOID Argument1,
    _In_ PVOID Argument2
    )

/*++

Routine Description:

    Initialize the tracing for our miniport

Arguments:

    Argument1 - First argument passed to StorPortInitialize.
                Pointer to Driver Object

    Argument2 - Second argument passed to StorPortInitialize.
                Pointer to RegistryPath

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    NONE

--*/

{
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:DriverObject:%p, RegistryPath:%wZ",
            __FUNCTION__,
            Argument1,
            Argument2);
    return;
}

_Use_decl_annotations_
VOID
VMHwCleanupTracing(
    _In_ PVOID Argument1
    )

/*++

Routine Description:

    Cleanup the tracing for our miniport

Arguments:

    Argument1 - Pointer to Driver Object

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    NONE

--*/

{
    UNREFERENCED_PARAMETER(Argument1);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:DriverObject:%p",
            __FUNCTION__,
            Argument1);
    return;
}

_Use_decl_annotations_
VOID
VMHwTracingEnabled(
    _In_ PVOID DeviceExtension,
    _In_ BOOLEAN Enabled
    )

/*++

Routine Description:

    Storport indicates Miniport to enable tracing via this entry-point

Arguments:

    DeviceExtension - Storport allocated non - paged device extension;
                      For us its PVIRTUAL_MINIPORT_ADAPTER_EXTENSION

    Enabled - TRUE to enable tracing in Miniport, else FALSE


Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    NONE

--*/

{
    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(Enabled);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_DRIVER,
            "[%s]:AdapterExtension:%p, Enable:%!bool!",
            __FUNCTION__,
            DeviceExtension,
            Enabled);
    return;
}