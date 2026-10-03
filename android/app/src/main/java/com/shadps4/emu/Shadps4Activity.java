// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Android port contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package com.shadps4.emu;

import android.Manifest;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.system.ErrnoException;
import android.system.Os;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

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
 *   <li>guides the user through a first-launch welcome + storage permission
 *       flow: on Android 13+ asks for POST_NOTIFICATIONS + MANAGE_EXTERNAL_STORAGE;
 *       on Android 11-12 asks for MANAGE_EXTERNAL_STORAGE; on Android 10
 *       and older asks for READ_EXTERNAL_STORAGE at runtime. The flow
 *       re-prompts each launch until the user either grants or explicitly
 *       skips via the "Later" button (recorded in SharedPreferences).</li>
 * </ul>
 */
public class Shadps4Activity extends SDLActivity {

    private static final String PREFS_NAME = "shadps4";
    private static final String PREF_ASKED_ALL_FILES = "asked_all_files_access";
    private static final String PREF_ASKED_READ_STORAGE = "asked_read_storage";
    private static final String PREF_ASKED_NOTIFICATIONS = "asked_notifications";
    private static final String PREF_ASKED_WELCOME = "asked_welcome";

    private static final int REQ_CODE_READ_STORAGE = 0x5344;
    private static final int REQ_CODE_NOTIFICATIONS = 0x5345;
    private static final int REQ_CODE_ALL_FILES = 0x5346;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Force landscape orientation immediately, before any native code
        // runs. MIUI/HyperOS sometimes ignores the manifest's
        // screenOrientation attribute, so we also set it programmatically.
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);

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

            // Auto-pick the emulator console language on first launch based
            // on the system locale. The C++ side reads SHADPS4_DEFAULT_LANG
            // only when no settings.toml exists yet, so users can always
            // override from Settings -> Console Language.
            // Mapping follows the PS4 console-language codes used in
            // src/imgui/big_picture/settings_dialog_imgui.h languageMap.
            String sysLang = java.util.Locale.getDefault().getLanguage();
            String sysCountry = java.util.Locale.getDefault().getCountry();
            String defaultLang;
            if ("zh".equals(sysLang)) {
                // zh-CN / zh-SG -> Simplified; zh-TW / zh-HK / zh-MO -> Traditional
                if ("TW".equals(sysCountry) || "HK".equals(sysCountry) || "MO".equals(sysCountry)) {
                    defaultLang = "10"; // Traditional Chinese
                } else {
                    defaultLang = "11"; // Simplified Chinese
                }
            } else if ("ja".equals(sysLang)) {
                defaultLang = "0";
            } else if ("ko".equals(sysLang)) {
                defaultLang = "9";
            } else if ("fr".equals(sysLang)) {
                defaultLang = "CA".equals(sysCountry) ? "22" : "2";
            } else if ("es".equals(sysLang)) {
                // es-MX / es-* (Latin America) -> LatinAmerican; else Spain
                if ("MX".equals(sysCountry) || "AR".equals(sysCountry) || "CO".equals(sysCountry)
                        || "CL".equals(sysCountry) || "PE".equals(sysCountry)
                        || "VE".equals(sysCountry) || "UY".equals(sysCountry)
                        || "PY".equals(sysCountry) || "BO".equals(sysCountry)
                        || "EC".equals(sysCountry) || "CR".equals(sysCountry)
                        || "GT".equals(sysCountry) || "HN".equals(sysCountry)
                        || "NI".equals(sysCountry) || "PA".equals(sysCountry)
                        || "DO".equals(sysCountry) || "CU".equals(sysCountry)
                        || "PR".equals(sysCountry)) {
                    defaultLang = "20";
                } else {
                    defaultLang = "3";
                }
            } else if ("de".equals(sysLang)) {
                defaultLang = "4";
            } else if ("it".equals(sysLang)) {
                defaultLang = "5";
            } else if ("nl".equals(sysLang)) {
                defaultLang = "6";
            } else if ("pt".equals(sysLang)) {
                defaultLang = "BR".equals(sysCountry) ? "17" : "7";
            } else if ("ru".equals(sysLang)) {
                defaultLang = "8";
            } else if ("tr".equals(sysLang)) {
                defaultLang = "19";
            } else if ("pl".equals(sysLang)) {
                defaultLang = "16";
            } else if ("ar".equals(sysLang)) {
                defaultLang = "21";
            } else if ("th".equals(sysLang)) {
                defaultLang = "27";
            } else if ("vi".equals(sysLang)) {
                defaultLang = "28";
            } else if ("id".equals(sysLang)) {
                defaultLang = "29";
            } else if ("uk".equals(sysLang)) {
                defaultLang = "30";
            } else if ("cs".equals(sysLang)) {
                defaultLang = "23";
            } else if ("hu".equals(sysLang)) {
                defaultLang = "24";
            } else if ("el".equals(sysLang)) {
                defaultLang = "25";
            } else if ("ro".equals(sysLang)) {
                defaultLang = "26";
            } else if ("fi".equals(sysLang)) {
                defaultLang = "12";
            } else if ("sv".equals(sysLang)) {
                defaultLang = "13";
            } else if ("da".equals(sysLang)) {
                defaultLang = "14";
            } else if ("no".equals(sysLang) || "nb".equals(sysLang) || "nn".equals(sysLang)) {
                defaultLang = "15";
            } else {
                defaultLang = "1"; // English (US)
            }
            Os.setenv("SHADPS4_DEFAULT_LANG", defaultLang, true);
        } catch (ErrnoException e) {
            // path_util.cpp has a fallback; keep going.
        }

        super.onCreate(savedInstanceState);

        runFirstLaunchFlow();
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
     * First-launch flow:
     *   welcome dialog -> storage permission (runtime or all-files) ->
     *   notifications permission (Android 13+).
     * Each step records in SharedPreferences that we asked once, so the
     * user is not nagged on every launch if they hit "Later".
     */
    private void runFirstLaunchFlow() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);

        // 1) Welcome dialog (one-shot)
        if (!prefs.getBoolean(PREF_ASKED_WELCOME, false)) {
            prefs.edit().putBoolean(PREF_ASKED_WELCOME, true).apply();
            new AlertDialog.Builder(this)
                    .setTitle(R.string.welcome_title)
                    .setMessage(R.string.welcome_message)
                    .setPositiveButton(R.string.welcome_continue, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            askForStoragePermission();
                        }
                    })
                    .setCancelable(false)
                    .show();
            return;
        }

        askForStoragePermission();
    }

    /**
     * Storage permission flow. On Android 11+ we recommend MANAGE_EXTERNAL_STORAGE
     * (the only way to enumerate arbitrary folders under /storage/emulated/0/);
     * on Android 10 and older we fall back to the runtime READ_EXTERNAL_STORAGE
     * permission.
     */
    private void askForStoragePermission() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ -> MANAGE_EXTERNAL_STORAGE.
            if (Environment.isExternalStorageManager()) {
                askForNotificationsPermission();
                return;
            }
            if (prefs.getBoolean(PREF_ASKED_ALL_FILES, false)) {
                // Already asked once, user dismissed or denied. Don't nag.
                askForNotificationsPermission();
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
                                startActivityForResult(intent, REQ_CODE_ALL_FILES);
                            } catch (Exception e) {
                                try {
                                    startActivity(new Intent(
                                            Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                                } catch (Exception e2) {
                                    Toast.makeText(Shadps4Activity.this,
                                            R.string.all_files_manual,
                                            Toast.LENGTH_LONG).show();
                                    askForNotificationsPermission();
                                }
                            }
                        }
                    })
                    .setNegativeButton(R.string.all_files_later, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            askForNotificationsPermission();
                        }
                    })
                    .setCancelable(false)
                    .show();
        } else {
            // Android 10 and older -> runtime READ_EXTERNAL_STORAGE.
            if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                    == PackageManager.PERMISSION_GRANTED) {
                askForNotificationsPermission();
                return;
            }
            if (prefs.getBoolean(PREF_ASKED_READ_STORAGE, false)) {
                // Already asked once, don't keep nagging.
                askForNotificationsPermission();
                return;
            }
            prefs.edit().putBoolean(PREF_ASKED_READ_STORAGE, true).apply();

            new AlertDialog.Builder(this)
                    .setTitle(R.string.storage_title)
                    .setMessage(R.string.storage_message)
                    .setPositiveButton(R.string.storage_open, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            requestPermissions(
                                    new String[]{Manifest.permission.READ_EXTERNAL_STORAGE},
                                    REQ_CODE_READ_STORAGE);
                        }
                    })
                    .setNegativeButton(R.string.storage_later, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            Toast.makeText(Shadps4Activity.this, R.string.storage_denied,
                                    Toast.LENGTH_LONG).show();
                            askForNotificationsPermission();
                        }
                    })
                    .setCancelable(false)
                    .show();
        }
    }

    /**
     * Android 13+ POST_NOTIFICATIONS prompt. Best-effort: don't block if the
     * user denies; trophy popups just fall back to in-app ImGui windows.
     */
    private void askForNotificationsPermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            return;
        }
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                == PackageManager.PERMISSION_GRANTED) {
            return;
        }
        if (prefs.getBoolean(PREF_ASKED_NOTIFICATIONS, false)) {
            return;
        }
        prefs.edit().putBoolean(PREF_ASKED_NOTIFICATIONS, true).apply();
        requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},
                REQ_CODE_NOTIFICATIONS);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_CODE_ALL_FILES) {
            // After the user comes back from the All-files-access screen, we
            // still want to continue into the notifications prompt.
            askForNotificationsPermission();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_CODE_READ_STORAGE) {
            boolean granted = grantResults.length > 0
                    && grantResults[0] == PackageManager.PERMISSION_GRANTED;
            if (!granted) {
                Toast.makeText(this, R.string.storage_denied, Toast.LENGTH_LONG).show();
            }
            askForNotificationsPermission();
        } else if (requestCode == REQ_CODE_NOTIFICATIONS) {
            // Best-effort: don't toast on deny, fall through silently.
        }
    }
}
