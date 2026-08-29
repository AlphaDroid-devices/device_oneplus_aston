/*
 * SPDX-FileCopyrightText: 2026 AlphaDroid
 * SPDX-License-Identifier: Apache-2.0
 *
 * Current-cap policy for SuperVOOC via /sys/class/oplus_chg/battery/cool_down.
 * Direct sysfs, same pattern as BypassChargingController. The kernel COOL_DOWN
 * votable is VOTE_MIN; writing 0 unvotes USER_VOTER (unlimited SuperVOOC).
 */

package org.lineageos.device.settings.fastcharge;

import android.content.Context;
import android.content.SharedPreferences;
import android.provider.Settings;
import android.util.Log;

import androidx.preference.PreferenceManager;

import org.lineageos.device.settings.Constants;
import org.lineageos.device.settings.utils.FileUtils;

public class FastChargeController {

    private static final String TAG = "FastChargeController";

    /** Unvote USER_VOTER: SuperVOOC up to 9.5A / 100W. */
    private static final int COOL_DOWN_UNLIMITED = 0;
    /** svooc_2_0_curr_table level 1 = 1500 mA (~15W). */
    private static final int COOL_DOWN_NIGHT = 1;
    /** svooc_2_0_curr_table level 5 = 3000 mA (~30W). Fast-off without night. */
    private static final int COOL_DOWN_STANDARD = 5;

    private static final boolean FAST_CHARGING_DEFAULT = true;
    private static final boolean NIGHT_CHARGING_DEFAULT = false;

    private static FastChargeController sInstance;

    private final Context mContext;
    private final SharedPreferences mSharedPrefs;
    private final Object mLock = new Object();

    /** One-shot unlimited cap until unplug. Does not change persisted prefs. */
    private boolean mSessionBoost;

    public static synchronized FastChargeController getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new FastChargeController(context.getApplicationContext());
        }
        return sInstance;
    }

    private FastChargeController(Context context) {
        mContext = context;
        mSharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
    }

    public boolean isSupported() {
        return FileUtils.readLine(Constants.NODE_COOL_DOWN) != null;
    }

    public boolean isFastChargingEnabled() {
        return mSharedPrefs.getBoolean(Constants.KEY_FAST_CHARGING, FAST_CHARGING_DEFAULT);
    }

    public boolean isNightModeEnabled() {
        return mSharedPrefs.getBoolean(Constants.KEY_NIGHT_CHARGING, NIGHT_CHARGING_DEFAULT);
    }

    /**
     * Re-apply the persisted cap. Kernel USER_VOTER resets on reboot, so this
     * must run at boot and on plug. Also clears a session boost — reconnect
     * always resumes DeviceSettings prefs.
     */
    public void restore() {
        synchronized (mLock) {
            mSessionBoost = false;
            applyLocked();
        }
    }

    public void handlePowerConnected() {
        if (Constants.DEBUG) Log.i(TAG, "Power connected");
        restore();
    }

    public void handlePowerDisconnected() {
        synchronized (mLock) {
            if (Constants.DEBUG) Log.i(TAG, "Power disconnected");
            mSessionBoost = false;
            // USER_VOTER survives unplug; write the persisted cap so the next
            // plug starts limited even if this process is not running then.
            applyLocked();
        }
    }

    public boolean setFastChargingEnabled(boolean enabled) {
        synchronized (mLock) {
            mSessionBoost = false;
            int level = computeLevel(enabled, isNightModeEnabled());
            if (!writeCoolDownLocked(level)) {
                return false;
            }
            mSharedPrefs.edit().putBoolean(Constants.KEY_FAST_CHARGING, enabled).commit();
            publishHudStateLocked();
            return true;
        }
    }

    public boolean setNightModeEnabled(boolean enabled) {
        synchronized (mLock) {
            mSessionBoost = false;
            int level = computeLevel(isFastChargingEnabled(), enabled);
            if (!writeCoolDownLocked(level)) {
                return false;
            }
            mSharedPrefs.edit().putBoolean(Constants.KEY_NIGHT_CHARGING, enabled).commit();
            publishHudStateLocked();
            return true;
        }
    }

    /**
     * Charging-animation long-press: uncap for this plug session only.
     * Prefs are unchanged; unplug/replug restores them.
     */
    public void boostSession() {
        synchronized (mLock) {
            if (writeCoolDownLocked(COOL_DOWN_UNLIMITED)) {
                mSessionBoost = true;
                publishHudStateLocked();
                if (Constants.DEBUG) Log.i(TAG, "Session boost: cool_down uncapped");
            }
        }
    }

    private void applyLocked() {
        int level = mSessionBoost
                ? COOL_DOWN_UNLIMITED
                : computeLevel(isFastChargingEnabled(), isNightModeEnabled());
        writeCoolDownLocked(level);
        publishHudStateLocked();
    }

    /**
     * Mirror cap + HUD policy so SystemUI can show the long-press hint and a
     * truthful watt / night-mode label without reading oplus_chg (platform_app
     * is denied search on that sysfs dir).
     */
    private void publishHudStateLocked() {
        boolean available = !mSessionBoost && !isFastChargingEnabled();
        int mode;
        if (mSessionBoost || isFastChargingEnabled()) {
            mode = Constants.CHARGE_HUD_UNLIMITED;
        } else if (isNightModeEnabled()) {
            mode = Constants.CHARGE_HUD_NIGHT;
        } else {
            mode = Constants.CHARGE_HUD_STANDARD;
        }
        try {
            Settings.System.putInt(mContext.getContentResolver(),
                    Constants.SETTINGS_CHARGE_BOOST_AVAILABLE, available ? 1 : 0);
            Settings.System.putInt(mContext.getContentResolver(),
                    Constants.SETTINGS_CHARGE_HUD_MODE, mode);
        } catch (Exception e) {
            Log.w(TAG, "Failed to publish hud mode=" + mode
                    + " boost-available=" + available, e);
        }
    }

    private int computeLevel(boolean fast, boolean night) {
        if (fast) {
            return COOL_DOWN_UNLIMITED;
        }
        if (night) {
            return COOL_DOWN_NIGHT;
        }
        return COOL_DOWN_STANDARD;
    }

    /**
     * Write cool_down. Readback is logged only: COOL_DOWN is VOTE_MIN, so other
     * voters (USB temp, etc.) can leave the node below what USER_VOTER asked.
     */
    private boolean writeCoolDownLocked(int level) {
        if (!FileUtils.isFileWritable(Constants.NODE_COOL_DOWN)) {
            Log.w(TAG, "cool_down is not writable");
            return false;
        }
        if (!FileUtils.writeLine(Constants.NODE_COOL_DOWN, Integer.toString(level))) {
            Log.e(TAG, "Failed to write cool_down=" + level);
            return false;
        }
        if (Constants.DEBUG) {
            Log.i(TAG, "Wrote cool_down=" + level
                    + " readback=" + FileUtils.readLineTrimmed(Constants.NODE_COOL_DOWN));
        }
        return true;
    }
}
