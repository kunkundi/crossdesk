/*
     File:       HIToolbox/Events.h

     Contains:   Event Manager Interfaces.

     Copyright:  锟� 1985-2008 by Apple Computer, Inc., all rights reserved

     Bugs?:      For bug reports, consult the following page on
                 the World Wide Web:

                     http://developer.apple.com/bugreporter/

*/
#ifndef __EVENTS__
#define __EVENTS__

#ifndef __APPLICATIONSERVICES__
#include <ApplicationServices/ApplicationServices.h>
#endif

#include <AvailabilityMacros.h>

#if PRAGMA_ONCE
#pragma once
#endif

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)

typedef UInt16 EventKind;
typedef UInt16 EventMask;
enum {
  nullEvent = 0,
  mouseDown = 1,
  mouseUp = 2,
  keyDown = 3,
  keyUp = 4,
  autoKey = 5,
  updateEvt = 6,
  diskEvt = 7, /* Not sent in Carbon. See kEventClassVolume in CarbonEvents.h*/
  activateEvt = 8,
  osEvt = 15,
  kHighLevelEvent = 23
};

enum {
  mDownMask = 1 << mouseDown,   /* mouse button pressed*/
  mUpMask = 1 << mouseUp,       /* mouse button released*/
  keyDownMask = 1 << keyDown,   /* key pressed*/
  keyUpMask = 1 << keyUp,       /* key released*/
  autoKeyMask = 1 << autoKey,   /* key repeatedly held down*/
  updateMask = 1 << updateEvt,  /* window needs updating*/
  diskMask = 1 << diskEvt,      /* disk inserted*/
  activMask = 1 << activateEvt, /* activate/deactivate window*/
  highLevelEventMask = 0x0400,  /* high-level events (includes AppleEvents)*/
  osMask = 1 << osEvt,          /* operating system events (suspend, resume)*/
  everyEvent = 0xFFFF           /* all of the above*/
};

enum {
  charCodeMask = 0x000000FF,
  keyCodeMask = 0x0000FF00,
  adbAddrMask = 0x00FF0000,
  osEvtMessageMask = (UInt32)0xFF000000
};

enum {
  /* OS event messages.  Event (sub)code is in the high byte of the message
     field.*/
  mouseMovedMessage = 0x00FA,
  suspendResumeMessage = 0x0001
};

enum {
  resumeFlag = 1 /* Bit 0 of message indicates resume vs suspend*/
};

#if CALL_NOT_IN_CARBON
/*  convertClipboardFlag is not ever set under Carbon. This is because scrap
 * conversion is  */
/*  not tied to suspend/resume events any longer. Your application should
 * instead use the   */
/*  scrap promise mechanism and fulfill scrap requests only when your promise
 * keeper proc   */
/*  is called. If you need to know if the scrap has changed, you can cache the
 * last         */
/*  ScrapRef you received and compare it with the current ScrapRef */
enum {
  convertClipboardFlag =
      2 /* Bit 1 in resume message indicates clipboard change*/
};

#endif /* CALL_NOT_IN_CARBON */

/*
    CARBON ALERT! BATTLESTATIONS!

    The EventModifiers bits defined here are also used in the newer Carbon Event
    key modifiers parameters. There are two main differences:

    1)  The Carbon key modifiers parameter is a UInt32, not a UInt16. Never try
   to extract the key modifiers parameter from a Carbon Event into an
   EventModifiers type. You will probably get your stack trashed. 2)  The Carbon
   key modifiers is just that: key modifiers. That parameter will never contain
   the button state bit.
*/
typedef UInt16 EventModifiers;
enum {
  /* modifiers */
  activeFlagBit = 0,      /* activate? (activateEvt and mouseDown)*/
  btnStateBit = 7,        /* state of button?*/
  cmdKeyBit = 8,          /* command key down?*/
  shiftKeyBit = 9,        /* shift key down?*/
  alphaLockBit = 10,      /* alpha lock down?*/
  optionKeyBit = 11,      /* option key down?*/
  controlKeyBit = 12,     /* control key down?*/
  rightShiftKeyBit = 13,  /* right shift key down? Not supported on Mac OS X.*/
  rightOptionKeyBit = 14, /* right Option key down? Not supported on Mac OS X.*/
  rightControlKeyBit =
      15 /* right Control key down? Not supported on Mac OS X.*/
};

