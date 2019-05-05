/* WPP Tracing */

#pragma once
#ifndef _TRACE_H_
#define _TRACE_H_

// required to send debug msg to kernel debugger
#define WPP_AUTOLOGGER	L"FastDisk"
#define WPP_GLOBALLOGGER

// Categorize trace statements

#define WPP_CONTROL_GUIDS                                                       \
    WPP_DEFINE_CONTROL_GUID(                                                    \
        CtlGuid,(2F555787,46DC,4078,B7E6,AE5CAF0F9FB2),                         \
        WPP_DEFINE_BIT(AIOFastDiskDebugError)         /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(AIOFastDiskDebugWarning)       /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(AIOFastDiskDebugTrace)         /* bit  2 = 0x00000004 */ \
        WPP_DEFINE_BIT(AIOFastDiskDebugInfo) )        /* bit  3 = 0x00000008 */ \


// defines to understand SRB and request sense buffer
#define WPP_LOGSRB(x) WPP_LOGPAIR((x)->Length, (x))
#define WPP_LOGSENSE(x) WPP_LOGPAIR((sizeof(SENSE_DATA)), (x))

// define to further control debug spew
#define DbgLvlErr    0x00000001
#define DbgLvlWrn    0x00000002
#define DbgLvlInfo   0x00000003
#define DbgLvlLoud   0x00000004

// DoTraceLevelMessage
// define to use levels and flag bits together
#define WPP_LEVEL_FLAGS_LOGGER(level,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_ENABLED(level,flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level & level)

#define WPP_FLAG_EXP_PRE(FLAGS, HR) {if (HR != STATUS_SUCCESS) {
#define WPP_FLAG_EXP_POST(FLAGS, HR) ;}}

#define WPP_FLAG_EXP_ENABLED(FLAGS, HR) WPP_FLAG_ENABLED(FLAGS)
#define WPP_FLAG_EXP_LOGGER(FLAGS, HR) WPP_FLAG_LOGGER(FLAGS)


// DoTraceMessage(AIOFastDiskDebugTrace, "message");
// DoLevelTraceMessage(DbgLvlInfo, AIOFastDiskDebugTrace, "Message");
// TRACE_RETURN(status)

#endif