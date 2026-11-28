// SPDX-License-Identifier: GPL-3.0-or-later
package org.recompia.pkp

import android.os.Build
import android.view.Display

/** The active display must accept the HDR10 signal used by the game. */
internal fun hdrDisplayCapabilities(display: Display?): Display.HdrCapabilities? {
    val hdr = display?.hdrCapabilities ?: return null
    @Suppress("DEPRECATION")
    val types = if (Build.VERSION.SDK_INT >= 34) display.mode.supportedHdrTypes else hdr.supportedHdrTypes
    return hdr.takeIf {
        types.any { type ->
            type == Display.HdrCapabilities.HDR_TYPE_HDR10 || type == Display.HdrCapabilities.HDR_TYPE_HDR10_PLUS
        }
    }
}

internal fun reportedHdrPeak(hdr: Display.HdrCapabilities): Int? =
    hdr.desiredMaxLuminance.takeIf { it in 200f..10000f }?.toInt()