enum {
  activeFlag = 1 << activeFlagBit,
  btnState = 1 << btnStateBit,
  cmdKey = 1 << cmdKeyBit,
  shiftKey = 1 << shiftKeyBit,
  alphaLock = 1 << alphaLockBit,
  optionKey = 1 << optionKeyBit,
  controlKey = 1 << controlKeyBit,
  rightShiftKey = 1 << rightShiftKeyBit,    /* Not supported on Mac OS X.*/
  rightOptionKey = 1 << rightOptionKeyBit,  /* Not supported on Mac OS X.*/
  rightControlKey = 1 << rightControlKeyBit /* Not supported on Mac OS X.*/
};

/* MacRoman character codes*/
enum {
  kNullCharCode = 0,
  kHomeCharCode = 1,
  kEnterCharCode = 3,
  kEndCharCode = 4,
  kHelpCharCode = 5,
  kBellCharCode = 7,
  kBackspaceCharCode = 8,
  kTabCharCode = 9,
  kLineFeedCharCode = 10,
  kVerticalTabCharCode = 11,
  kPageUpCharCode = 11,
  kFormFeedCharCode = 12,
  kPageDownCharCode = 12,
  kReturnCharCode = 13,
  kFunctionKeyCharCode = 16,
  kCommandCharCode = 17,   /* glyph available only in system fonts*/
  kCheckCharCode = 18,     /* glyph available only in system fonts*/
  kDiamondCharCode = 19,   /* glyph available only in system fonts*/
  kAppleLogoCharCode = 20, /* glyph available only in system fonts*/
  kEscapeCharCode = 27,
  kClearCharCode = 27,
  kLeftArrowCharCode = 28,
  kRightArrowCharCode = 29,
  kUpArrowCharCode = 30,
  kDownArrowCharCode = 31,
  kSpaceCharCode = 32,
  kDeleteCharCode = 127,
  kBulletCharCode = 165,
  kNonBreakingSpaceCharCode = 202
};

/* useful Unicode code points*/
enum {
  kShiftUnicode = 0x21E7,   /* Unicode UPWARDS WHITE ARROW*/
  kControlUnicode = 0x2303, /* Unicode UP ARROWHEAD*/
  kOptionUnicode = 0x2325,  /* Unicode OPTION KEY*/
  kCommandUnicode = 0x2318, /* Unicode PLACE OF INTEREST SIGN*/
  kPencilUnicode = 0x270E,  /* Unicode LOWER RIGHT PENCIL; actually pointed left
                               until Mac OS X 10.3*/
  kPencilLeftUnicode = 0xF802, /* Unicode LOWER LEFT PENCIL; available in Mac OS
                                  X 10.3 and later*/
  kCheckUnicode = 0x2713,      /* Unicode CHECK MARK*/
  kDiamondUnicode = 0x25C6,    /* Unicode BLACK DIAMOND*/
  kBulletUnicode = 0x2022,     /* Unicode BULLET*/
  kAppleLogoUnicode = 0xF8FF   /* Unicode APPLE LOGO*/
};

/*
 *  Summary:
 *    Virtual keycodes
 *
 *  Discussion:
 *    These constants are the virtual keycodes defined originally in
 *    Inside Mac Volume V, pg. V-191. They identify physical keys on a
 *    keyboard. Those constants with "ANSI" in the name are labeled
 *    according to the key position on an ANSI-standard US keyboard.
 *    For example, kVK_ANSI_A indicates the virtual keycode for the key
 *    with the letter 'A' in the US keyboard layout. Other keyboard
 *    layouts may have the 'A' key label on a different physical key;
 *    in this case, pressing 'A' will generate a different virtual
 *    keycode.
 */
