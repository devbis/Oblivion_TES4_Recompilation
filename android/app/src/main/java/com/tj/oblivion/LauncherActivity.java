package com.tj.oblivion;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.Manifest;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Point;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.AdapterView;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/* Start screen, in the spirit of the PC OblivionLauncher: pick the folder that
 * holds the Oblivion install, set the graphics options, press Play. The options
 * are written to Oblivion.ini (where the game reads them) before the game
 * starts; the folder is passed to the native side as argv[1]. */
public class LauncherActivity extends Activity {
    private static final int REQ_FOLDER = 1, REQ_PERM = 2;
    private SharedPreferences prefs;
    private TextView folderText, folderStatus;
    private Button playButton;
    private boolean pendingPick, pendingPlay;  /* continue once the storage prompt is answered */
    private boolean askedAllFiles;
    private final Map<String, Spinner> spinners = new LinkedHashMap<>();
    private final Map<String, CheckBox> checks = new LinkedHashMap<>();

    /* option key -> labels, and the INI values each label stands for */
    private static final int[] MODE_HEIGHTS = {1080, 900, 720, 648, 576, 540, 480, 432, 360, 288, 270, 216, 180, 144, 108};

    // Aspect ratio of the picture. Very wide phones (20:9 and up) show the game
    // at 16:9 or so with black bars instead of stretching its UI to the edges.
    private static final String[] ASPECTS = {"16:9", "16:10", "4:3", "21:9", "Screen"};
    private static final int[][] ASPECT_V = {{16, 9}, {16, 10}, {4, 3}, {21, 9}, {0, 0}};

    private int[] screen() {
        Point sz = new Point();
        getWindowManager().getDefaultDisplay().getRealSize(sz);
        return new int[]{Math.max(sz.x, sz.y), Math.min(sz.x, sz.y)};
    }

    // Must match bridge_scaled_mode in runtime/d3d9_bridge.cpp: the game only
    // accepts a size from the display mode list.
    private int[][] modes(int aspect) {
        int[] s = screen();
        int w = s[0], h = s[1];
        int an = ASPECT_V[aspect][0], ad = ASPECT_V[aspect][1];
        if (an == 0) { an = w; ad = h; }
        int top = (int) Math.min(h, (long) w * ad / an);
        java.util.ArrayList<int[]> l = new java.util.ArrayList<>();
        l.add(new int[]{(int) ((long) top * an / ad) & ~1, top});
        for (int mh : MODE_HEIGHTS)
            if (mh < top) l.add(new int[]{(int) ((long) mh * an / ad) & ~1, mh});
        return l.toArray(new int[0][]);
    }

    private static String[] modeNames(int[][] m) {
        String[] r = new String[m.length];
        for (int i = 0; i < m.length; i++) r[i] = m[i][0] + " x " + m[i][1];
        return r;
    }

    private int defaultMode(int[][] m) {
        for (int i = 0; i < m.length; i++) if (m[i][1] <= 540) return i;
        return m.length - 1;
    }
    private static final String[] AA = {"Off", "2x", "4x"};
    private static final int[] AA_V = {0, 2, 4};          // iMultiSample
    private static final String[] TEXQ = {"Small", "Medium", "Large"};
    private static final int[] TEXQ_V = {2, 1, 0};        // iTexMipMapSkip
    private static final String[] SHADOW = {"Low", "Medium", "High"};
    private static final int[] SHADOW_V = {512, 1024, 2048}; // iShadowMapResolution
    private static final String[] FADE = {"Short", "Medium", "Far"};
    private static final String[][] FADE_V = {{"2.0", "1.5"}, {"5.0", "3.0"}, {"10.0", "6.0"}};  // objects/items, actors

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("launcher", MODE_PRIVATE);
        buildUi();
        refreshFolder();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshFolder();
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private TextView text(LinearLayout parent, String s, int sp, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(Color.rgb(230, 225, 210));
        if (bold) t.setTypeface(null, android.graphics.Typeface.BOLD);
        t.setPadding(0, dp(6), 0, dp(2));
        parent.addView(t);
        return t;
    }

