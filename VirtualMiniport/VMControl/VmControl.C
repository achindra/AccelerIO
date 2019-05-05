/*++

Module Name:

    VMControl.C

Date:

    2-Feb-2014

Abstract:

    Module implements the control program to control the virtual miniport.

--*/

#include<tchar.h>
#include<windows.h>
#include<ntddscsi.h>
#include<setupapi.h>

#include<stdio.h>
#include<stdlib.h>

#include<winioctl.h>
#include<initguid.h>
#include<VMControl.h>
#include<VirtualMiniportCommon.h>

#define MAX_BUFFER 255

// These needs to be fixed; as we expect in future multiple VM HBA devices 
// Open the handle to VM HBA device

DWORD
VMControlOpenHBADevice(
    _Out_ HANDLE *hDevice
    )
{
    DWORD Status;
    HDEVINFO hDevInfo;
    DWORD DeviceIndex, DevIndex;
    DWORD RequiredSize;
    PSP_DEVICE_INTERFACE_DATA pDeviceInterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA pDeviceInterfaceDetailData;
    SP_DEVINFO_DATA SpDevInfoData;

    Status = ERROR_NOT_FOUND;
    hDevInfo = NULL;
    pDeviceInterfaceData = NULL;
    pDeviceInterfaceDetailData = NULL;

    if( hDevice == NULL ) {
        _tprintf(TEXT("VMControlOpenHBADevice: Invalid parameter\n"));
        Status = ERROR_INVALID_PARAMETER;
        goto Cleanup;
    }

    *hDevice = INVALID_HANDLE_VALUE;

    hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_VIRTUAL_MINIPORT_DEVICE,
                                   NULL,
                                   NULL,
                                   DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

    if( hDevInfo != NULL ) {
        DevIndex = 0;
        ZeroMemory(&SpDevInfoData,
                   sizeof(SP_DEVINFO_DATA));
        SpDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        
        while( SetupDiEnumDeviceInfo(hDevInfo,
            DevIndex,
            &SpDevInfoData) ) {

            DeviceIndex = 0;
            pDeviceInterfaceData = (PSP_DEVICE_INTERFACE_DATA) malloc(sizeof(SP_DEVICE_INTERFACE_DATA));
            pDeviceInterfaceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

            while( SetupDiEnumDeviceInterfaces(hDevInfo,
                                               &SpDevInfoData,
                                               &GUID_DEVINTERFACE_VIRTUAL_MINIPORT_DEVICE,
                                               DeviceIndex,
                                               pDeviceInterfaceData) ) {

                pDeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) malloc(sizeof(TCHAR) *MAX_BUFFER);
                RequiredSize = sizeof(TCHAR) *MAX_BUFFER;

                if( pDeviceInterfaceDetailData != NULL ) {
                    pDeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                    if( SetupDiGetDeviceInterfaceDetail(hDevInfo,
                                                        pDeviceInterfaceData,
                                                        pDeviceInterfaceDetailData,
                                                        RequiredSize,
                                                        &RequiredSize,
                                                        NULL) == TRUE ) {

                        if ( *hDevice == INVALID_HANDLE_VALUE || *hDevice == NULL ) {
                            *hDevice = CreateFile(pDeviceInterfaceDetailData->DevicePath,
                                                  GENERIC_ALL,
                                                  0,
                                                  NULL,
                                                  OPEN_EXISTING,
                                                  FILE_ATTRIBUTE_NORMAL,
                                                  NULL);

                            if ( *hDevice == NULL ) {

                                Status = GetLastError();
                                _tprintf(TEXT("CreateFile failed (Error: 0x%08x)\n"), Status);
                                break;
                            } else {
                                _tprintf(TEXT("Opened device %s successfully\n"), pDeviceInterfaceDetailData->DevicePath);
                                Status = ERROR_SUCCESS;
                                break;
                            }
                        } else {
                            _tprintf(TEXT("Found device %s\n"), pDeviceInterfaceDetailData->DevicePath);
                        }
                    } // InterfaceDetail
                }
            DeviceIndex++;
            }
            
            DevIndex++;
        }
    }

