#ifndef _RACE_CALLBACK_STORAGE_H
#define _RACE_CALLBACK_STORAGE_H

#include "CallbackState.h"

// Runtime storage owned by the in-race machine; never a file payload.
struct RaceCallbackStorage {
    CallbackStateMachine machine;  // 0x00
    CallbackStateRecord records[4]; // 0x10 on Win32; PORT: 4-player split screen (2 in the original)
};

extern RaceCallbackStorage g_raceCallbackStorage;

#endif
