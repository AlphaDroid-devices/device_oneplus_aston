/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */
package org.lineageos.device.settings;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.Locale;

/** Resolves thermal zones by type, since the zone numbering shifts between builds. */
public final class ThermalZones {

    private static final String THERMAL_ROOT = "/sys/class/thermal";

    /**
     * Zone types that report the CPU, most specific first. Deliberately excludes the
     * chassis sensors (skin, shell), socd (a throttle level, not millidegrees) and
     * thermal_zone0, which is the RF power amplifier and reads ~9C below the cores.
     */
    private static final String[] CPU_ZONE_TYPES = { "cpuss", "cpu-" };

    private static String sCpuTemp;
    private static boolean sResolved;

    private ThermalZones() { }

    /** Path to a readable CPU temperature node, or null if none is exposed. */
    public static synchronized String cpuTempPath() {
        if (sResolved) {
            return sCpuTemp;
        }
        sResolved = true;
        final File[] zones = new File(THERMAL_ROOT).listFiles(
                (dir, name) -> name.startsWith("thermal_zone"));
        if (zones == null) {
            return null;
        }
        for (String want : CPU_ZONE_TYPES) {
            for (File zone : zones) {
                final String type = firstLine(new File(zone, "type").getPath());
                if (type == null
                        || !type.trim().toLowerCase(Locale.US).startsWith(want)) {
                    continue;
                }
                final File temp = new File(zone, "temp");
                if (temp.canRead()) {
                    sCpuTemp = temp.getPath();
                    return sCpuTemp;
                }
            }
        }
        return null;
    }

    /** Celsius from a raw thermal node value, which may be milli- or deci-degrees. */
    public static float toCelsius(float raw) {
        if (raw > 1000f) {
            return raw / 1000f;
        }
        if (raw > 100f) {
            return raw / 10f;
        }
        return raw;
    }

    private static String firstLine(String path) {
        try (BufferedReader br = new BufferedReader(new FileReader(path))) {
            return br.readLine();
        } catch (IOException e) {
            return null;
        }
    }
}
