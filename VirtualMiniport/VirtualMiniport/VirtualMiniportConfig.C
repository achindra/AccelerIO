/*++

Module Name:

    VirtualMiniportConfig.C

Date:

    5-Feb-2014

Abstract:

    Module contains the config routines and configuration data
    All routines start with VM - for 'V'irtual 'M'iniport

--*/

#include <VirtualMiniportConfig.h>

//
// WPP based event trace
//

#include <VirtualMiniportConfig.tmh>

//
// Forward declarations of private functions
//


//
// Define the attributes of functions; declarations are in module
// specific header
//

#pragma alloc_text(INIT, VMDriverLoadConfig)

//
// Config routines
//

NTSTATUS
VMDriverLoadConfig(
    _In_ PUNICODE_STRING RegistryPath,
    _Inout_ PVIRTUAL_MINIPORT_CONFIGURATION Configuration
    )

/*++

Routine Description:

    Reads the configuration information at the initizalization

Arguments:

    RegistryPath - Pointer to counted UNICODE string passed
                   to the DriverEntry

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;
    ULONG BreakOnEntry;
    ULONG NumberOfAdapters, BusesPerAdapter, TargetsPerBus, LunsPerTarget, PhysicalBreaks;
    ULONG DeviceSizeMax;
    UNICODE_STRING DefaultVendorID, DefaultProductID, DefaultProductRevision, DefaultMetadataLocation;
    PWCHAR Buffer;
    UNICODE_STRING ParametersKeyAbsolutePath, ParametersKey;
    UNICODE_STRING ConfigKeyAbsolutePath, ConfigKey;
    RTL_QUERY_REGISTRY_TABLE Parameters [2];
    RTL_QUERY_REGISTRY_TABLE Config [11];
    USHORT BufferLength;

    //
    // Read the Parameters key. Settings in parameters is driver context
    // specific.
    //
    Buffer = NULL;
    BreakOnEntry = 0;

    BufferLength = RegistryPath->MaximumLength + (sizeof(WCHAR) *sizeof(VIRTUAL_MINIPORT_PARAMETERS_KEY)) + sizeof(WCHAR);
    Buffer = ExAllocatePoolWithTag(PagedPool,
                                   BufferLength,
                                   VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG);
    if( Buffer != NULL ) {

        RtlInitEmptyUnicodeString(&ParametersKeyAbsolutePath,
                                  Buffer,
                                  BufferLength);
        RtlInitUnicodeString(&ParametersKey,
                             VIRTUAL_MINIPORT_PARAMETERS_KEY);

        RtlCopyUnicodeString(&ParametersKeyAbsolutePath,
                             RegistryPath);
        RtlAppendUnicodeStringToString(&ParametersKeyAbsolutePath,
                                       &ParametersKey);

        Parameters [0].QueryRoutine = NULL;
        Parameters [0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
        Parameters [0].Name = L"BreakOnEntry";
        Parameters [0].EntryContext = (PVOID) &BreakOnEntry;
        Parameters [0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
        Parameters [0].DefaultData = &BreakOnEntry;
        Parameters [0].DefaultLength = sizeof(BreakOnEntry);

        Parameters [1].QueryRoutine = NULL;
        Parameters [1].Flags = 0;
        Parameters [1].Name = NULL;

        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                        ParametersKeyAbsolutePath.Buffer,
                                        Parameters,
                                        NULL,
                                        NULL
                                        );

        VMTrace(TRACE_LEVEL_INFORMATION,
                VM_TRACE_CONFIG,
                "[%s]:BreakOnEntry : %!bool!, Registry read Status:%!STATUS!",
                __FUNCTION__,
                BreakOnEntry,
                Status );

        if ( Buffer != NULL ) {

            ExFreePoolWithTag(Buffer,
                              VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG);
        }
    }

    //
    // If we have requested for break on entry, and debugger is enabled,
    // issue a breakpoint for us to allow to debug at startup
    //

    if( BreakOnEntry == TRUE) {

        VMRtlDebugBreak();
    }

    //
    // Clean the configuration before moving ahead
    //

    RtlZeroMemory(Configuration, sizeof(VIRTUAL_MINIPORT_CONFIGURATION));

    //
    // Start with defaults
    //

    NumberOfAdapters = SP_UNINITIALIZED_VALUE;
    BusesPerAdapter = SCSI_MAXIMUM_BUSES_PER_ADAPTER;
    TargetsPerBus = SCSI_MAXIMUM_TARGETS_PER_BUS; // MSDN says its 255, but storport.h has it at 128
    LunsPerTarget = SCSI_MAXIMUM_LUNS_PER_TARGET;
    PhysicalBreaks = SP_UNINITIALIZED_VALUE; // Should be SCSI_MINIMUM_PHYSICAL_BREAKS OR SCSI_MAXIMUM_PHYSICAL_BREAKS
    DeviceSizeMax = VIRTUAL_MINIPORT_MIN_DEVICE_SIZE;

    RtlInitUnicodeString(&DefaultVendorID, VIRTUAL_MINIPORT_VENDORID_STRING);
    RtlInitUnicodeString(&DefaultProductID, VIRTUAL_MINIPORT_PRODUCTID_STRING);
    RtlInitUnicodeString(&DefaultProductRevision, VIRTUAL_MINIPORT_PRODUCT_REVISION_STRING);
    RtlInitUnicodeString(&DefaultMetadataLocation, VIRTUAL_MINIPORT_METADATA_LOCATION);
    //
    // We ask the system to allocate memory for us, so we directly use configuration members
    // and we anyways dont validate the content as of now for these
    //
    RtlInitUnicodeString(&Configuration->VendorID, NULL);
    RtlInitUnicodeString(&Configuration->ProductID, NULL);
    RtlInitUnicodeString(&Configuration->ProductRevision, NULL);
    RtlInitUnicodeString(&(Configuration->MetadataLocation), NULL);
    
    BufferLength = RegistryPath->MaximumLength + (sizeof(WCHAR) *sizeof(VIRTUAL_MINIPORT_CONFIGURATION_KEY)) + sizeof(WCHAR);
    Buffer = ExAllocatePoolWithTag(PagedPool,
                                   BufferLength,
                                   VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG);
    if( Buffer == NULL ) {

        Status = STATUS_INSUFFICIENT_RESOURCES;

        //
        // This case is as good as we fail to read from registry.
        // But for now we bail out
        //

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_CONFIG,
                "[%s]:Failed to allocate buffer",
                __FUNCTION__);
        goto Cleanup;
    }

    RtlInitEmptyUnicodeString(&ConfigKeyAbsolutePath,
                              Buffer,
                              BufferLength);
    RtlInitUnicodeString(&ConfigKey,
                         VIRTUAL_MINIPORT_CONFIGURATION_KEY);

    RtlCopyUnicodeString(&ConfigKeyAbsolutePath,
                         RegistryPath);
    RtlAppendUnicodeStringToString(&ConfigKeyAbsolutePath,
                                   &ConfigKey);

    Config [0].QueryRoutine = NULL;
    Config [0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [0].Name = L"NumberOfAdapters";
    Config [0].EntryContext = (PVOID) &NumberOfAdapters;
    Config [0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [0].DefaultData = &NumberOfAdapters;
    Config [0].DefaultLength = sizeof(NumberOfAdapters);

    Config [1].QueryRoutine = NULL;
    Config [1].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [1].Name = L"BusesPerAdapter";
    Config [1].EntryContext = (PVOID) &BusesPerAdapter;
    Config [1].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [1].DefaultData = &BusesPerAdapter;
    Config [1].DefaultLength = sizeof(BusesPerAdapter);
    
    Config [2].QueryRoutine = NULL;
    Config [2].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [2].Name = L"TargetsPerBus";
    Config [2].EntryContext = (PVOID) &TargetsPerBus;
    Config [2].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [2].DefaultData = &TargetsPerBus;
    Config [2].DefaultLength = sizeof(TargetsPerBus);

    Config [3].QueryRoutine = NULL;
    Config [3].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [3].Name = L"LunsPerTarget";
    Config [3].EntryContext = (PVOID) &LunsPerTarget;
    Config [3].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [3].DefaultData = &LunsPerTarget;
    Config [3].DefaultLength = sizeof(LunsPerTarget);

    Config [4].QueryRoutine = NULL;
    Config [4].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [4].Name = L"PhysicalBreaks";
    Config [4].EntryContext = (PVOID) &PhysicalBreaks;
    Config [4].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [4].DefaultData = &PhysicalBreaks;
    Config [4].DefaultLength = sizeof(PhysicalBreaks);

    Config [5].QueryRoutine = NULL;
    Config [5].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [5].Name = L"VendorID";
    Config [5].EntryContext = (PVOID) &Configuration->VendorID;
    Config [5].DefaultType = (REG_SZ << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [5].DefaultData = &DefaultVendorID;
    Config [5].DefaultLength = 0;

    Config [6].QueryRoutine = NULL;
    Config [6].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [6].Name = L"ProductID";
    Config [6].EntryContext = (PVOID) &Configuration->ProductID;
    Config [6].DefaultType = (REG_SZ << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [6].DefaultData = &DefaultProductID;
    Config [6].DefaultLength = 0;

    Config [7].QueryRoutine = NULL;
    Config [7].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [7].Name = L"ProductRevision";
    Config [7].EntryContext = (PVOID) &Configuration->ProductRevision;
    Config [7].DefaultType = (REG_SZ << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [7].DefaultData = &DefaultProductRevision;
    Config [7].DefaultLength = 0;

    Config [8].QueryRoutine = NULL;
    Config [8].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [8].Name = L"DeviceSizeMax";
    Config [8].EntryContext = (PVOID) &DeviceSizeMax;
    Config [8].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [8].DefaultData = &DeviceSizeMax;
    Config [8].DefaultLength = sizeof(DeviceSizeMax);

    //
    // Metadata path should be folder with a trailing '\'. We dont do error checking against
    // it for now. But good to add.
    //
    Config [9].QueryRoutine = NULL;
    Config [9].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Config [9].Name = L"MetadataLocation";
    Config [9].EntryContext = (PVOID) &Configuration->MetadataLocation;
    Config [9].DefaultType = (REG_SZ << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Config [9].DefaultData = &DefaultMetadataLocation;
    Config [9].DefaultLength = 0;

    Config [10].QueryRoutine = NULL;
    Config [10].Flags = 0;
    Config [10].Name = NULL;

    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                    ConfigKeyAbsolutePath.Buffer,
                                    Config,
                                    NULL,
                                    NULL
                                    );
    if( NT_SUCCESS(Status) ) {

        //
        // We read DWORDs from registry; but the actual fields are UCHARs and ULONGs.
        // Make sure we normalize the values as we read from the registry
        //
        Configuration->NumberOfAdapters = NumberOfAdapters;
        Configuration->BusesPerAdapter = (UCHAR) ((BusesPerAdapter > SCSI_MAXIMUM_BUSES_PER_ADAPTER) ? SCSI_MAXIMUM_BUSES_PER_ADAPTER : BusesPerAdapter);
        Configuration->TargetsPerBus = (UCHAR) ((TargetsPerBus > SCSI_MAXIMUM_TARGETS_PER_BUS) ? SCSI_MAXIMUM_TARGETS_PER_BUS : TargetsPerBus);
        Configuration->LunsPerTarget = (UCHAR) ((LunsPerTarget > SCSI_MAXIMUM_LUNS_PER_TARGET) ? SCSI_MAXIMUM_LUNS_PER_TARGET : LunsPerTarget);
        Configuration->PhysicalBreaks = PhysicalBreaks;

        Configuration->DeviceSizeMax = DeviceSizeMax;
        Configuration->FreeUnicodeStringsAtUnload = TRUE;
    } else {

        Configuration->NumberOfAdapters = SP_UNINITIALIZED_VALUE;
        Configuration->BusesPerAdapter = SCSI_MAXIMUM_BUSES_PER_ADAPTER;
        Configuration->TargetsPerBus = SCSI_MAXIMUM_TARGETS_PER_BUS;
        Configuration->LunsPerTarget = SCSI_MAXIMUM_LUNS_PER_TARGET;
        Configuration->PhysicalBreaks = SP_UNINITIALIZED_VALUE;

        RtlInitUnicodeString(&Configuration->VendorID, VIRTUAL_MINIPORT_VENDORID_STRING);
        RtlInitUnicodeString(&Configuration->ProductID, VIRTUAL_MINIPORT_PRODUCTID_STRING);
        RtlInitUnicodeString(&Configuration->ProductRevision, VIRTUAL_MINIPORT_PRODUCT_REVISION_STRING);

        Configuration->DeviceSizeMax = VIRTUAL_MINIPORT_MIN_DEVICE_SIZE;

        RtlInitUnicodeString(&Configuration->MetadataLocation, VIRTUAL_MINIPORT_METADATA_LOCATION);
        Configuration->FreeUnicodeStringsAtUnload = FALSE;

        VMTrace(TRACE_LEVEL_ERROR,
                VM_TRACE_CONFIG,
                "[%s]:RtlQueryRegistryValues failed Status:%!STATUS!, setting defaults, and forcing success",
                __FUNCTION__,
                Status);

        Status = STATUS_SUCCESS;
    }

    RtlUnicodeStringToAnsiString(&(Configuration->AnsiVendorID), &(Configuration->VendorID), TRUE);
    RtlUnicodeStringToAnsiString(&(Configuration->AnsiProductID), &(Configuration->ProductID), TRUE);
    RtlUnicodeStringToAnsiString(&(Configuration->AnsiProductRevision), &(Configuration->ProductRevision), TRUE);
    Configuration->FreeAnsiStringsAtUnload = TRUE;

    if( Buffer != NULL ) {

        ExFreePoolWithTag(Buffer,
                          VIRTUAL_MINIPORT_GENERIC_ALLOCATION_TAG);
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_CONFIG,
            "[%s]:NumberOfAdapters:%d, BusesPerAdapter:%d, TargetsPerBus:%d, LunsPerTarget:%d, PhysicalBreaks:%d",
            __FUNCTION__,
            Configuration->NumberOfAdapters,
            Configuration->BusesPerAdapter,
            Configuration->TargetsPerBus,
            Configuration->LunsPerTarget,
            Configuration->PhysicalBreaks);
    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_CONFIG,
            "[%s]:DeviceSize:0x%I64x, VendorID:%S, ProductID:%S, ProductRevision:%S, MetadataLocation:%S",
            __FUNCTION__,
            Configuration->DeviceSizeMax,
            Configuration->VendorID.Buffer,
            Configuration->ProductID.Buffer,
            Configuration->ProductRevision.Buffer,
            Configuration->MetadataLocation.Buffer);

Cleanup:

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_CONFIG,
            "[%s]:Status:%!STATUS!",
            __FUNCTION__,
            Status);
    return(Status);
}

NTSTATUS
VMDriverUnLoadConfig(
    _Inout_ PVIRTUAL_MINIPORT_CONFIGURATION Configuration
    )

/*++

Routine Description:

    Reads the configuration information at the initizalization

Arguments:

    Configuration - configuration to be unloaded

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    Other NTSTATUS from callee

--*/

{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Configuration);


    if ( Configuration->FreeUnicodeStringsAtUnload == TRUE ) {
        RtlFreeUnicodeString(&Configuration->VendorID);
        RtlFreeUnicodeString(&Configuration->ProductID);
        RtlFreeUnicodeString(&Configuration->ProductRevision);
        RtlFreeUnicodeString(&Configuration->MetadataLocation);
        Configuration->FreeUnicodeStringsAtUnload = FALSE;
    }

    if ( Configuration->FreeAnsiStringsAtUnload == TRUE ) {
        RtlFreeAnsiString(&Configuration->AnsiVendorID);
        RtlFreeAnsiString(&Configuration->AnsiProductID);
        RtlFreeAnsiString(&Configuration->AnsiProductRevision);
        Configuration->FreeAnsiStringsAtUnload = FALSE;
    }

    Status = STATUS_SUCCESS;

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_CONFIG,
            "[%s]:FreeUnicodeStringsAtUnload:%!bool!, FreeAnsiStringsAtUnload:%!bool!, Status:%!STATUS!",
            __FUNCTION__,
            Configuration->FreeUnicodeStringsAtUnload,
            Configuration->FreeAnsiStringsAtUnload,
            Status);

    return(Status);
}