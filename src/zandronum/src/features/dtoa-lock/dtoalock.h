// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// [MGOOOOOO] gdtoa's two locks (ACQUIRE_DTOA_LOCK and FREE_DTOA_LOCK in gdtoaimp.h): 0 guards its block freelist, 1 its powers of five.
extern "C" void fua_dtoa_lock(int n);
extern "C" void fua_dtoa_unlock(int n);
