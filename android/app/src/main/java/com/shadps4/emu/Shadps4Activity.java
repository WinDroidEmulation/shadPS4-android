// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Android port contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package com.shadps4.emu;

import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.system.ErrnoException;
import android.system.Os;
import android.widget.Toast;

import java.io.File;

import org.libsdl.app.SDLActivity;

/**
 * Entry point of the shadPS4 Android port.
 *
 * <p>The emulator core lives in {@code libmain.so} and is loaded by SDLActivity
 * after {@code libSDL3.so}. This subclass wires up the Android specifics:</p>
 *
 * <ul>
 *   <li>exports {@code SHADPS4_DATA_DIR} before any native code runs, so
 *       path_util.cpp places user data / logs / caches inside the app's
 *       external files directory;</li>
 *   <li>declares the native libraries to load;</li>
 *   <li>optionally guides the user to the "All files access" screen so the Big
 *       Picture library can enumerate games stored on shared storage.</li>
 * </ul>
 */
public class Shadps4Activity extends SDLActivity {

    private static final String PREFS_NAME = "shadps4";
    private static final String PREF_ASKED_ALL_FILES = "asked_all_files_access";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Must run before the native libraries are loaded by the SDL startup
        // sequence, otherwise path_util.cpp would fall back to a hard-coded
        // default directory.
        try {
            File dataDir = getExternalFilesDir(null);
            if (dataDir == null) {
                dataDir = getFilesDir();
            }
            // android.system.Os.setenv requires API 21; minSdk is 29.
            Os.setenv("SHADPS4_DATA_DIR", dataDir.getAbsolutePath(), true);
        } catch (ErrnoException e) {
            // path_util.cpp has a fallback; keep going.
        }

        super.onCreate(savedInstanceState);

        maybeAskForAllFilesAccess();
    }

    /**
     * SDLActivity loads these in order. libSDL3.so comes from the in-tree
     * SDL3 build, libmain.so is the emulator core itself.
     */
    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL3", "main"};
    }

    /**
     * One-time prompt that points the user at the system "All files access"
     * screen. Games kept inside the app directory work without it; this is
     * only needed for libraries on shared storage.
     */
    private void maybeAskForAllFilesAccess() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return;
        }
        if (Environment.isExternalStorageManager()) {
            return;
        }
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        if (prefs.getBoolean(PREF_ASKED_ALL_FILES, false)) {
            return;
        }
        prefs.edit().putBoolean(PREF_ASKED_ALL_FILES, true).apply();

        new AlertDialog.Builder(this)
                .setTitle(R.string.all_files_title)
                .setMessage(R.string.all_files_message)
                .setPositiveButton(R.string.all_files_open, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        try {
                            Intent intent = new Intent(
                                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                    Uri.parse("package:" + getPackageName()));
                            startActivity(intent);
                        } catch (Exception e) {
                            try {
                                startActivity(
                                        new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                            } catch (Exception e2) {
                                Toast.makeText(Shadps4Activity.this, R.string.all_files_manual,
                                        Toast.LENGTH_LONG).show();
                            }
                        }
                    }
                })
                .setNegativeButton(R.string.all_files_later, null)
                .show();
    }
}
