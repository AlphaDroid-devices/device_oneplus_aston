/*
 * SPDX-FileCopyrightText: 2026 AlphaDroid
 * SPDX-License-Identifier: Apache-2.0
 */
package org.lineageos.device.settings.display;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import androidx.preference.PreferenceManager;

import org.lineageos.device.settings.Constants;
import org.lineageos.device.settings.utils.FileUtils;

/**
 * Stock-style AOD brightness: the panel only supports two fixed AOD levels via
 * {@code /sys/kernel/oplus_display/aod_light_mode_set}:
 * <ul>
 *   <li>{@code 0} = high (~50 nits)</li>
 *   <li>{@code 1} = low (~10 nits)</li>
 * </ul>
 * Kernel OFP applies the value on doze entry (and live if already in AOD).
 * Default matches stock: low / 10 nits (preference off → node {@code 1}).
 */
public class AodBrightnessController {
    private static final String TAG = "AodBrightnessController";
    private static AodBrightnessController sInstance;

    private final SharedPreferences mSharedPrefs;

    private AodBrightnessController(Context context) {
        mSharedPrefs = PreferenceManager.getDefaultSharedPreferences(
                context.getApplicationContext());
    }

    public static synchronized AodBrightnessController getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new AodBrightnessController(context);
        }
        return sInstance;
    }

    /** Whether the user selected high (~50 nits) AOD brightness. Default is false (10 nits). */
    public boolean isHighBrightnessEnabled() {
        return mSharedPrefs.getBoolean(Constants.KEY_AOD_HIGH_BRIGHTNESS, false);
    }

    /**
     * Apply the user choice to the kernel node and persist it.
     *
     * @param highBrightness true → 50 nits (node 0); false → 10 nits (node 1)
     * @return true if the node write succeeded (or node absent was treated as noop success)
     */
    public boolean setHighBrightness(boolean highBrightness) {
        if (!FileUtils.isFileWritable(Constants.NODE_AOD_LIGHT_MODE)) {
            Log.w(TAG, "AOD light-mode node is not writable: " + Constants.NODE_AOD_LIGHT_MODE);
            // Still persist so a later restore can apply once the node appears.
            mSharedPrefs.edit()
                    .putBoolean(Constants.KEY_AOD_HIGH_BRIGHTNESS, highBrightness)
                    .commit();
            return false;
        }
        return apply(highBrightness);
    }

    /**
     * Re-apply the persisted choice after boot. The kernel always boots with
     * {@code aod_light_mode=0} (50 nits), so without this we would silently stay on high
     * even when the user left the default (10 nits) selected.
     */
    public void restoreAodBrightness() {
        boolean high = isHighBrightnessEnabled();
        if (!FileUtils.isFileWritable(Constants.NODE_AOD_LIGHT_MODE)) {
            Log.w(TAG, "Cannot restore AOD brightness: node not writable");
            return;
        }
        if (apply(high)) {
            Log.i(TAG, "Restored AOD brightness: "
                    + (high ? "high (50 nits)" : "low (10 nits)"));
        }
    }

    /**
     * Kernel polarity: 0 = high (50 nits), 1 = low (10 nits).
     */
    private boolean apply(boolean highBrightness) {
        final String nodeValue = highBrightness ? "0" : "1";
        if (!FileUtils.writeLine(Constants.NODE_AOD_LIGHT_MODE, nodeValue)) {
            Log.e(TAG, "Failed to write AOD light mode " + nodeValue);
            return false;
        }
        mSharedPrefs.edit()
                .putBoolean(Constants.KEY_AOD_HIGH_BRIGHTNESS, highBrightness)
                .commit();
        Log.i(TAG, "AOD light mode set to " + nodeValue
                + " (" + (highBrightness ? "50 nits" : "10 nits") + ")");
        return true;
    }
}
