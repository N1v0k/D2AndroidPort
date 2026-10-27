package com.d2port.android;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.text.InputType;
import android.util.TypedValue;
import android.view.View;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The launcher: with the game files in place it opens the game (D2Activity) right away; without them it is
 * the import screen. The import copies from the player's own Diablo II: Resurrected (its folder with
 * .build.info and Data) the files the port uses, with the importer (tools/d2port-importar.c, built for
 * Android as lib/arm64-v8a/libd2import.so), into the "d2port" folder of the phone's storage.
 */
public class SetupActivity extends Activity {
    private static final int REQ_TREE = 1, REQ_LEGACY = 2;
    private static final String[][] LANGS = {
        { "deDE", "Deutsch" }, { "enUS", "English" }, { "esMX", "Español (Latinoamérica)" },
        { "esES", "Español (España)" }, { "frFR", "Français" }, { "itIT", "Italiano" },
        { "ptBR", "Português (Brasil)" }, { "plPL", "Polski" }, { "ruRU", "Русский" },
        { "jaJP", "日本語" }, { "koKR", "한국어" }, { "zhCN", "简体中文" }, { "zhTW", "繁體中文" },
    };

    private boolean de;
    private TextView permState, findState, log;
    private Button permButton, importButton, playButton, findButton, pickButton;
    private EditText pathEdit;
    private Spinner langSpinner;
    private LinearLayout found;
    private ScrollView scroll;
    private volatile boolean running;

    private String t(String german, String english) {
        return de ? german : english;
    }

    /** Where the game lives: the "d2port" folder of the phone's storage (needs file access). */
    static File publicDir() {
        return new File(Environment.getExternalStorageDirectory(), "d2port");
    }

    static boolean hasData(File dir) {
        return dir != null && new File(dir, "data/global/palette/act1/pal.dat").canRead();
    }

