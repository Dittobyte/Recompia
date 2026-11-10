// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: the multimedia timer calls (timeBeginPeriod, timeEndPeriod) live in windows.h of the compat layer, as no-ops:
// Linux timers and sleeps are not rounded to a coarse system tick.
#pragma once
#ifndef _WIN32
#include "windows.h"
#define TIMERR_NOERROR 0
#endif
