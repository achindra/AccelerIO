/*++

Module Name:

    VirtualMiniportWPPConfiguration.h

Date:

    24-Feb-2014

Abstract:

    Header contains the WPP configuration blocks for tracing

--*/

#ifndef __VIRTUAL_MINIPORT_WPP_CONFIGURATION_H_
#define __VIRTUAL_MINIPORT_WPP_CONFIGURATION_H_

//
// Defines for making this logger an auto logger and global logger. When this is enabled
// logging goes directly to the debugger. Reference: Storage-Tracing-For-Miniports.doc
// We will enable it if needed. 
// NOTE: For now I have configured auto logger on the test system directly.
//

//#define WPP_AUTOLOGGER L"VirtualMiniportLogger"
//#define WPP_GLOBALLOGGER


//
// Configuration blocks are scanned by the build as we have provided this header file
// to the build with -scan:<filename> option. All the configuration blocks are scanned
// by wpp pre-processor to generate the required types

//
// Build settings are done to generate VMTrace. It should be equivalent to
// below configuration block. For some reason we are not able to get WPP
// pre-processor to scan it. Going ahead including this trace function in
// -func WPP preprocessor option
//
// begin_wpp config
// FUNC VMTRACE (LEVEL, FLAGS, MSG, ...);
// end_wpp
//

//
// Configuration block for SCSI_ADAPTER_CONTROL_TYPE
//
// begin_wpp config
// CUSTOM_TYPE (ADPATERCONTROL, ItemEnum(_SCSI_ADAPTER_CONTROL_TYPE) );
// end_wpp
//

//
// Configuration block for Storport SCSI_ADAPTER_CONTROL_STATUS
//
// begin_wpp config
// CUSTOM_TYPE (ADAPTERCONTROLSTATUS, ItemEnum(_SCSI_ADAPTER_CONTROL_STATUS) );
// end_wpp
//

//
// Configuration block for Storport STOR_PNP_ACTION
//
// begin_wpp config
// CUSTOM_TYPE (STORPNPACTION, ItemEnum(STOR_PNP_ACTION) );
// end_wpp
//

//
// Configuration block for our IOCTLs IOCTL_VIRTUAL_MINIPORT
//
// begin_wpp config
// CUSTOM_TYPE (STORIOCTL, ItemEnum(_IOCTL_VIRTUAL_MINIPORT) );
// end_wpp
//

//
// Configuration blocks for driver specific enums
//
// begin_wpp config
// CUSTOM_TYPE (VMTYPE, ItemEnum(_VM_TYPE) );
// CUSTOM_TYPE (VMDEVICESTATE, ItemEnum(_VM_DEVICE_STATE) );
// end_wpp
//

//
// Configuration block for scheduler specific types
//
// begin_wpp config
// CUSTOM_TYPE (VMSCHEDULERSTATE, ItemEnum(_VM_SCHEDULER_STATE) );
// CUSTOM_TYPE (VMSCHEDULERHINT, ItemEnum(_VM_SCHEDULER_HINT) );
// end_wpp
//

#endif //__VIRTUAL_MINIPORT_WPP_CONFIGURATION_H_