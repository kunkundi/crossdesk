/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-05
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _TRANSFER_NOTIFICATION_STATE_H_
#define _TRANSFER_NOTIFICATION_STATE_H_

namespace crossdesk::transfer_notification {

struct Presentation {
  bool dismissed = false;
  bool result_seen = false;

  bool ShowNotification(bool browser_visible, bool result_presented) {
    // Hiding an active transfer is temporary. A result already presented in
    // the browser must not pop up again when that window closes.
    result_seen = result_seen || result_presented;
    return !dismissed && !result_seen && !browser_visible;
  }
};

}  // namespace crossdesk::transfer_notification

#endif
