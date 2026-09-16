#pragma once

typedef enum {
    DesktopMainEventLock,
    DesktopMainEventOpenLockMenu,
    DesktopMainEventOpenArchive,
    DesktopMainEventOpenFavoriteLeftShort,
    DesktopMainEventOpenFavoriteLeftLong,
    DesktopMainEventOpenFavoriteRightShort,
    DesktopMainEventOpenFavoriteRightLong,
    DesktopMainEventOpenFavoriteOkLong,
    DesktopMainEventOpenMenu,
    DesktopMainEventOpenDebug,
    DesktopMainEventOpenPowerOff,

    DesktopLockedEventUnlocked,
    DesktopLockedEventUpdate,
    DesktopLockedEventShowPinInput,
    DesktopLockedEventDoorsClosed,

    DesktopPinInputEventResetWrongPinLabel,
    DesktopPinInputEventUnlocked,
    DesktopPinInputEventUnlockFailed,
    DesktopPinInputEventBack,

    DesktopPinTimeoutExit,

    DesktopDebugEventToggleDebugMode,
    DesktopDebugEventExit,

    DesktopLockMenuEventLock,
    DesktopLockMenuEventBt,
    DesktopLockMenuEventStealthModeOn,
    DesktopLockMenuEventStealthModeOff,
    DesktopLockMenuEventSettings,
    DesktopLockMenuEventSubGhz,
    DesktopLockMenuEventProtoPirate,

    DesktopAnimationEventCheckAnimation,
    DesktopAnimationEventNewIdleAnimation,
    DesktopAnimationEventInteractAnimation,

    DesktopSlideshowCompleted,
    DesktopSlideshowPoweroff,

    DesktopHwMismatchExit,

    DesktopEnclaveExit,

    // Global events
    DesktopGlobalBeforeAppStarted,
    DesktopGlobalAfterAppFinished,
    DesktopGlobalAutoLock,
    DesktopGlobalApiUnlock,
    DesktopGlobalSaveSettings,
    DesktopGlobalReloadSettings,
} DesktopEvent;
