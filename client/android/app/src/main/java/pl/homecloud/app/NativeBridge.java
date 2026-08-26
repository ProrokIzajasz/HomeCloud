package pl.homecloud.app;

public final class NativeBridge {
    static { System.loadLibrary("homecloud_native"); }
    private NativeBridge() {}
    public static native String baseUrl();
}
