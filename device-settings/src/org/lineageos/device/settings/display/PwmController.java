/*
 * SPDX-FileCopyrightText: 2025 AlphaDroid
 * SPDX-License-Identifier: Apache-2.0
 */
package org.lineageos.device.settings.display;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import androidx.preference.PreferenceManager;

import org.lineageos.device.settings.Constants;
import org.lineageos.device.settings.utils.FileUtils;

public class PwmController {
    private static final String TAG = "PwmController";
    private static PwmController sInstance;
    private final Context mContext;
    private final SharedPreferences mSharedPrefs;

    /**
     * Panel settle after HBM exit / PWM drive-mode switch. HBM EXIT rewrites gamma
     * and 0x51; stacking DC↔1P on top within a few ms produces crazy colors and
     * peak brightness. ~2 frames + ADFR kickoff is enough; dmesg showed ~11 ms was not.
     */
    static final long PANEL_MODE_SETTLE_MS = 150;

    private PwmController(Context context) {
        mContext = context.getApplicationContext();
        mSharedPrefs = PreferenceManager.getDefaultSharedPreferences(mContext);
    }

    public static synchronized PwmController getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new PwmController(context);
        }
        return sInstance;
    }

    public boolean isPwmEnabled() {
        // The kernel state resets on reboot, so the node is the source of truth;
        // the preference is only a fallback while the node is unreadable
        String value = FileUtils.readLineTrimmed(Constants.NODE_ONEPULSE_PWM);
        if (value != null) {
            return "1".equals(value);
        }
        return mSharedPrefs.getBoolean(Constants.KEY_ONEPULSE_PWM, false);
    }

    /**
     * Re-apply the persisted PWM choice after boot: the panel always comes up with
     * one-pulse disabled, so a user selection would otherwise be lost on reboot.
     */
    public void restorePwmSetting() {
        boolean wanted = mSharedPrefs.getBoolean(Constants.KEY_ONEPULSE_PWM, false);
        if (wanted && !isPwmEnabled()) {
            if (FileUtils.isFileWritable(Constants.NODE_ONEPULSE_PWM)) {
                setPwm(true);
                Log.i(TAG, "Restored PWM setting after boot");
            } else {
                Log.w(TAG, "PWM node is not writable, cannot restore setting");
            }
        }
    }

    public boolean enablePwm() {
        if (!FileUtils.isFileWritable(Constants.NODE_ONEPULSE_PWM)) {
            Log.w(TAG, "PWM node is not writable");
            return false;
        }

        // PWM has priority: tear HBM down fully, then wait before DC→1P.
        HbmController hbmController = HbmController.getInstance(mContext);
        if (hbmController.isHbmEnabled()) {
            Log.i(TAG, "HBM is active, disabling it (PWM has priority)");
            if (!hbmController.disableHbm()) {
                Log.w(TAG, "Failed to disable HBM before enabling PWM");
                return false;
            }
            settlePanel("after HBM off, before PWM on");
        }

        setPwm(true);
        return true;
    }

    public boolean disablePwm() {
        if (!FileUtils.isFileWritable(Constants.NODE_ONEPULSE_PWM)) {
            Log.w(TAG, "PWM node is not writable");
            return false;
        }

        setPwm(false);
        // Kernel re-applies BL so 1P→DC runs now; settle before a following HBM on.
        settlePanel("after PWM off");
        return true;
    }

    static void settlePanel(String reason) {
        try {
            Log.i(TAG, "Panel settle " + PANEL_MODE_SETTLE_MS + "ms (" + reason + ")");
            Thread.sleep(PANEL_MODE_SETTLE_MS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            Log.w(TAG, "Panel settle interrupted (" + reason + ")");
        }
    }

    private void setPwm(boolean enable) {
        FileUtils.writeLine(Constants.NODE_ONEPULSE_PWM, enable ? "1" : "0");
        mSharedPrefs.edit().putBoolean(Constants.KEY_ONEPULSE_PWM, enable).commit();
        Log.i(TAG, "PWM set to: " + enable);
    }
}