enum {
  kVK_ANSI_A = 0x00,
  kVK_ANSI_S = 0x01,
  kVK_ANSI_D = 0x02,
  kVK_ANSI_F = 0x03,
  kVK_ANSI_H = 0x04,
  kVK_ANSI_G = 0x05,
  kVK_ANSI_Z = 0x06,
  kVK_ANSI_X = 0x07,
  kVK_ANSI_C = 0x08,
  kVK_ANSI_V = 0x09,
  kVK_ANSI_B = 0x0B,
  kVK_ANSI_Q = 0x0C,
  kVK_ANSI_W = 0x0D,
  kVK_ANSI_E = 0x0E,
  kVK_ANSI_R = 0x0F,
  kVK_ANSI_Y = 0x10,
  kVK_ANSI_T = 0x11,
  kVK_ANSI_1 = 0x12,
  kVK_ANSI_2 = 0x13,
  kVK_ANSI_3 = 0x14,
  kVK_ANSI_4 = 0x15,
  kVK_ANSI_6 = 0x16,
  kVK_ANSI_5 = 0x17,
  kVK_ANSI_Equal = 0x18,
  kVK_ANSI_9 = 0x19,
  kVK_ANSI_7 = 0x1A,
  kVK_ANSI_Minus = 0x1B,
  kVK_ANSI_8 = 0x1C,
  kVK_ANSI_0 = 0x1D,
  kVK_ANSI_RightBracket = 0x1E,
  kVK_ANSI_O = 0x1F,
  kVK_ANSI_U = 0x20,
  kVK_ANSI_LeftBracket = 0x21,
  kVK_ANSI_I = 0x22,
  kVK_ANSI_P = 0x23,
  kVK_ANSI_L = 0x25,
  kVK_ANSI_J = 0x26,
  kVK_ANSI_Quote = 0x27,
  kVK_ANSI_K = 0x28,
  kVK_ANSI_Semicolon = 0x29,
  kVK_ANSI_Backslash = 0x2A,
  kVK_ANSI_Comma = 0x2B,
  kVK_ANSI_Slash = 0x2C,
  kVK_ANSI_N = 0x2D,
  kVK_ANSI_M = 0x2E,
  kVK_ANSI_Period = 0x2F,
  kVK_ANSI_Grave = 0x32,
  kVK_ANSI_KeypadDecimal = 0x41,
  kVK_ANSI_KeypadMultiply = 0x43,
  kVK_ANSI_KeypadPlus = 0x45,
  kVK_ANSI_KeypadClear = 0x47,
  kVK_ANSI_KeypadDivide = 0x4B,
  kVK_ANSI_KeypadEnter = 0x4C,
  kVK_ANSI_KeypadMinus = 0x4E,
  kVK_ANSI_KeypadEquals = 0x51,
  kVK_ANSI_Keypad0 = 0x52,
  kVK_ANSI_Keypad1 = 0x53,
  kVK_ANSI_Keypad2 = 0x54,
  kVK_ANSI_Keypad3 = 0x55,
  kVK_ANSI_Keypad4 = 0x56,
  kVK_ANSI_Keypad5 = 0x57,
  kVK_ANSI_Keypad6 = 0x58,
  kVK_ANSI_Keypad7 = 0x59,
  kVK_ANSI_Keypad8 = 0x5B,
  kVK_ANSI_Keypad9 = 0x5C
};

