/*++

Module Name:

    VirtualMiniportTrace.h

Date:

    20-Feb-2014

Abstract:

    Header contains the macro definitions for WPP tracing

--*/

#ifndef __VIRTUAL_MINIPORT_TRACE_H_
#define __VIRTUAL_MINIPORT_TRACE_H_

#include<evntrace.h>

//
// Reference WPP_Intro.docx. We have to define WPP_LEVEL_FLAGS_LOGGER
// and WPP_LEVEL_FLAGS_ENABLED in terms of default macros WPP_LEVEL_ENABLED
// and WPP_LEVEL_LOGGER. This is needed if we are defining out custom
// tracing function
//

#define WPP_LEVEL_FLAGS_LOGGER(level,flags) \
    WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(level, flags) \
    (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_## flags).Level >= level)


//
// Only 32 components are allowed
//

//
// {DD04F0EA-A764-401F-8E4E-402D5011B760}
//

#define WPP_CONTROL_GUIDS\
    WPP_DEFINE_CONTROL_GUID(VMTraceGUID, (DD04F0EA, A764, 401F, 8E4E, 402D5011B760), \
    WPP_DEFINE_BIT(VM_TRACE_DRIVER)     \
    WPP_DEFINE_BIT(VM_TRACE_CONFIG)     \
    WPP_DEFINE_BIT(VM_TRACE_DEVICE)     \
    WPP_DEFINE_BIT(VM_TRACE_ADAPTER)    \
    WPP_DEFINE_BIT(VM_TRACE_BUS)        \
    WPP_DEFINE_BIT(VM_TRACE_TARGET)     \
    WPP_DEFINE_BIT(VM_TRACE_LUN)        \
    WPP_DEFINE_BIT(VM_TRACE_SCSI)       \
    WPP_DEFINE_BIT(VM_TRACE_IOCTL)      \
    WPP_DEFINE_BIT(VM_TRACE_PNP)        \
    WPP_DEFINE_BIT(VM_TRACE_SCHEDULER)  \
    WPP_DEFINE_BIT(VM_TRACE_TIER_MEMORY) \
    WPP_DEFINE_BIT(VM_TRACE_TIER_FILE) \
    )

//
// The trace formatting engine understands how to format a
// SCSI_REQUEST_BLOCK and a REQUEST SENSE buffer. To use these
// custom formats, however, you must include the following two
// #defines. Custom type for SRB and SENSEDATA are defined for
// WPP in the ini file.
//

#define WPP_LOGSRB(x) WPP_LOGPAIR((x)->Length, (x))
#define WPP_LOGSENSE(x) WPP_LOGPAIR((sizeof(SENSE_DATA)), (x))

#endif //__VIRTUAL_MINIPORT_TRACE_H_