    private Spinner spinner(LinearLayout parent, String key, String label, String[] items, int def) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        TextView t = new TextView(this);
        t.setText(label);
        t.setTextSize(16);
        t.setTextColor(Color.rgb(230, 225, 210));
        row.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Spinner s = new Spinner(this);
        ArrayAdapter<String> a = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, items);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        s.setAdapter(a);
        s.setSelection(Math.min(prefs.getInt(key, def), items.length - 1));
        row.addView(s, new LinearLayout.LayoutParams(dp(180), ViewGroup.LayoutParams.WRAP_CONTENT));
        parent.addView(row);
        spinners.put(key, s);
        return s;
    }

    private CheckBox check(LinearLayout parent, String key, String label, boolean def) {
        CheckBox c = new CheckBox(this);
        c.setText(label);
        c.setTextSize(16);
        c.setTextColor(Color.rgb(230, 225, 210));
        c.setChecked(prefs.getBoolean(key, def));
        parent.addView(c);
        checks.put(key, c);
        return c;
    }

    private void buildUi() {
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.rgb(24, 22, 20));
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(16), dp(24), dp(24));
        sv.addView(root);

        text(root, "Oblivion", 28, true);
        text(root, "Developed by Toby Tomkinson.", 14, false);
        text(root, "Only from github.com/ttom795/Oblivion_Android. A copy from any other source is unauthorized.", 14, false);

        text(root, "Game folder", 20, true);

        folderText = text(root, "", 14, false);
        folderStatus = text(root, "", 14, false);
        Button pick = new Button(this);
        pick.setText("Choose Oblivion folder");
        pick.setOnClickListener(v -> {
            /* the game reads the folder with plain file calls: ask for file
             * access first (only shown once), then pick */
            if (hasStorage()) openPicker();
            else { pendingPick = true; requestStorage(); }
        });
        root.addView(pick);

        text(root, "Options", 20, true);
        Spinner aspect = spinner(root, "aspect", "Aspect ratio", ASPECTS, 0);
        int[][] m = modes(aspect.getSelectedItemPosition());
        Spinner res = spinner(root, "res", "Resolution", modeNames(m), defaultMode(m));
        aspect.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            boolean first = true;
            @Override public void onItemSelected(AdapterView<?> p, View v, int pos, long id) {
                if (first) { first = false; return; }
                // keep the same height where the new list has it
                int oldH = modes(prefs.getInt("aspectShown", 0))[Math.max(0, res.getSelectedItemPosition())][1];
                int[][] nm = modes(pos);
                ArrayAdapter<String> a = new ArrayAdapter<>(LauncherActivity.this,
                        android.R.layout.simple_spinner_item, modeNames(nm));
                a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
                res.setAdapter(a);
                int pick = defaultMode(nm);
                for (int i = 0; i < nm.length; i++) if (nm[i][1] == oldH) pick = i;
                res.setSelection(pick);
                prefs.edit().putInt("aspectShown", pos).apply();
            }
            @Override public void onNothingSelected(AdapterView<?> p) {}
        });
        prefs.edit().putInt("aspectShown", aspect.getSelectedItemPosition()).apply();
        spinner(root, "aa", "Antialiasing", AA, 0);
        spinner(root, "texq", "Texture size", TEXQ, 1);
        spinner(root, "shadow", "Shadow map size", SHADOW, 0);
        spinner(root, "fade", "Object fade distance", FADE, 0);
        check(root, "shadows", "Actor shadows", false);
        check(root, "grass", "Grass", false);
        check(root, "water", "Water reflections", false);
        check(root, "bloom", "Bloom", false);
        check(root, "hdr", "HDR (slow)", false);
        check(root, "distant", "Distant land and buildings", true);
        check(root, "skipvid", "Skip startup videos (recommended until Bink is confirmed working)", true);
        check(root, "vsync", "Vertical sync", true);
        check(root, "touch", "Touch controls (disabled in this build: use a controller)", false);
        check(root, "nintendo", "Nintendo button layout (A/B, X/Y swapped)", false);

        playButton = new Button(this);
        playButton.setText("Play");
        playButton.setTextSize(22);
        playButton.setOnClickListener(v -> play());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(64));
        lp.topMargin = dp(16);
        root.addView(playButton, lp);
        setContentView(sv);
    }

    /* Storage: the app targets API 29 with legacy storage, so the normal
     * "allow access to files" prompt gives the game plain file access. Only
     * if this Android ignores legacy storage and the folder still can't be
     * read is all-files access asked for (once, automatically). */
    private boolean hasStorage() {
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                || (Build.VERSION.SDK_INT >= 30 && Environment.isExternalStorageManager());
    }

    private void requestStorage() {
        requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQ_PERM);
    }

    @Override
    public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
        super.onRequestPermissionsResult(req, perms, res);
        if (req != REQ_PERM) return;
        boolean ok = hasStorage();
        if (ok && pendingPick) openPicker();
        if (ok && pendingPlay) play();
        pendingPick = pendingPlay = false;
        refreshFolder();
    }

    /* folder exists but its files can't be read: legacy storage was refused */
    private boolean unreadable(List<String> bad) {
        for (String b : bad) if (b.startsWith("cannot read")) return true;
        return false;
    }

    private boolean hasAllFiles() {
        return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
    }

    private void requestAllFiles() {
        try {
            Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            startActivity(i);
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    private void openPicker() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_FOLDER);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_FOLDER || res != RESULT_OK || data == null || data.getData() == null) return;
        String path = treeToPath(data.getData());
        if (path == null) {
            folderStatus.setText("That location is not a plain folder on this device; choose one on internal storage or the SD card.");
            return;
        }
        prefs.edit().putString("folder", path).apply();
        refreshFolder();
        if (!askedAllFiles && !hasAllFiles() && unreadable(checkFolder(path))) {
            askedAllFiles = true;
            requestAllFiles();
        }
    }

    /* content://...tree/primary:Games/Oblivion -> /storage/emulated/0/Games/Oblivion */
    private static String treeToPath(Uri tree) {
        String id;
        try { id = DocumentsContract.getTreeDocumentId(tree); } catch (Exception e) { return null; }
        int c = id.indexOf(':');
        if (c < 0) return null;
        String vol = id.substring(0, c), rel = id.substring(c + 1);
        String base = vol.equalsIgnoreCase("primary") ? Environment.getExternalStorageDirectory().getPath()
                : "/storage/" + vol;
        return rel.isEmpty() ? base : base + "/" + rel;
    }

    private static File findCi(File dir, String name) {
        File f = new File(dir, name);
        if (f.exists()) return f;
        String[] l = dir.list();
        if (l != null) for (String s : l) if (s.equalsIgnoreCase(name)) return new File(dir, s);
        return null;
    }

    // SHA-1 of the Steam Oblivion.exe (1.2.0.416) the port was recompiled from:
    // other builds have different code at different addresses and would crash.
    private static final String SHA_OBLIVION_STEAM = "42b5268a3dc938683c3fe9ea6bdac99744ac6121";
    // the same exe with the Steam wrapper removed (tools/unpack_oblivion.py)
    private static final String SHA_OBLIVION_UNPACKED = "0881ee045b332bdab266ecccdd1f2279b89be41d";

    private void checkSha(File f, String want, String name, List<String> bad) {
        String got = sha1(f);
        if (got == null) bad.add("cannot read " + name);
        else if (!got.equals(want)) bad.add(name + " is the wrong version (SHA-1 " + got.substring(0, 8)
                + ", expected " + want.substring(0, 8) + ")");
    }

    /* SHA-1 as hex, cached by path, size and modification time; null if unreadable. */
    private String sha1(File f) {
        String key = "sha1:" + f.getAbsolutePath(), stamp = f.length() + ":" + f.lastModified();
        String cached = prefs.getString(key, null);
        if (cached != null && cached.startsWith(stamp + "=")) return cached.substring(stamp.length() + 1);
        try (java.io.InputStream in = new java.io.FileInputStream(f)) {
            java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-1");
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) md.update(buf, 0, n);
            StringBuilder sb = new StringBuilder();
            for (byte b : md.digest()) sb.append(String.format("%02x", b));
            String hex = sb.toString();
            prefs.edit().putString(key, stamp + "=" + hex).apply();
            return hex;
        } catch (Exception e) {
            return null;
        }
    }

    /* Returns the list of problems with the folder (empty when it is ready). */
    private List<String> checkFolder(String path) {
        List<String> bad = new ArrayList<>();
        if (path == null) { bad.add("no folder chosen"); return bad; }
        File d = new File(path);
        if (!d.isDirectory()) { bad.add("folder not found"); return bad; }
        File data = findCi(d, "Data");
        File esm = data != null ? findCi(data, "Oblivion.esm") : null;
        if (data == null) bad.add("Data folder missing");
        else if (esm == null) bad.add(data.canRead() ? "Data/Oblivion.esm missing" : "cannot read the Data folder");
        else if (!esm.canRead()) bad.add("cannot read Data/Oblivion.esm");
        File exe = findCi(d, "Oblivion.exe");
        if (exe == null) bad.add("Oblivion.exe missing");
        else {
            String got = sha1(exe);
            if (got == null) bad.add("cannot read Oblivion.exe");
            else if (!got.equals(SHA_OBLIVION_STEAM) && !got.equals(SHA_OBLIVION_UNPACKED))
                bad.add("Oblivion.exe is not 1.2.0.416 as tested (SHA-1 " + got.substring(0, 8) + ")");
        }
        for (String n : new String[] {"d3dx9_27.dll", "binkw32.dll"})
            if (findCi(d, n) == null) bad.add(n + " missing");
        return bad;
    }

    private void refreshFolder() {
        String path = prefs.getString("folder", null);
        folderText.setText(path == null ? "No folder chosen yet. Copy your Oblivion folder (with d3dx9_27.dll) "
                + "anywhere outside Android/data, e.g. Internal storage/Oblivion, then choose it here." : path);
        List<String> bad = checkFolder(path);
        folderStatus.setText(bad.isEmpty() ? "Ready" : "Not ready: " + String.join(", ", bad));
        folderStatus.setTextColor(bad.isEmpty() ? Color.rgb(140, 200, 120) : Color.rgb(230, 150, 110));
        playButton.setEnabled(bad.isEmpty());
    }

    private void saveChoices() {
        SharedPreferences.Editor e = prefs.edit();
        for (Map.Entry<String, Spinner> s : spinners.entrySet()) e.putInt(s.getKey(), s.getValue().getSelectedItemPosition());
        for (Map.Entry<String, CheckBox> c : checks.entrySet()) e.putBoolean(c.getKey(), c.getValue().isChecked());
        e.apply();
    }

    private int sel(String k) { return spinners.get(k).getSelectedItemPosition(); }

    private void play() {
        if (!hasStorage()) { pendingPlay = true; requestStorage(); return; }
        saveChoices();
        String folder = prefs.getString("folder", null);
        File save = new File(folder, "port/save");
        File iniDir = new File(save, "Documents/My Games/Oblivion");
        iniDir.mkdirs();
        try {
            writePrefs(new File(folder), new File(iniDir, "Oblivion.ini"));
            // the native side builds the same mode list from this aspect (see modes())
            int[] av = ASPECT_V[sel("aspect")];
            try (Writer w = new OutputStreamWriter(new FileOutputStream(new File(save, "display.cfg")))) {
                w.write(av[0] + " " + av[1] + "\n");
            }
            try (Writer w = new OutputStreamWriter(new FileOutputStream(new File(save, "touch.cfg")))) {
                w.write((checks.get("touch").isChecked() ? "1" : "0") + " " + (checks.get("nintendo").isChecked() ? "1" : "0") + "\n");
            }
        } catch (Exception ex) {
            folderStatus.setText("Could not write the settings: " + ex);
            return;
        }
        Intent i = new Intent(this, OblivionActivity.class);
        i.putExtra("game", folder);
        startActivity(i);
        finish();
    }

    /* Oblivion.ini: keep what is there, then apply the options. Key names are the
     * ones the exe registers (see runtime/skyrim_shims.c, oblivion_seed_prefs). */
    private void writePrefs(File game, File out) throws Exception {
        LinkedHashMap<String, LinkedHashMap<String, String>> ini = new LinkedHashMap<>();
        readIni(out, ini);
        int[] mode = modes(sel("aspect"))[sel("res")];
        put(ini, "Display", "iSize W", Integer.toString(mode[0]));
        put(ini, "Display", "iSize H", Integer.toString(mode[1]));
        put(ini, "Display", "bFull Screen", "1");
        put(ini, "Display", "iMultiSample", Integer.toString(AA_V[sel("aa")]));
        put(ini, "Display", "iTexMipMapSkip", Integer.toString(TEXQ_V[sel("texq")]));
        put(ini, "Display", "iShadowMapResolution", Integer.toString(SHADOW_V[sel("shadow")]));
        put(ini, "Display", "iPresentInterval", checks.get("vsync").isChecked() ? "1" : "0");
        String sh = checks.get("shadows").isChecked() ? "1" : "0";
        put(ini, "Display", "bDoActorShadows", sh);
        put(ini, "Display", "bShadowsOnGrass", "0");
        put(ini, "Display", "bDoCanopyShadowPass", "0");
        put(ini, "Display", "bDoTallGrassEffect", checks.get("grass").isChecked() ? "1" : "0");
        put(ini, "Grass", "bDrawShaderGrass", checks.get("grass").isChecked() ? "1" : "0");
        String wr = checks.get("water").isChecked() ? "1" : "0";
        put(ini, "Water", "bUseWaterReflections", wr);
        put(ini, "Water", "bUseWaterHiRes", "0");
        put(ini, "BlurShader", "bUseBlurShader", checks.get("bloom").isChecked() ? "1" : "0");
        put(ini, "BlurShaderHDR", "bDoHighDynamicRange", checks.get("hdr").isChecked() ? "1" : "0");
        String dl = checks.get("distant").isChecked() ? "1" : "0";
        put(ini, "LOD", "bDisplayLODLand", dl);
        put(ini, "LOD", "bDisplayLODBuildings", dl);
        put(ini, "LOD", "bDisplayLODTrees", dl);
        String[] f = FADE_V[sel("fade")];
        put(ini, "LOD", "fLODFadeOutMultObjects", f[0]);
        put(ini, "LOD", "fLODFadeOutMultItems", f[0]);
        put(ini, "LOD", "fLODFadeOutMultActors", f[1]);
        put(ini, "General", "bUseMyGamesDirectory", "1");
        put(ini, "Controls", "bBackground Keyboard", "0");
        put(ini, "Controls", "bBackground Mouse", "0");
        if (checks.get("skipvid").isChecked()) {
            put(ini, "General", "sIntroSequence", "");
            put(ini, "General", "sMainMenuMovieIntro", "");
            put(ini, "General", "sMainMenuMovie", "");
            put(ini, "General", "sOblivionIntro", "");
        }
        try (Writer wr2 = new OutputStreamWriter(new FileOutputStream(out))) {
            for (Map.Entry<String, LinkedHashMap<String, String>> s2 : ini.entrySet()) {
                wr2.write("[" + s2.getKey() + "]\r\n");
                for (Map.Entry<String, String> kv : s2.getValue().entrySet()) wr2.write(kv.getKey() + "=" + kv.getValue() + "\r\n");
                wr2.write("\r\n");
            }
        }
    }

    private static void put(LinkedHashMap<String, LinkedHashMap<String, String>> ini, String sec, String k, String v) {
        String sk = sec;
        for (String s : ini.keySet()) if (s.equalsIgnoreCase(sec)) sk = s;
        LinkedHashMap<String, String> m = ini.get(sk);
        if (m == null) ini.put(sk, m = new LinkedHashMap<>());
        String kk = k;
        for (String s : m.keySet()) if (s.equalsIgnoreCase(k)) kk = s;
        m.put(kk, v);
    }

    private static void readIni(File f, LinkedHashMap<String, LinkedHashMap<String, String>> ini) {
        if (!f.exists()) return;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f)))) {
            String sec = "General", line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith(";") || line.startsWith("#")) continue;
                if (line.startsWith("[")) {
                    int e = line.indexOf(']');
                    sec = (e > 0 ? line.substring(1, e) : line.substring(1)).trim();
                    continue;
                }
                int eq = line.indexOf('=');
                if (eq > 0) put(ini, sec, line.substring(0, eq).trim(), line.substring(eq + 1).trim());
            }
        } catch (Exception ignored) {
        }
    }
}