/* keycodes for keys that are independent of keyboard layout*/
enum {
  kVK_Return = 0x24,
  kVK_Tab = 0x30,
  kVK_Space = 0x31,
  kVK_Delete = 0x33,
  kVK_Escape = 0x35,
  kVK_Command = 0x37,
  kVK_Shift = 0x38,
  kVK_CapsLock = 0x39,
  kVK_Option = 0x3A,
  kVK_Control = 0x3B,
  kVK_RightCommand = 0x36,
  kVK_RightShift = 0x3C,
  kVK_RightOption = 0x3D,
  kVK_RightControl = 0x3E,
  kVK_Function = 0x3F,
  kVK_F17 = 0x40,
  kVK_VolumeUp = 0x48,
  kVK_VolumeDown = 0x49,
  kVK_Mute = 0x4A,
  kVK_F18 = 0x4F,
  kVK_F19 = 0x50,
  kVK_F20 = 0x5A,
  kVK_F5 = 0x60,
  kVK_F6 = 0x61,
  kVK_F7 = 0x62,
  kVK_F3 = 0x63,
  kVK_F8 = 0x64,
  kVK_F9 = 0x65,
  kVK_F11 = 0x67,
  kVK_F13 = 0x69,
  kVK_F16 = 0x6A,
  kVK_F14 = 0x6B,
  kVK_F10 = 0x6D,
  kVK_ContextualMenu = 0x6E,
  kVK_F12 = 0x6F,
  kVK_F15 = 0x71,
  kVK_Help = 0x72,
  kVK_Home = 0x73,
  kVK_PageUp = 0x74,
  kVK_ForwardDelete = 0x75,
  kVK_F4 = 0x76,
  kVK_End = 0x77,
  kVK_F2 = 0x78,
  kVK_PageDown = 0x79,
  kVK_F1 = 0x7A,
  kVK_LeftArrow = 0x7B,
  kVK_RightArrow = 0x7C,
  kVK_DownArrow = 0x7D,
  kVK_UpArrow = 0x7E
};

/* ISO keyboards only*/
enum { kVK_ISO_Section = 0x0A };

/* JIS keyboards only*/
enum {
  kVK_JIS_Yen = 0x5D,
  kVK_JIS_Underscore = 0x5E,
  kVK_JIS_KeypadComma = 0x5F,
  kVK_JIS_Eisu = 0x66,
  kVK_JIS_Kana = 0x68
};

struct EventRecord {
  EventKind what;
  unsigned long message;
  UInt32 when;
  Point where;
  EventModifiers modifiers;
};
typedef struct EventRecord EventRecord;
typedef CALLBACK_API(void, FKEYProcPtr)(void);
typedef STACK_UPP_TYPE(FKEYProcPtr) FKEYUPP;
/*
 *  NewFKEYUPP()
 *
 *  Availability:
 *    Mac OS X:         not available
 *    CarbonLib:        not available
 *    Non-Carbon CFM:   available as macro/inline
 */

/*
 *  DisposeFKEYUPP()
 *
 *  Availability:
 *    Mac OS X:         not available
 *    CarbonLib:        not available
 *    Non-Carbon CFM:   available as macro/inline
 */

/*
 *  InvokeFKEYUPP()
 *
 *  Availability:
 *    Mac OS X:         not available
 *    CarbonLib:        not available
 *    Non-Carbon CFM:   available as macro/inline
 */

#if !__LP64__
/*
 *  GetMouse()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use HIGetMousePosition instead.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] but deprecated in 10.5 CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern void GetMouse(Point *mouseLoc)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_5;

#endif /* !__LP64__ */

/*
 *  Button()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use GetCurrentButtonState or GetCurrentEventButtonState instead.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework but
 * deprecated in 10.6 CarbonLib:        in CarbonLib 1.0 and later Non-Carbon
 * CFM:   in InterfaceLib 7.1 and later
 */
extern Boolean Button(void)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_6;

#if !__LP64__
/*
 *  StillDown()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] CarbonLib:        in CarbonLib 1.0 and later Non-Carbon CFM:   in
 * InterfaceLib 7.1 and later
 */
extern Boolean StillDown(void) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

/*
 *  WaitMouseUp()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] CarbonLib:        in CarbonLib 1.0 and later Non-Carbon CFM:   in
 * InterfaceLib 7.1 and later
 */
extern Boolean WaitMouseUp(void) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

/*
 *  KeyTranslate()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use UCKeyTranslate instead.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] but deprecated in 10.6 CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern UInt32 KeyTranslate(const void *transData, UInt16 keycode, UInt32 *state)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_6;

/*
 *  GetCaretTime()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] CarbonLib:        in CarbonLib 1.0 and later Non-Carbon CFM:   in
 * InterfaceLib 7.1 and later
 */