    private boolean hasFileAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            return Environment.isExternalStorageManager();
        }
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private boolean gameReady() {
        return (hasFileAccess() && hasData(publicDir())) || hasData(getExternalFilesDir(null));
    }

    private void startGame() {
        Intent i = new Intent(this, D2Activity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(i);
        finish();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        de = Locale.getDefault().getLanguage().equals("de");
        if (gameReady() && !getIntent().getBooleanExtra("setup", false)) {
            startGame();
            return;
        }
        buildUi();
        searchFolders();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (permState != null) {
            refresh();
        }
    }

    // ---- UI (built in code: no layouts or resources needed) ----

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private TextView text(LinearLayout parent, String s, float size, boolean bold) {
        TextView v = new TextView(this);
        v.setText(s);
        v.setTextSize(size);
        v.setTextColor(bold ? Color.WHITE : Color.rgb(210, 210, 210));
        if (bold) {
            v.setTypeface(Typeface.DEFAULT_BOLD);
        }
        v.setPadding(0, dp(bold ? 14 : 4), 0, dp(4));
        parent.addView(v);
        return v;
    }

    private Button button(LinearLayout parent, String s, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setOnClickListener(l);
        parent.addView(b);
        return b;
    }

    private void buildUi() {
        scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.rgb(18, 12, 10));
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(20), dp(20), dp(40));
        scroll.addView(root);

        TextView title = text(root, "Diablo II Port", 26, true);
        title.setTextColor(Color.rgb(215, 170, 80));
        text(root, t("Der Port braucht die Dateien deines eigenen Diablo II: Resurrected. Er kopiert nur, was er "
                     + "verwendet (etwa 1,6 GB), in den Ordner „d2port“ im internen Speicher. "
                     + "Danach kannst du den Ordner von Diablo II: Resurrected wieder löschen.",
                     "The port needs the files of your own Diablo II: Resurrected. It copies only what it uses "
                     + "(about 1.6 GB) to the folder “d2port” in the phone's storage. After that you can "
                     + "delete the Diablo II: Resurrected folder again."), 15, false);

        text(root, t("1. Zugriff auf die Dateien", "1. File access"), 18, true);
        permState = text(root, "", 15, false);
        permButton = button(root, t("Zugriff erlauben", "Allow access"), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                requestFileAccess();
            }
        });

        text(root, t("2. Ordner von Diablo II: Resurrected", "2. Diablo II: Resurrected folder"), 18, true);
        text(root, t("Der Ordner, in dem D2R.exe liegt (Battle.net- oder Steam-Version). Android lässt keine App "
                     + "in die Ordner anderer Apps (z. B. GameHub unter Android/data): kopiere oder verschiebe den "
                     + "Ordner zuerst mit einem Dateimanager in den internen Speicher, z. B. nach Download.",
                     "The folder with D2R.exe (Battle.net or Steam edition). Android does not let an app "
                     + "into other apps' folders (e.g. GameHub's under Android/data): first copy or move the folder "
                     + "to the phone's storage with a file manager, e.g. to Download."), 15, false);
        findState = text(root, "", 15, false);
        found = new LinearLayout(this);
        found.setOrientation(LinearLayout.VERTICAL);
        root.addView(found);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(row);
        findButton = button(row, t("Erneut suchen", "Search again"), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                searchFolders();
            }
        });
        pickButton = button(row, t("Ordner wählen…", "Choose folder…"), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                try {
                    startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_TREE);
                } catch (Exception e) {
                    append("! " + e);
                }
            }
        });
        pathEdit = new EditText(this);
        pathEdit.setHint(t("Pfad, z. B. /storage/emulated/0/Download/Diablo II Resurrected",
                           "Path, e.g. /storage/emulated/0/Download/Diablo II Resurrected"));
        pathEdit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        pathEdit.setTextColor(Color.WHITE);
        pathEdit.setHintTextColor(Color.GRAY);
        pathEdit.setSingleLine(true);
        root.addView(pathEdit);

        text(root, t("3. Sprache (Texte und Stimmen)", "3. Language (texts and voices)"), 18, true);
        text(root, t("Die Stimmen gibt es nur in den Sprachen, die dein Diablo II: Resurrected heruntergeladen hat; "
                     + "sonst bleiben sie englisch.",
                     "Voices exist only in the languages your Diablo II: Resurrected downloaded; otherwise they "
                     + "stay in English."), 15, false);
        langSpinner = new Spinner(this);
        List<String> names = new ArrayList<String>();
        int def = 1;
        String sys = Locale.getDefault().getLanguage() + Locale.getDefault().getCountry();
        for (int i = 0; i < LANGS.length; i++) {
            names.add(LANGS[i][1]);
            if (LANGS[i][0].equalsIgnoreCase(sys)) {
                def = i;
            } else if (def == 1 && LANGS[i][0].substring(0, 2).equals(Locale.getDefault().getLanguage())) {
                def = i;
            }
        }
        ArrayAdapter<String> ad = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_dropdown_item, names);
        langSpinner.setAdapter(ad);
        langSpinner.setSelection(def);
        langSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> p, View v, int pos, long id) {
                if (v instanceof TextView) {
                    ((TextView) v).setTextColor(Color.WHITE);
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> p) {
            }
        });
        root.addView(langSpinner);

        text(root, t("4. Importieren", "4. Import"), 18, true);
        importButton = button(root, t("Importieren", "Import"), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startImport();
            }
        });
        playButton = button(root, t("Spielen", "Play"), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startGame();
            }
        });
        log = new TextView(this);
        log.setTypeface(Typeface.MONOSPACE);
        log.setTextSize(12);
        log.setTextColor(Color.rgb(190, 220, 190));
        log.setPadding(0, dp(10), 0, 0);
        root.addView(log);

        setContentView(scroll);
        refresh();
    }

    private void refresh() {
        boolean access = hasFileAccess();
        permState.setText(access ? t("✔ erlaubt", "✔ allowed")
                                 : t("Noch nicht erlaubt: tippe auf „Zugriff erlauben“ und schalte "
                                     + "„Zugriff auf alle Dateien“ für diese App ein.",
                                     "Not allowed yet: tap “Allow access” and turn on “All files "
                                     + "access” for this app."));
        permButton.setVisibility(access ? View.GONE : View.VISIBLE);
        importButton.setEnabled(access && !running);
        findButton.setEnabled(!running);
        pickButton.setEnabled(!running);
        playButton.setVisibility(gameReady() ? View.VISIBLE : View.GONE);
    }

    private void requestFileAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                         Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            requestPermissions(new String[] { Manifest.permission.READ_EXTERNAL_STORAGE,
                                              Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQ_LEGACY);
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        refresh();
        searchFolders();
    }

    @Override
    protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code != REQ_TREE || result != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        // content://.../tree/primary:Download/Diablo II Resurrected -> /storage/emulated/0/Download/...
        try {
            String id = DocumentsContract.getTreeDocumentId(data.getData());
            String[] parts = id.split(":", 2);
            String base = "primary".equalsIgnoreCase(parts[0]) ? Environment.getExternalStorageDirectory().getPath()
                                                                : "/storage/" + parts[0];
            String path = parts.length > 1 && parts[1].length() > 0 ? base + "/" + parts[1] : base;
            path = gameRoot(new File(path)).getPath();
            pathEdit.setText(path);
            if (!isGameDir(new File(path))) {
                append(t("! Das ist nicht der Ordner von Diablo II: Resurrected: wähle den, in dem D2R.exe liegt.",
                         "! That is not the Diablo II: Resurrected folder: choose the one with D2R.exe."));
            }
        } catch (Exception e) {
            append("! " + e);
        }
    }

    // ---- Finding the Diablo II: Resurrected folder ----

    /** Battle.net install (.build.info and Data) or the Steam edition (data/.build.config). */
    private static boolean isGameDir(File d) {
        return (new File(d, ".build.info").isFile() && new File(d, "Data").isDirectory())
            || new File(d, "data/.build.config").isFile();
    }

    /** The folder chosen may be the Steam edition's "data" folder itself: then the game is its parent. */
    private static File gameRoot(File d) {
        if (new File(d, ".build.config").isFile() && d.getParentFile() != null) {
            return d.getParentFile();
        }
        return d;
    }

    private void searchFolders() {
        if (findState == null) {
            return;
        }
        if (!hasFileAccess()) {
            findState.setText(t("(Die Suche geht, sobald der Zugriff erlaubt ist.)",
                                "(The search works once access is allowed.)"));
            return;
        }
        findState.setText(t("Suche…", "Searching…"));
        found.removeAllViews();
        new Thread(new Runnable() {
            @Override
            public void run() {
                final List<File> hits = new ArrayList<File>();
                File top = Environment.getExternalStorageDirectory();
                ArrayDeque<File> queue = new ArrayDeque<File>();
                ArrayDeque<Integer> depth = new ArrayDeque<Integer>();
                queue.add(top);
                depth.add(0);
                int visited = 0;
                while (!queue.isEmpty() && visited < 40000 && hits.size() < 8) {
                    File d = queue.poll();
                    int dd = depth.poll();
                    visited++;
                    if (isGameDir(d)) {
                        hits.add(d);
                        continue;
                    }
                    if (dd >= 6) {
                        continue;
                    }
                    File[] subs = d.listFiles();
                    if (subs == null) {
                        continue;
                    }
                    for (File s : subs) {
                        String n = s.getName();
                        if (!s.isDirectory() || n.startsWith(".") || (dd == 0 && n.equals("Android"))
                            || n.equals("d2port")) {
                            continue;
                        }
                        queue.add(s);
                        depth.add(dd + 1);
                    }
                }
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        showHits(hits);
                    }
                });
            }
        }).start();
    }

    private void showHits(List<File> hits) {
        found.removeAllViews();
        if (hits.isEmpty()) {
            findState.setText(t("Kein Ordner von Diablo II: Resurrected im internen Speicher gefunden. Kopiere ihn "
                                + "dorthin und tippe auf „Erneut suchen“, oder gib den Pfad unten ein.",
                                "No Diablo II: Resurrected folder found in the phone's storage. Copy it there and "
                                + "tap “Search again”, or type the path below."));
            return;
        }
        findState.setText(t("Gefunden (antippen zum Auswählen):", "Found (tap to choose):"));
        for (final File f : hits) {
            Button b = new Button(this);
            b.setAllCaps(false);
            b.setText(f.getPath());
            b.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    pathEdit.setText(f.getPath());
                }
            });
            found.addView(b);
        }
        if (pathEdit.getText().length() == 0) {
            pathEdit.setText(hits.get(0).getPath());
        }
    }

    // ---- The import ----

    private void append(final String line) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                log.append(line + "\n");
                scroll.post(new Runnable() {
                    @Override
                    public void run() {
                        scroll.fullScroll(View.FOCUS_DOWN);
                    }
                });
            }
        });
    }

    private void startImport() {
        final String game = gameRoot(new File(pathEdit.getText().toString().trim())).getPath();
        pathEdit.setText(game);
        if (!isGameDir(new File(game))) {
            append(t("! Kein Ordner von Diablo II: Resurrected: „" + game + "“ (wähle den Ordner mit D2R.exe).",
                     "! Not a Diablo II: Resurrected folder: “" + game + "” (choose the folder with D2R.exe)."));
            return;
        }
        final File dest = publicDir();
        if (!dest.isDirectory() && !dest.mkdirs()) {
            append(t("! Ich kann den Ordner " + dest + " nicht anlegen.", "! Cannot create the folder " + dest));
            return;
        }
        try {
            long free = new StatFs(dest.getPath()).getAvailableBytes();
            if (free < 3L * 1024 * 1024 * 1024) {
                append(t("! Achtung: nur " + (free >> 20) + " MB frei, der Import braucht etwa 2,5 GB.",
                         "! Warning: only " + (free >> 20) + " MB free, the import needs about 2.5 GB."));
            }
        } catch (Exception e) {
            // (not important)
        }
        final String lang = LANGS[langSpinner.getSelectedItemPosition()][0];
        running = true;
        refresh();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        log.setText("");
        append(t("Import von " + game + " nach " + dest + " (" + lang + ")…",
                 "Importing from " + game + " to " + dest + " (" + lang + ")…"));
        new Thread(new Runnable() {
            @Override
            public void run() {
                int rc = -1;
                try {
                    // the list of the files the port uses, where the importer looks for it
                    InputStream in = getAssets().open("archivos.txt");
                    OutputStream out = new FileOutputStream(new File(dest, "archivos.txt"));
                    byte[] buf = new byte[65536];
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        out.write(buf, 0, n);
                    }
                    in.close();
                    out.close();
                    String exe = getApplicationInfo().nativeLibraryDir + "/libd2import.so";
                    ProcessBuilder pb = new ProcessBuilder(exe, "--idioma", lang, "--juego", game, "--destino",
                                                           dest.getPath(), "--sin-pausa", "--sin-cinematicas");
                    pb.redirectErrorStream(true);
                    pb.directory(dest);
                    Process p = pb.start();
                    p.getOutputStream().close();
                    BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream(), "UTF-8"));
                    String line;
                    while ((line = r.readLine()) != null) {
                        append(line);
                    }
                    rc = p.waitFor();
                } catch (Exception e) {
                    append("! " + e);
                }
                final int code = rc;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        running = false;
                        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        if (hasData(dest)) {
                            append(code == 0 ? t("\n✔ Fertig! Tippe auf „Spielen“.",
                                                 "\n✔ Done! Tap “Play”.")
                                             : t("\nFertig, aber einige Dateien fehlen (siehe d2port/importar.log). "
                                                 + "Das Spiel läuft meistens trotzdem.",
                                                 "\nDone, but some files are missing (see d2port/importar.log). "
                                                 + "The game usually works anyway."));
                        } else {
                            append(t("\n✘ Der Import hat nicht geklappt (Code " + code + ").",
                                     "\n✘ The import did not work (code " + code + ")."));
                        }
                        refresh();
                    }
                });
            }
        }).start();
    }

    @Override
    public void onBackPressed() {
        if (!running) {
            super.onBackPressed();
        }
    }
}
