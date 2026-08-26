package pl.homecloud.app;

import android.webkit.JavascriptInterface;

final class NativeUploadBridge {
    interface Host {
        void chooseFilesForUpload(String destination, boolean folder, String token);
    }

    private final Host host;

    NativeUploadBridge(Host host) {
        this.host = host;
    }

    @JavascriptInterface
    public boolean supportsBackgroundUploads() {
        return true;
    }

    @JavascriptInterface
    public void pickFiles(String destination, boolean folder, String token) {
        host.chooseFilesForUpload(destination, folder, token);
    }
}
