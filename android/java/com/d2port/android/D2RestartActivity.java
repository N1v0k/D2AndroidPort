package com.d2port.android;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * Runs in its own process (":restart"): the game process has just exited; this starts the game
 * activity again (in a new process) and exits too. Same idea as Jake Wharton's ProcessPhoenix.
 */
public class D2RestartActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent i = new Intent(this, D2Activity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(i);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
