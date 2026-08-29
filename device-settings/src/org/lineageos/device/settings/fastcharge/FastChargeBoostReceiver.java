/*
 * SPDX-FileCopyrightText: 2026 AlphaDroid
 * SPDX-License-Identifier: Apache-2.0
 *
 * SystemUI charging-animation long-press. Restricted to STATUS_BAR_SERVICE.
 */

package org.lineageos.device.settings.fastcharge;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

import org.lineageos.device.settings.Constants;

public class FastChargeBoostReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !Constants.ACTION_BOOST_CHARGING.equals(intent.getAction())) {
            return;
        }
        FastChargeController.getInstance(context).boostSession();
    }
}