extern UInt32 GetCaretTime(void) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

#endif /* !__LP64__ */

/*
    QuickTime 3.0 supports GetKeys() on unix and win32
    But, on little endian machines you will have to be
    careful about bit numberings and/or use a KeyMapByteArray
    instead.
*/
#if TARGET_API_MAC_OS8

typedef UInt32 KeyMap[4];
#else
typedef BigEndianUInt32 KeyMap[4];
#endif /* TARGET_API_MAC_OS8 */

typedef UInt8 KeyMapByteArray[16];
/*
 *  GetKeys()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework
 *    CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern void GetKeys(KeyMap theKeys) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

/* Obsolete event types & masks */
enum {
  networkEvt = 10,
  driverEvt = 11,
  app1Evt = 12,
  app2Evt = 13,
  app3Evt = 14,
  app4Evt = 15,
  networkMask = 0x0400,
  driverMask = 0x0800,
  app1Mask = 0x1000,
  app2Mask = 0x2000,
  app3Mask = 0x4000,
  app4Mask = 0x8000
};

struct EvQEl {
  QElemPtr qLink;
  SInt16 qType;
  EventKind
      evtQWhat; /* this part is identical to the EventRecord as defined above */
  unsigned long evtQMessage;
  UInt32 evtQWhen;
  Point evtQWhere;
  EventModifiers evtQModifiers;
};
typedef struct EvQEl EvQEl;
typedef EvQEl *EvQElPtr;
typedef CALLBACK_API(void, GetNextEventFilterProcPtr)(EventRecord *theEvent,
                                                      Boolean *result);
typedef STACK_UPP_TYPE(GetNextEventFilterProcPtr) GetNextEventFilterUPP;
/*
 *  NewGetNextEventFilterUPP()
 *
 *  Availability:
 *    Mac OS X:         not available
 *    CarbonLib:        not available
 *    Non-Carbon CFM:   available as macro/inline
 */

/*
 *  DisposeGetNextEventFilterUPP()
 *
 *  Availability:
 *    Mac OS X:         not available
 *    CarbonLib:        not available
 *    Non-Carbon CFM:   available as macro/inline
 */

/*
 *  InvokeGetNextEventFilterUPP()
 *
 *  Availability:
 *    Mac OS X:         not available
 *    CarbonLib:        not available
 *    Non-Carbon CFM:   available as macro/inline
 */

typedef GetNextEventFilterUPP GNEFilterUPP;
#if !__LP64__
/*
 *  GetDblTime()
 *
 *  Summary:
 *    Returns the maximum time (in units of 1/60th of a second) allowed
 *    between two consecutive mouse-down events in order for the second
 *    click to be considered a double-click.
 *
 *  Discussion:
 *    In 64-bit applications, you may replace calls to this API with
 *    calls to NXClickTime (declared in
 *    <IOKit/hidsystem/event_status_driver.h>) or with +[NSEvent
 *    doubleClickInterval] (available in Mac OS X 10.6 and later).
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Result:
 *    The maximum time between mouse-downs allowed for a double-click.
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] CarbonLib:        in CarbonLib 1.0 and later Non-Carbon CFM:   in
 * InterfaceLib 7.1 and later
 */
extern UInt32 GetDblTime(void) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

/*
 *  SetEventMask()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] CarbonLib:        in CarbonLib 1.0 and later Non-Carbon CFM:   in
 * InterfaceLib 7.1 and later
 */
extern void SetEventMask(EventMask value)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

/*
 *  GetNextEvent()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use ReceiveNextEvent instead.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] but deprecated in 10.6 CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern Boolean GetNextEvent(EventMask eventMask, EventRecord *theEvent)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_6;

/*
 *  WaitNextEvent()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use ReceiveNextEvent instead.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] but deprecated in 10.6 CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern Boolean WaitNextEvent(EventMask eventMask, EventRecord *theEvent,
                             UInt32 sleep, RgnHandle mouseRgn) /* can be NULL */
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_6;