Cleanup:

    if( pDeviceInterfaceDetailData != NULL ) {
        free(pDeviceInterfaceDetailData);
    }

    if( pDeviceInterfaceData != NULL ) {
        free(pDeviceInterfaceData);
    }

    if( hDevInfo != NULL && SetupDiDestroyDeviceInfoList(hDevInfo) == FALSE ) {
        _tprintf(TEXT("SetupDiDestroyDeviceInfoList Failed (Error: 0x%08x)\n"),
                 GetLastError());
    }

    return(Status);
}

// Close the handle to VM HBA device

DWORD
VMControlCloseHBADevice(
    _In_ HANDLE hDevice
    )
{
    DWORD Status;

    if( hDevice == NULL ) {
        _tprintf(TEXT("VMControlCloseHBADevice: Invalid Parameter\n"));
        Status = ERROR_INVALID_PARAMETER;
        goto Cleanup;
    }

    if( CloseHandle(hDevice) != TRUE ) {
        Status = GetLastError();
    }

Cleanup:
    return(Status);
}


// Open handle to control device
DWORD
VMOpenControlDevice(
    _In_ HANDLE *hDevice
    )
{
    DWORD Status;

    if( hDevice == NULL ) {
        _tprintf(TEXT("VMOpenControlDevice: Invalid Parameter\n"));
        Status = ERROR_INVALID_PARAMETER;
        goto Cleanup;
    }

    *hDevice = CreateFile(VIRTUAL_MINIPORT_CONTROL_DEVICE,
                          GENERIC_ALL,
                          0,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    if( *hDevice == INVALID_HANDLE_VALUE ) {
        Status = GetLastError();
        _tprintf(TEXT("VMOpenControlDevice: CreateFile failed (Error: 0x%08x)\n"),
                 Status);
    }

Cleanup:
    return(Status);
}

DWORD
VMCloseControlDevice(
    _In_ HANDLE *hDevice
    )
{
    DWORD Status;

    if( hDevice == NULL ) {
        _tprintf(TEXT("VMCloseControlDevice: Invalid Parameter\n"));
        Status = ERROR_INVALID_PARAMETER;
        goto Cleanup;
    }

    Status = CloseHandle(*hDevice);

Cleanup:
    return(Status);
}

PVOID
AllocateInitializeIoctlDescriptor(
    _In_ ULONG AdditionalSize,
    _Inout_ ULONG *BufferLength,
    _In_ ULONG ControlCode
    ) 
{
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR Buffer;

    *BufferLength = sizeof(VIRTUAL_MINIPORT_IOCTL_DESCRIPTOR) +AdditionalSize;

    Buffer = (PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR) malloc(*BufferLength);
    ZeroMemory(Buffer, sizeof(BufferLength));

    Buffer->SrbIoControl.HeaderLength = sizeof(SRB_IO_CONTROL);
    Buffer->SrbIoControl.ControlCode = ControlCode;
    Buffer->SrbIoControl.Length = (((UCHAR)sizeof(VIRTUAL_MINIPORT_IOCTL_DESCRIPTOR) +AdditionalSize) - 
                                    (UCHAR) FIELD_OFFSET(VIRTUAL_MINIPORT_IOCTL_DESCRIPTOR, RequestResponse)
                                    );
    CopyMemory(&(Buffer->SrbIoControl.Signature), VIRTUAL_MINIPORT_IOCTL_SIGNATURE, sizeof(VIRTUAL_MINIPORT_IOCTL_SIGNATURE));
    return (Buffer);
}

VOID
DisplayGUID(
    _In_ GUID *Guid
    ) 
{
    _tprintf(TEXT("%x-%x-%x-%x%x-%x%x%x%x%x%x"),
             Guid->Data1,
             Guid->Data2,
             Guid->Data3,
             Guid->Data4 [0],
             Guid->Data4 [1],
             Guid->Data4 [2],
             Guid->Data4 [3],
             Guid->Data4 [4],
             Guid->Data4 [5],
             Guid->Data4 [6],
             Guid->Data4 [7]);
}

VOID
IoctlDummy(
    _In_ HANDLE hDevice
    ) 
{
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR Buffer;
    ULONG BufferLength, Index;

    _tprintf(TEXT("Executing ---DUMMY IOCTL---\n"));

    Buffer = AllocateInitializeIoctlDescriptor(0,
                                               &BufferLength,
                                               IOCTL_VIRTUAL_MINIPORT_DUMMY);

    if ( !DeviceIoControl(hDevice,
                          IOCTL_SCSI_MINIPORT,
                          Buffer,
                          BufferLength,
                          Buffer,
                          BufferLength,
                          &BufferLength,
                          NULL
                          ) ) {
        _tprintf(TEXT("DeviceIoControlFailed\n"));
        goto Cleanup;
    }

    for ( Index = 0; Index < Buffer->SrbIoControl.Length; Index++ ) {
        _tprintf(TEXT("%c"), Buffer->RequestResponse.DummyData.Buffer [Index]);
    }

    _tprintf(TEXT("\n"));

    free(Buffer);
Cleanup:
    return;
}

DWORD
IoctlCreateBus(
    _In_ HANDLE hDevice,
    _Inout_ ULONG *BusCount
    ) 
{
    ULONG Index;
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR Buffer;
    ULONG BufferLength;
    DWORD Status;

    *BusCount = 0;
    Status = ERROR_SUCCESS;
    //
    // Create a bus
    //
    _tprintf(TEXT("\n\nExecuting ---Creating Bus---\n"));
    Buffer = AllocateInitializeIoctlDescriptor(MAX_BUFFER,
                                               &BufferLength,
                                               IOCTL_VIRTUAL_MINIPORT_CREATE_BUS);

    if ( !DeviceIoControl(hDevice,
                          IOCTL_SCSI_MINIPORT,
                          Buffer,
                          BufferLength,
                          Buffer,
                          BufferLength,
                          &BufferLength,
                          NULL) ) {
        Status = GetLastError();
        _tprintf(TEXT("DeviceIoControlFailed, Status:0x%08x\n"), Status);
        goto Cleanup;
    }

    Status = Buffer->SrbIoControl.ReturnCode;
    if ( Status == ERROR_SUCCESS ) {
        _tprintf(TEXT("Successfully create a bus\n"));
        _tprintf(TEXT("  AdapterID: "));
        DisplayGUID(&(Buffer->RequestResponse.AdapterDetails.AdapterId));
        _tprintf(TEXT("\n"));
        *BusCount = Buffer->RequestResponse.AdapterDetails.BusCount;
        _tprintf(TEXT("  BusCount:%d\n"), *BusCount);
        for ( Index = 0; Index < Buffer->RequestResponse.AdapterDetails.BusCount; Index++ ) {
            _tprintf(TEXT("  BusID: %d\n"), Buffer->RequestResponse.AdapterDetails.Buses [Index]);
        }
    } else {
        _tprintf(TEXT("CreateBus returned with status: 0x%08x"), Status);
    }

    free(Buffer);

Cleanup:
    return(Status);
}

DWORD
IoctlCreateTarget(
    _In_ HANDLE hDevice,
    _In_ UCHAR Bus,
    _Inout_ ULONG *TargetCount
    )
{
    ULONG Index;
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR Buffer;
    ULONG BufferLength;
    DWORD Status;

    *TargetCount = 0;
    Status = ERROR_SUCCESS;
    //
    // Create a bus
    //
    _tprintf(TEXT("\n\nExecuting ---Creating Target on Bus: %d---\n"), Bus);
    Buffer = AllocateInitializeIoctlDescriptor(MAX_BUFFER,
                                               &BufferLength,
                                               IOCTL_VIRTUAL_MINIPORT_CREATE_TARGET);

    Buffer->RequestResponse.CreateTarget.Bus = Bus;
    Buffer->RequestResponse.CreateTarget.BlockSize = VMBlockSizeDefault;
    Buffer->RequestResponse.CreateTarget.Size = 0xfff00000; //(50+150) * 1024 * 1024; // Should be cummulative of tier sizes
    Buffer->RequestResponse.CreateTarget.TierCount = 1;
    
    //
    // Tier description
    Buffer->RequestResponse.CreateTarget.TierDescription [0].Tier = VMTierPhysicalMemory;
    Buffer->RequestResponse.CreateTarget.TierDescription [0].TierSize = 0xfff00000;//50 * 1024 * 1024;

    //Buffer->RequestResponse.CreateTarget.TierDescription [1].Tier = VMTierFile;
    //Buffer->RequestResponse.CreateTarget.TierDescription [1].TierSize = 150 * 1024 * 1024;

    _tprintf(TEXT("Creating physical device of size: 0x%I64x\n"), Buffer->RequestResponse.CreateTarget.Size);
    if ( !DeviceIoControl(hDevice,
                          IOCTL_SCSI_MINIPORT,
                          Buffer,
                          BufferLength,
                          Buffer,
                          BufferLength,
                          &BufferLength,
                          NULL) ) {
        Status = GetLastError();
        _tprintf(TEXT("DeviceIoControlFailed, Status:0x%08x\n"), Status);
        goto Cleanup;
    }

    Status = Buffer->SrbIoControl.ReturnCode;
    if ( Status == ERROR_SUCCESS ) {
        _tprintf(TEXT("Successfully create a target\n"));
        _tprintf(TEXT("  AdapterID: "));
        DisplayGUID(&(Buffer->RequestResponse.BusDetails.AdapterId));
        _tprintf(TEXT("\n"));
        *TargetCount = Buffer->RequestResponse.BusDetails.TargetCount;
        _tprintf(TEXT("  TargetCount:%d\n"), *TargetCount);
        for ( Index = 0; Index < Buffer->RequestResponse.BusDetails.TargetCount; Index++ ) {
            _tprintf(TEXT("  TargetID: %d\n"), Buffer->RequestResponse.BusDetails.Targets [Index]);
        }
    } else {
        _tprintf(TEXT("CreateTarget returned with status: 0x%08x"), Status);
    }

    free(Buffer);

Cleanup:
    return(Status);
}

DWORD
IoctlCreateLun(
    _In_ HANDLE hDevice,
    _In_ UCHAR Bus,
    _In_ UCHAR Target,
    _Inout_ ULONG *LunCount
    ) 
{
    ULONG Index;
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR Buffer;
    ULONG BufferLength;
    DWORD Status;

    *LunCount = 0;
    Status = ERROR_SUCCESS;

    //
    // Create a bus
    //
    _tprintf(TEXT("\n\nExecuting ---Creating Lun on Bus: %d, Target: %d---\n"), Bus, Target);
    Buffer = AllocateInitializeIoctlDescriptor(MAX_BUFFER,
                                               &BufferLength,
                                               IOCTL_VIRTUAL_MINIPORT_CREATE_LUN);

    Buffer->RequestResponse.CreateLun.Bus = Bus;
    Buffer->RequestResponse.CreateLun.Target = Target;
    Buffer->RequestResponse.CreateLun.Size = 0xfff00000;//100*1024*1024; // Units are in Bytes
    Buffer->RequestResponse.CreateLun.ThinProvision = FALSE; // we dont support this yet
    _tprintf(TEXT("Creating LUN of size: 0x%I64x\n"), Buffer->RequestResponse.CreateLun.Size);

    if ( !DeviceIoControl(hDevice,
                          IOCTL_SCSI_MINIPORT,
                          Buffer,
                          BufferLength,
                          Buffer,
                          BufferLength,
                          &BufferLength,
                          NULL) ) {
        Status = GetLastError();
        _tprintf(TEXT("DeviceIoControlFailed, Status:0x%08x\n"), Status);
        goto Cleanup;
    }

    Status = Buffer->SrbIoControl.ReturnCode;
    if ( Status == ERROR_SUCCESS ) {
        _tprintf(TEXT("Successfully create a lun\n"));
        _tprintf(TEXT("  AdapterID: "));
        DisplayGUID(&(Buffer->RequestResponse.TargetDetails.AdapterId));
        _tprintf(TEXT("\n"));
        *LunCount = Buffer->RequestResponse.TargetDetails.LunCount;

        _tprintf(TEXT("  BusID: %d\n"), Buffer->RequestResponse.TargetDetails.Bus);
        _tprintf(TEXT("  TargetID: %d\n"), Buffer->RequestResponse.TargetDetails.Target);
        _tprintf(TEXT("  Device details:\n"));
        _tprintf(TEXT("    Size: 0x%I64x (Bytes)\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.Size);
        _tprintf(TEXT("    BlockSize: 0x%x (Bytes)\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.BlockSize);
        _tprintf(TEXT("    MaxBlocks: 0x%llx\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.MaxBlocks);
        _tprintf(TEXT("    LogicalDeviceCount: 0x%x\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.LogicalDeviceCount);
        _tprintf(TEXT("    TierCount: %d\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.TierCount);
        _tprintf(TEXT("  MaxLunCount:%d\n"), Buffer->RequestResponse.TargetDetails.MaxLunCount);
        _tprintf(TEXT("  LunCount:%d\n"), Buffer->RequestResponse.TargetDetails.LunCount);
        for ( Index = 0; Index < Buffer->RequestResponse.TargetDetails.LunCount; Index++ ) {
            _tprintf(TEXT("  LunID: %d\n"), Buffer->RequestResponse.TargetDetails.Luns [Index]);
        }
    } else {
        _tprintf(TEXT("CreateTarget returned with status: 0x%08x"), Status);
    }

    free(Buffer);

Cleanup:
    return(Status);
}

DWORD
IoctlQueryAdapterDetails(
    _In_ HANDLE hDevice,
    _Inout_ ULONG *BusCount,
    _Inout_ PVIRTUAL_MINIPORT_ADAPTER_DETAILS *AdapterDetails,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR *IoctlBuffer
    ) 
{
    ULONG Index;
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR Buffer;
    ULONG BufferLength;
    DWORD Status;

    *BusCount = 0;
    *IoctlBuffer = NULL;
    *AdapterDetails = NULL;
    Status = ERROR_SUCCESS;

    //
    // Create a bus
    //
    _tprintf(TEXT("\n\nExecuting ---Querying adapter details---\n"));
    Buffer = AllocateInitializeIoctlDescriptor(MAX_BUFFER,
                                               &BufferLength,
                                               IOCTL_VIRTUAL_MINIPORT_QUERY_ADAPTER_DETAILS);

    if ( !DeviceIoControl(hDevice,
                          IOCTL_SCSI_MINIPORT,
                          Buffer,
                          BufferLength,
                          Buffer,
                          BufferLength,
                          &BufferLength,
                          NULL) ) {
        Status = GetLastError();
        _tprintf(TEXT("DeviceIoControlFailed, Status:0x%08x\n"), Status);
        goto Cleanup;
    }

    Status = Buffer->SrbIoControl.ReturnCode;
    if ( Status == ERROR_SUCCESS ) {
        _tprintf(TEXT("Successfully queried adapter details a bus\n"));
        
        // fill up the buffer to pass it back

        *BusCount = Buffer->RequestResponse.AdapterDetails.BusCount;
        *IoctlBuffer = Buffer;
        *AdapterDetails = &(Buffer->RequestResponse.AdapterDetails);

        // now display the details

        _tprintf(TEXT("  AdapterID: "));
        DisplayGUID(&(Buffer->RequestResponse.AdapterDetails.AdapterId));
        _tprintf(TEXT("\n"));
        _tprintf(TEXT("  MaxBusCount:%d\n"), Buffer->RequestResponse.AdapterDetails.MaxBusCount);
        _tprintf(TEXT("  BusCount:%d\n"), Buffer->RequestResponse.AdapterDetails.BusCount);
        for ( Index = 0; Index < Buffer->RequestResponse.AdapterDetails.BusCount; Index++ ) {
            _tprintf(TEXT("  BusID: %d\n"), Buffer->RequestResponse.AdapterDetails.Buses [Index]);
        }


    } else {
        _tprintf(TEXT("Query adapter details returned with status: 0x%08x"), Status);
        free(Buffer);
    }

Cleanup:
    return(Status);
}

DWORD
IoctlQueryBusDetails(
    _In_ HANDLE hDevice,
    _In_ UCHAR Bus,
    _Inout_ ULONG *TargetCount,
    _Inout_ PVIRTUAL_MINIPORT_BUS_DETAILS *BusDetails,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR *IoctlBuffer
    ) 
{
    ULONG Index;
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR Buffer;
    ULONG BufferLength;
    DWORD Status;

    *TargetCount = 0;
    *IoctlBuffer = NULL;
    *BusDetails = NULL;
    Status = ERROR_SUCCESS;

    _tprintf(TEXT("\n\nExecuting ---Bus Details [%02d.00.00]---\n"), Bus);
    Buffer = AllocateInitializeIoctlDescriptor(MAX_BUFFER,
                                              &BufferLength,
                                              IOCTL_VIRTUAL_MINIPORT_QUERY_BUS_DETAILS);

    Buffer->RequestResponse.BusDetails.Bus = Bus;

    if ( !DeviceIoControl(hDevice,
                          IOCTL_SCSI_MINIPORT,
                          Buffer,
                          BufferLength,
                          Buffer,
                          BufferLength,
                          &BufferLength,
                          NULL) ) {
        _tprintf(TEXT("DeviceIoControlFailed\n"));
        goto Cleanup;
    }

    if ( Buffer->SrbIoControl.ReturnCode == ERROR_SUCCESS ) {
        _tprintf(TEXT("Successfully queried bus: BusID:%d\n"), Bus);

        // fill up the buffer to pass it back

        *TargetCount = Buffer->RequestResponse.BusDetails.TargetCount;
        *IoctlBuffer = Buffer;
        *BusDetails = &(Buffer->RequestResponse.BusDetails);

        _tprintf(TEXT("  AdapterID: "));
        DisplayGUID(&(Buffer->RequestResponse.BusDetails.AdapterId));
        _tprintf(TEXT("\n"));
        _tprintf(TEXT("  BusID: %d\n"), Buffer->RequestResponse.BusDetails.Bus);
        _tprintf(TEXT("  MaxTargetCount:%d\n"), Buffer->RequestResponse.BusDetails.MaxTargetCount);
        _tprintf(TEXT("  TargetCount:%d\n"), Buffer->RequestResponse.BusDetails.TargetCount);
        for ( Index = 0; Index < Buffer->RequestResponse.BusDetails.TargetCount; Index++ ) {
            _tprintf(TEXT("  TargetID: %d\n"), Buffer->RequestResponse.BusDetails.Targets [Index]);
        }
    }

Cleanup:
    return(Status);
}

DWORD
IoctlQueryTargetDetails(
    _In_ HANDLE hDevice,
    _In_ UCHAR Bus,
    _In_ UCHAR Target,
    _Inout_ ULONG *LunCount,
    _Inout_ PVIRTUAL_MINIPORT_TARGET_DETAILS *TargetDetails,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR *IoctlBuffer
    ) 
{
    ULONG Index;
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR Buffer;
    ULONG BufferLength;
    DWORD Status;

    *LunCount = 0;
    *IoctlBuffer = NULL;
    *TargetDetails = NULL;
    Status = ERROR_SUCCESS;

    _tprintf(TEXT("\n\nExecuting ---Target Details [%02d.%02d.00]---\n"), Bus, Target);
    Buffer = AllocateInitializeIoctlDescriptor(MAX_BUFFER,
                                               &BufferLength,
                                               IOCTL_VIRTUAL_MINIPORT_QUERY_TARGET_DETAILS);

    Buffer->RequestResponse.TargetDetails.Bus = Bus;
    Buffer->RequestResponse.TargetDetails.Target = Target;

    if ( !DeviceIoControl(hDevice,
                          IOCTL_SCSI_MINIPORT,
                          Buffer,
                          BufferLength,
                          Buffer,
                          BufferLength,
                          &BufferLength,
                          NULL) ) {
        _tprintf(TEXT("DeviceIoControlFailed\n"));
        goto Cleanup;
    }

    if ( Buffer->SrbIoControl.ReturnCode == ERROR_SUCCESS ) {
        _tprintf(TEXT("Successfully queried Target: BusID:%d, TragetID:%d\n"), Bus, Target);

        // fill up the buffer to pass it back

        *LunCount = Buffer->RequestResponse.TargetDetails.LunCount;
        *IoctlBuffer = Buffer;
        *TargetDetails = &(Buffer->RequestResponse.TargetDetails);

        _tprintf(TEXT("  AdapterID: "));
        DisplayGUID(&(Buffer->RequestResponse.TargetDetails.AdapterId));
        _tprintf(TEXT("\n"));
        _tprintf(TEXT("  BusID: %d\n"), Buffer->RequestResponse.TargetDetails.Bus);
        _tprintf(TEXT("  TargetID: %d\n"), Buffer->RequestResponse.TargetDetails.Target);
        _tprintf(TEXT("  Device details:\n"));
        _tprintf(TEXT("    Size: 0x%I64x (Bytes)\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.Size);
        _tprintf(TEXT("    BlockSize:0x%x (Bytes)\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.BlockSize);
        _tprintf(TEXT("    MaxBlocks:0x%llx\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.MaxBlocks);
        _tprintf(TEXT("    LogicalDeviceCount:0x%x\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.LogicalDeviceCount);
        _tprintf(TEXT("    TierCount: %d\n"), Buffer->RequestResponse.TargetDetails.DeviceDetails.TierCount);
        _tprintf(TEXT("  MaxLunCount:%d\n"), Buffer->RequestResponse.TargetDetails.MaxLunCount);
        _tprintf(TEXT("  LunCount:%d\n"), Buffer->RequestResponse.TargetDetails.LunCount);
        for ( Index = 0; Index < Buffer->RequestResponse.TargetDetails.LunCount; Index++ ) {
            _tprintf(TEXT("  LunID: %d\n"), Buffer->RequestResponse.TargetDetails.Luns [Index]);
        }
    }

Cleanup:
    return(Status);
}

DWORD
IoctlQueryLunDetails(
    _In_ HANDLE hDevice,
    _In_ UCHAR Bus,
    _In_ UCHAR Target,
    _In_ UCHAR Lun,
    _Inout_ PVIRTUAL_MINIPORT_LUN_DETAILS *LunDetails,
    _Inout_ PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR *IoctlBuffer
    ) 
{
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR Buffer;
    ULONG BufferLength;
    DWORD Status;

    *IoctlBuffer = NULL;
    *LunDetails = NULL;
    Status = ERROR_SUCCESS;

    _tprintf(TEXT("\n\nExecuting ---Lun Details [%02d.%02d.%02d]---\n"), Bus, Target, Lun);
    Buffer = AllocateInitializeIoctlDescriptor(MAX_BUFFER,
                                               &BufferLength,
                                               IOCTL_VIRTUAL_MINIPORT_QUERY_LUN_DETAILS);

    Buffer->RequestResponse.LunDetails.Bus = Bus;
    Buffer->RequestResponse.LunDetails.Target = Target;
    Buffer->RequestResponse.LunDetails.Lun = Lun;

    if ( !DeviceIoControl(hDevice,
                          IOCTL_SCSI_MINIPORT,
                          Buffer,
                          BufferLength,
                          Buffer,
                          BufferLength,
                          &BufferLength,
                          NULL) ) {
        _tprintf(TEXT("DeviceIoControlFailed\n"));
        goto Cleanup;
    }

    if ( Buffer->SrbIoControl.ReturnCode == ERROR_SUCCESS ) {
        _tprintf(TEXT("Successfully queried Target: BusID:%d, TragetID:%d\n"), Bus, Target);

        // fill up the buffer to pass it back

        *IoctlBuffer = Buffer;
        *LunDetails = &(Buffer->RequestResponse.LunDetails);

        _tprintf(TEXT("  AdapterID: "));
        DisplayGUID(&(Buffer->RequestResponse.LunDetails.AdapterId));
        _tprintf(TEXT("\n"));
        _tprintf(TEXT("  BusID: %d\n"), Buffer->RequestResponse.LunDetails.Bus);
        _tprintf(TEXT("  TargetID: %d\n"), Buffer->RequestResponse.LunDetails.Target);
        _tprintf(TEXT("  LunID: %d\n"), Buffer->RequestResponse.LunDetails.Lun);
        _tprintf(TEXT("  Logical Device details:\n"));
        _tprintf(TEXT("    Size: 0x%I64x (Bytes)\n"), Buffer->RequestResponse.LunDetails.DeviceDetails.Size);
        _tprintf(TEXT("    BlockSize:0x%x (Bytes)\n"), Buffer->RequestResponse.LunDetails.DeviceDetails.BlockSize);
        _tprintf(TEXT("    MaxBlocks:0x%llx\n"), Buffer->RequestResponse.LunDetails.DeviceDetails.MaxBlocks);
        _tprintf(TEXT("    ThinProvison:%s\n"), Buffer->RequestResponse.LunDetails.DeviceDetails.ThinProvison?TEXT("TRUE"):TEXT("FALSE"));
    }

Cleanup:
    return(Status);
}

DWORD
_tmain(
    int argc,
    TCHAR* argv[]
    )
{

    HANDLE hDevice = INVALID_HANDLE_VALUE;
    DWORD Status;
    UCHAR BusID, TargetID, LunID;
    ULONG BusCount, TargetCount, LunCount;
    PVIRTUAL_MINIPORT_IOCTL_DESCRIPTOR IoctlAdapterBuffer, IoctlBusBuffer, IoctlTargetBuffer, IoctlLunBuffer;
    PVIRTUAL_MINIPORT_ADAPTER_DETAILS AdapterDetails;
    PVIRTUAL_MINIPORT_BUS_DETAILS BusDetails;
    PVIRTUAL_MINIPORT_TARGET_DETAILS TargetDetails;
    PVIRTUAL_MINIPORT_LUN_DETAILS LunDetails;
    BOOLEAN TargetCreated;
    BOOLEAN LunCreated;


    hDevice = NULL;
    Status = ERROR_SUCCESS;
    TargetCreated = FALSE;
    LunCreated = FALSE;

    //Status = VMOpenControlDevice(&hDevice);
    Status = VMControlOpenHBADevice(&hDevice);
    if ( Status != ERROR_SUCCESS ) {
        _tprintf(TEXT("Failed to open VMControl device (Error: 0x%08x)\n"), Status);
        goto Cleanup;
    }

    _tprintf(TEXT("Open VMControl device successfully\n"));

    IoctlDummy(hDevice);

    IoctlCreateBus(hDevice, &BusCount);
    IoctlQueryAdapterDetails(hDevice, &BusCount, &AdapterDetails, &IoctlAdapterBuffer);

    if ( AdapterDetails != NULL && IoctlAdapterBuffer != NULL ) {
        //
        // Query Bus details
        //
        for ( BusID = 0; BusID < BusCount; BusID++ ) {
            IoctlQueryBusDetails(hDevice, AdapterDetails->Buses [BusID], &TargetCount, &BusDetails, &IoctlBusBuffer);
            
            for ( TargetID = 0; TargetID < TargetCount; TargetID++ ) {
                IoctlQueryTargetDetails(hDevice, AdapterDetails->Buses [BusID], BusDetails->Targets [TargetID], &LunCount, &TargetDetails, &IoctlTargetBuffer);

                for ( LunID = 0; LunID < LunCount; LunID++ ) {
                    IoctlQueryLunDetails(hDevice, AdapterDetails->Buses [BusID], BusDetails->Targets [TargetID], TargetDetails->Luns [LunID], &LunDetails, &IoctlLunBuffer);

                    LunDetails = NULL;
                    free(IoctlLunBuffer);
                    IoctlLunBuffer = NULL;
                }

                if ( !LunCreated ) {
                    if ( IoctlCreateLun(hDevice, AdapterDetails->Buses [BusID], BusDetails->Targets [TargetID], &LunCount) == ERROR_SUCCESS ) {
                        LunCreated = TRUE;
                    }
                }

                TargetDetails = NULL;
                free(IoctlTargetBuffer);
                IoctlTargetBuffer = NULL;
            }

            if ( !TargetCreated ) {
                if ( IoctlCreateTarget(hDevice, AdapterDetails->Buses [BusID], &TargetCount) == ERROR_SUCCESS ) {
                    TargetCreated = TRUE;
                }
            }

            BusDetails = NULL;
            free(IoctlBusBuffer);
            IoctlBusBuffer = NULL;
        }

        //
        // Free adapter detail buffer
        //
        AdapterDetails = NULL;
        free(IoctlAdapterBuffer);
        IoctlAdapterBuffer = NULL;
    }

Cleanup:

    if( hDevice != INVALID_HANDLE_VALUE ) {
        //VMCloseControlDevice(hDevice);
        VMControlCloseHBADevice(hDevice);
    }

    return(Status);
}