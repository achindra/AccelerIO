/*++

Module Name:

    VirtualMiniportFile.h

Date:

    20-Apr-2014

Abstract:

    Module contains the prototypes for kernel mode file I/O

--*/

#ifndef __VIRTUAL_MINIPORT_FILE_H_
#define __VIRTUAL_MINIPORT_FILE_H_

#include <wdm.h>
#include <storport.h>
#include <ntstrsafe.h>
#include <initguid.h>

#include <VirtualMiniportCommon.h>
#include <VirtualMiniportSupportRoutines.h>
#include <VirtualMiniportTrace.h>

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
    );

NTSTATUS
VMFileClose(
    _In_ HANDLE File
    );

NTSTATUS
VMFileReadWrite(
    _In_ HANDLE File,
    _Inout_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONGLONG FileOffset,
    _In_ BOOLEAN Read
    );

#endif // __VIRTUAL_MINIPORT_FILE_H_