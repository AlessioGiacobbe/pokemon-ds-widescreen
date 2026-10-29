// Starts the app again after QUIT in the pause menu (see GameActivity.restart): runs in a
// process of its own (":restart" in the manifest), ends the app's process, and opens the app,
// which starts in a fresh process on its game list.
package io.github.mamesj.pokemonds;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

public class Restart extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        int pid = getIntent().getIntExtra("pid", -1);
        if (pid > 0) Process.killProcess(pid);
        Intent main = new Intent(this, GameActivity.class);
        main.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(main);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
