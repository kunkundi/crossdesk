/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-13
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_COVER_VIEW_H_
#define _PRIVACY_COVER_VIEW_H_

#import <AppKit/AppKit.h>

namespace crossdesk {
// The branded cover requires the CrossDesk icon. The black cover needs no assets.
NSView* CreateMacPrivacyCoverView(NSRect frame, NSString* hint, bool allow_local_unlock = true,
                                  bool black_screen = false);
bool UpdateMacPrivacyCoverView(NSView* view, NSString* hint, bool allow_local_unlock,
                               bool black_screen = false);
}

#endif