/*
 *  EventAvail()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use FindSpecificEventInQueue instead.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] but deprecated in 10.6 CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern Boolean EventAvail(EventMask eventMask, EventRecord *theEvent)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_6;

/*
 *  PostEvent()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use PostEventToQueue or CGEventPost instead.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] but deprecated in 10.6 CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern OSErr PostEvent(EventKind eventNum, UInt32 eventMsg)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_6;

#endif /* !__LP64__ */

/*
 *  FlushEvents()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use FlushEventsMatchingListFromQueue,
 *    FlushSpecificEventsFromQueue, or FlushEventQueue instead.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework but
 * deprecated in 10.6 CarbonLib:        in CarbonLib 1.0 and later Non-Carbon
 * CFM:   in InterfaceLib 7.1 and later
 */
extern void FlushEvents(EventMask whichMask, EventMask stopMask)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_6;

#if OLDROUTINENAMES
#define KeyTrans(transData, keycode, state) \
  KeyTranslate(transData, keycode, state)
#endif /* OLDROUTINENAMES */

#if !__LP64__
/*
 *  KeyScript()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use TISSelectInputSource API for positive verbs (ScriptCode).
 *     Use TSMDocument properties to restrict input sources:
 *     kTSMDocumentEnabledInputSourcesPropertyTag
 *     kTSMDocumentInputSourceOverridePropertyTag
 *
 *  Summary:
 *    Switch to the specified script's default (last used) input source.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] but deprecated in 10.5 CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern void KeyScript(short code)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_5;

#endif /* !__LP64__ */

/*
 *  IsCmdChar()   *** DEPRECATED ***
 *
 *  Deprecated:
 *    Use IsUserCancelEventRef or CheckEventQueueForUserCancel instead.
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework but
 * deprecated in 10.6 CarbonLib:        in CarbonLib 1.0 and later Non-Carbon
 * CFM:   in InterfaceLib 7.1 and later
 */
extern Boolean IsCmdChar(const EventRecord *event, short test)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_6;

/*
    LowMem accessor functions previously in LowMem.h
*/
/*
 *  LMGetKeyThresh()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework
 *    CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern SInt16 LMGetKeyThresh(void) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

#if !__LP64__
/*
 *  LMSetKeyThresh()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] CarbonLib:        in CarbonLib 1.0 and later Non-Carbon CFM:   in
 * InterfaceLib 7.1 and later
 */
extern void LMSetKeyThresh(SInt16 value)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

#endif /* !__LP64__ */

/*
 *  LMGetKeyRepThresh()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework
 *    CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern SInt16 LMGetKeyRepThresh(void) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

#if !__LP64__
/*
 *  LMSetKeyRepThresh()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] CarbonLib:        in CarbonLib 1.0 and later Non-Carbon CFM:   in
 * InterfaceLib 7.1 and later
 */
extern void LMSetKeyRepThresh(SInt16 value)
    AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

#endif /* !__LP64__ */

/*
 *  LMGetKbdLast()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework
 *    CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern UInt8 LMGetKbdLast(void) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

#if !__LP64__
/*
 *  LMSetKbdLast()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] CarbonLib:        in CarbonLib 1.0 and later Non-Carbon CFM:   in
 * InterfaceLib 7.1 and later
 */
extern void LMSetKbdLast(UInt8 value) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

#endif /* !__LP64__ */

/*
 *  LMGetKbdType()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework
 *    CarbonLib:        in CarbonLib 1.0 and later
 *    Non-Carbon CFM:   in InterfaceLib 7.1 and later
 */
extern UInt8 LMGetKbdType(void) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

#if !__LP64__
/*
 *  LMSetKbdType()
 *
 *  Mac OS X threading:
 *    Not thread safe
 *
 *  Availability:
 *    Mac OS X:         in version 10.0 and later in Carbon.framework [32-bit
 * only] CarbonLib:        in CarbonLib 1.0 and later Non-Carbon CFM:   in
 * InterfaceLib 7.1 and later
 */
extern void LMSetKbdType(UInt8 value) AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER;

#endif /* !__LP64__ */

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* __EVENTS__ */
