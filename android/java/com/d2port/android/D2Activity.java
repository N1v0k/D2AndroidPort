package com.d2port.android;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDLActivity;

/**
 * The game's activity: SDL's activity (libSDL2.so + libmain.so, SDL_main in the C code) plus
 *  - a Wi-Fi multicast lock while it runs, so the LAN game discovery (UDP broadcast) reaches the phone,
 *  - restartApp(), called from C when the language changes (the game restarts in a fresh process),
 *  - requestAllFilesAccess(), from C when the game files are missing: the settings page that lets the game
 *    read the "d2port" folder in the phone's storage,
 *  - a clean process exit when the game quits, so the next launch starts from zero (C globals).
 */
public class D2Activity extends SDLActivity {
    private static final String TAG = "d2port";
    private WifiManager.MulticastLock mMulticast;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                mMulticast = wifi.createMulticastLock("d2port-lan");
                mMulticast.setReferenceCounted(false);
                mMulticast.acquire();
            }
        } catch (Exception e) {
            Log.w(TAG, "no multicast lock: " + e);
        }
    }

    @Override
    protected void onDestroy() {
        if (mMulticast != null && mMulticast.isHeld()) {
            mMulticast.release();
        }
        boolean finishing = isFinishing();
        super.onDestroy();
        if (finishing) {
            // SDL_main has returned (or was told to quit and joined): start fresh next time
            System.exit(0);
        }
    }

    /** From C (compat_check_data): the "All files access" page for this app (Android 11+). */
    public void requestAllFilesAccess() {
        if (Build.VERSION.SDK_INT < 30) {
            return;
        }
        try {
            Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                  Uri.parse("package:" + getPackageName()));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
        } catch (Exception e) {
            try {
                Intent i = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(i);
            } catch (Exception e2) {
                Log.e(TAG, "no settings page for all files access: " + e2);
            }
        }
    }

    /** From C (compat_restart): open the game again in a new process, then end this one. */
    public void restartApp() {
        try {
            Intent i = new Intent(this, D2RestartActivity.class);
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
        } catch (Exception e) {
            Log.e(TAG, "restart failed: " + e);
            return;
        }
        Runtime.getRuntime().exit(0);
    }
}
