package pl.homecloud.app;

import android.Manifest;
import android.app.Activity;
import android.app.DownloadManager;
import android.annotation.SuppressLint;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.graphics.Insets;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.content.pm.PackageManager;
import android.view.WindowInsets;
import android.window.OnBackInvokedDispatcher;
import android.webkit.SafeBrowsingResponse;
import android.webkit.ValueCallback;
import android.webkit.URLUtil;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import java.util.ArrayList;

@SuppressWarnings("deprecation")
public final class MainActivity extends Activity implements NativeUploadBridge.Host {
    private static final int PICK_FILE = 7001;
    private static final int PICK_BACKGROUND_FILES = 7002;
    private static final int PICK_BACKGROUND_FOLDER = 7003;
    private WebView cloudView;
    private ValueCallback<Uri[]> pendingFiles;
    private String uploadDestination = ".";
    private String uploadToken = "";

    @SuppressLint("SetJavaScriptEnabled")
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        cloudView = new WebView(this);
        setContentView(cloudView);
        cloudView.setOnApplyWindowInsetsListener((view, windowInsets) -> {
            int extraTop = Math.round(12 * getResources().getDisplayMetrics().density);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                Insets bars = windowInsets.getInsets(WindowInsets.Type.systemBars());
                view.setPadding(0, bars.top + extraTop, 0, bars.bottom);
            } else {
                view.setPadding(0, windowInsets.getSystemWindowInsetTop() + extraTop, 0,
                                windowInsets.getSystemWindowInsetBottom());
            }
            return windowInsets;
        });
        cloudView.setBackgroundColor(0xff2f2016);
        cloudView.getSettings().setJavaScriptEnabled(true);
        cloudView.getSettings().setDomStorageEnabled(true);
        cloudView.getSettings().setAllowFileAccess(false);
        cloudView.getSettings().setAllowContentAccess(true);
        cloudView.getSettings().setBuiltInZoomControls(false);
        cloudView.addJavascriptInterface(new NativeUploadBridge(this), "HomeCloudNative");
        cloudView.setWebViewClient(new HomeCloudWebClient());
        cloudView.setWebChromeClient(new HomeCloudChromeClient());
        cloudView.setDownloadListener((url, userAgent, disposition, mimeType, length) -> {
            String filename = URLUtil.guessFileName(url, disposition, mimeType);
            DownloadManager.Request request = new DownloadManager.Request(Uri.parse(url));
            request.setTitle(filename);
            request.setDescription("Pobieranie z HomeCloud");
            request.setMimeType(mimeType);
            request.setNotificationVisibility(
                DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            request.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, filename);
            ((DownloadManager)getSystemService(DOWNLOAD_SERVICE)).enqueue(request);
            Toast.makeText(this, "Pobieranie rozpoczęte.", Toast.LENGTH_SHORT).show();
        });
        cloudView.loadUrl(NativeBridge.baseUrl());
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
                PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 7004);
        }
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.P &&
            checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) !=
                PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, 7005);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::navigateBack);
    }

    @SuppressLint("GestureBackNavigation")
    @Override public void onBackPressed() {
        navigateBack();
    }

    private void navigateBack() {
        cloudView.evaluateJavascript(
            "window.homeCloudBack ? String(window.homeCloudBack()) : 'false'",
            handled -> {
                if (!"\"true\"".equals(handled) && !"true".equals(handled)) finish();
            });
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PICK_BACKGROUND_FILES || requestCode == PICK_BACKGROUND_FOLDER) {
            handleBackgroundSelection(requestCode, resultCode, data);
            return;
        }
        if (requestCode != PICK_FILE || pendingFiles == null) return;
        Uri[] result = null;
        if (resultCode == RESULT_OK && data != null) {
            if (data.getClipData() != null) {
                result = new Uri[data.getClipData().getItemCount()];
                for (int i = 0; i < result.length; ++i)
                    result[i] = data.getClipData().getItemAt(i).getUri();
            } else if (data.getData() != null) result = new Uri[]{data.getData()};
        }
        pendingFiles.onReceiveValue(result);
        pendingFiles = null;
    }

    @Override
    public void chooseFilesForUpload(String destination, boolean folder, String token) {
        runOnUiThread(() -> {
            uploadDestination = destination == null || destination.isBlank() ? "." : destination;
            uploadToken = token == null ? "" : token;
            Intent picker = new Intent(folder ? Intent.ACTION_OPEN_DOCUMENT_TREE
                                              : Intent.ACTION_OPEN_DOCUMENT);
            picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                            Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            if (!folder) {
                picker.addCategory(Intent.CATEGORY_OPENABLE);
                picker.setType("*/*");
                picker.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
            }
            startActivityForResult(picker,
                folder ? PICK_BACKGROUND_FOLDER : PICK_BACKGROUND_FILES);
        });
    }

    private void handleBackgroundSelection(int requestCode, int resultCode, Intent data) {
        if (resultCode != RESULT_OK || data == null || uploadToken.isBlank()) return;
        ArrayList<String> uris = new ArrayList<>();
        ArrayList<String> paths = new ArrayList<>();
        try {
            if (requestCode == PICK_BACKGROUND_FOLDER && data.getData() != null) {
                Uri tree = data.getData();
                retainReadPermission(tree, data.getFlags());
                String rootId = DocumentsContract.getTreeDocumentId(tree);
                collectTreeFiles(tree, rootId, "", uris, paths);
            } else if (data.getClipData() != null) {
                for (int i = 0; i < data.getClipData().getItemCount(); ++i) {
                    Uri uri = data.getClipData().getItemAt(i).getUri();
                    retainReadPermission(uri, data.getFlags());
                    uris.add(uri.toString());
                    paths.add(joinUploadPath(uploadDestination, displayName(uri)));
                }
            } else if (data.getData() != null) {
                Uri uri = data.getData();
                retainReadPermission(uri, data.getFlags());
                uris.add(uri.toString());
                paths.add(joinUploadPath(uploadDestination, displayName(uri)));
            }
        } catch (Exception error) {
            Toast.makeText(this, "Nie można odczytać wybranych plików.", Toast.LENGTH_LONG).show();
            return;
        }
        if (uris.isEmpty()) return;

        Intent service = new Intent(this, UploadService.class);
        service.putStringArrayListExtra(UploadService.EXTRA_URIS, uris);
        service.putStringArrayListExtra(UploadService.EXTRA_PATHS, paths);
        service.putExtra(UploadService.EXTRA_TOKEN, uploadToken);
        startForegroundService(service);
        Toast.makeText(this, "Wysyłanie działa teraz w tle.", Toast.LENGTH_SHORT).show();
    }

    private void collectTreeFiles(Uri tree, String parentId, String relative,
                                  ArrayList<String> uris, ArrayList<String> paths) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        };
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) return;
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                String childPath = relative.isEmpty() ? name : relative + "/" + name;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    collectTreeFiles(tree, id, childPath, uris, paths);
                } else {
                    Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                    uris.add(document.toString());
                    paths.add(joinUploadPath(uploadDestination, childPath));
                }
            }
        }
    }

    private String displayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(
                 uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return cursor.getString(0);
        }
        return uri.getLastPathSegment() == null ? "plik" : uri.getLastPathSegment();
    }

    private void retainReadPermission(Uri uri, int resultFlags) {
        int flags = resultFlags & (Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                   Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try { getContentResolver().takePersistableUriPermission(uri, flags); }
        catch (SecurityException ignored) { }
    }

    private static String joinUploadPath(String base, String child) {
        return ".".equals(base) ? child : base + "/" + child;
    }

    private final class HomeCloudWebClient extends WebViewClient {
        @Override public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            Uri target = request.getUrl();
            Uri home = Uri.parse(NativeBridge.baseUrl());
            return !"https".equals(target.getScheme()) || !home.getHost().equals(target.getHost());
        }

        @Override public void onReceivedError(WebView view, WebResourceRequest request,
                                               WebResourceError error) {
            if (request.isForMainFrame()) Toast.makeText(MainActivity.this,
                getString(R.string.offline_message), Toast.LENGTH_LONG).show();
        }

        @Override public void onSafeBrowsingHit(WebView view, WebResourceRequest request,
                                                 int threatType, SafeBrowsingResponse callback) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1)
                callback.backToSafety(true);
        }
    }

    private final class HomeCloudChromeClient extends WebChromeClient {
        @Override public boolean onShowFileChooser(WebView view, ValueCallback<Uri[]> callback,
                                                    FileChooserParams params) {
            if (pendingFiles != null) pendingFiles.onReceiveValue(null);
            pendingFiles = callback;
            Intent picker = params.createIntent();
            picker.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
            try { startActivityForResult(picker, PICK_FILE); }
            catch (RuntimeException error) {
                pendingFiles.onReceiveValue(null);
                pendingFiles = null;
                Toast.makeText(MainActivity.this, "Nie można otworzyć wyboru plików.",
                               Toast.LENGTH_LONG).show();
            }
            return true;
        }
    }
}
