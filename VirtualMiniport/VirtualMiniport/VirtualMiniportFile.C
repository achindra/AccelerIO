/*++

Module Name:

    VirtualMiniportFile.C

Date:

    20-Apr-2014

Abstract:

    Module implements the basic file I/O operations in kernel-mode.
    This module does not have any dependency on any other module.
    Caller is responsible for any synchronization requirements.

--*/

//#include <ntddk.h>
#include <ntifs.h>
#include <VirtualMiniportFile.h>

//
// WPP based event trace
//

#include <VirtualMiniportFile.tmh>

//
// Forward declarations of private functions
//

//
// Define the attributes of functions; declarations are in module
// specific header
//

#pragma alloc_text(PAGED, VMFileCreate)
#pragma alloc_text(PAGED, VMFileClose)
#pragma alloc_text(PAGED, VMFileReadWrite)

//
// Device routines
//

//
// File management routines
//

NTSTATUS
VMFileCreate(
    _In_ PUNICODE_STRING FileName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ BOOLEAN PreAllocate,
    _Inout_ HANDLE *File
    )

/*++

Routine Description:

    Creates or Opens the file

Arguments:

    FileName - Unicode pointer to file name

    DesiredAccess - Access need of the open file (GENERIC_READ, GENERIC_WRITE etc.)

    FileAttributes - Same as user mode CreateFile file attributes

    ShareAccess - Shared mode of open

    CreateDisposition - Create disposition

    CreateOptions - Create options

    AllocationSize - pointer to allocations size (for pre-allocation or on-demand allocation)

    PreAllocate - pre-allocate the file size

    File - Pointer to handle that receives the newly created file handle

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;
    IO_STATUS_BLOCK Iosb;
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_END_OF_FILE_INFORMATION EndOfFilePointer;

    Status = STATUS_UNSUCCESSFUL;

    if ( FileName == NULL || File == NULL || AllocationSize == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    *File = NULL;
    EndOfFilePointer.EndOfFile = *AllocationSize;

    RtlZeroMemory(&Iosb, sizeof(Iosb));
    InitializeObjectAttributes(&ObjectAttributes,
                               FileName,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateFile(File, 
                          DesiredAccess,
                          &ObjectAttributes,
                          &Iosb,
                          AllocationSize,
                          FileAttributes,
                          ShareAccess,
                          CreateDisposition,
                          CreateOptions,
                          NULL,
                          0);

    if ( NT_SUCCESS(Status) ) {
    
        if ( PreAllocate == TRUE ) {
            Status = ZwSetInformationFile(*File,
                                          &Iosb,
                                          &EndOfFilePointer,
                                          sizeof(FILE_END_OF_FILE_INFORMATION),
                                          FileEndOfFileInformation);
        }
    }

Cleanup:

    if ( !NT_SUCCESS(Status) && File != NULL && *File != NULL ) {
        ZwClose(*File);
        *File = NULL;
    }

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_TIER_FILE,
            "[%s]:FileName:%wZ, FileHandle:%p, Status:%!STATUS!",
            __FUNCTION__,
            FileName,
            File?*File:NULL,
            Status);

    return(Status);
}

NTSTATUS
VMFileClose(
    _In_ HANDLE File
    )

/*++

Routine Description:

    Closes the file handle

Arguments:

    File - Handle to be closed

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

    Status = ZwClose(File);

    VMTrace(TRACE_LEVEL_INFORMATION,
            VM_TRACE_TIER_FILE,
            "[%s]:File handle:%p, Status:%!STATUS!",
            __FUNCTION__,
            File,
            Status);

    return(Status);
}

NTSTATUS
VMFileReadWrite(
    _In_ HANDLE File,
    _Inout_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONGLONG FileOffset,
    _In_ BOOLEAN Read
    )

/*++

Routine Description:

    Reads from or Writes to the file

Arguments:

    File - Handle of the file

    Buffer - Data buffer 

    BufferLength - Length of data

    FileOffset - File pointer

    Read - Indicates the operations to be read or write

Environment:

    IRQL - PASSIVE_LEVEL

Return Value:

    STATUS_SUCCESS
    STATUS_UNSUCCESSFUL
    NTSTATUS

--*/

{
    NTSTATUS Status;
    LARGE_INTEGER ByteOffset;
    IO_STATUS_BLOCK Iosb;
    HANDLE Event;

    Status = STATUS_UNSUCCESSFUL;
    ByteOffset.QuadPart = 0;
    RtlZeroMemory(&Iosb, sizeof(Iosb));
    Event = NULL;

    if ( File == NULL || Buffer == NULL || BufferLength == 0 ) {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    Status = ZwCreateEvent(&Event,
                           EVENT_ALL_ACCESS,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if ( !NT_SUCCESS(Status) ) {
        goto Cleanup;
    }

    ByteOffset.QuadPart = FileOffset;
    if ( Read == TRUE ) {
    
        Status = ZwReadFile(File, Event, NULL, NULL, &Iosb, Buffer, BufferLength, &ByteOffset, NULL);
    } else {

        Status = ZwWriteFile(File, Event, NULL, NULL, &Iosb, Buffer, BufferLength, &ByteOffset, NULL);
    }

    if ( Status == STATUS_PENDING ) {
        Status = ZwWaitForSingleObject(Event, FALSE, NULL);
    }

    if ( Status == STATUS_SUCCESS ) {
        if ( Iosb.Information != BufferLength ) {
            Status = STATUS_UNSUCCESSFUL;
        }
    }

Cleanup:
    if ( Event != NULL ) {
        ZwClose(Event);
    }
    VMTrace(TRACE_LEVEL_VERBOSE,
            VM_TRACE_TIER_FILE,
            "[%s]:[%s], FileHandle:%p, Buffer:%p, BufferLength:0x%08x, Offset:0x%I64x, Status:%!STATUS!, Iosb:[%I64x, %!STATUS!]",
            __FUNCTION__,
            Read ? "READ" : "WRITE",
            File,
            Buffer,
            BufferLength,
            ByteOffset.QuadPart,
            Status,
            Iosb.Information,
            Iosb.Status);
    return(Status);
}