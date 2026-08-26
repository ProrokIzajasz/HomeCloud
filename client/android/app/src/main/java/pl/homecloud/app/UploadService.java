package pl.homecloud.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.net.Uri;
import android.os.IBinder;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

public final class UploadService extends Service {
    static final String EXTRA_URIS = "upload_uris";
    static final String EXTRA_PATHS = "upload_paths";
    static final String EXTRA_TOKEN = "upload_token";

    private static final String CHANNEL_ID = "homecloud_uploads";
    private static final int NOTIFICATION_ID = 41;
    private static final int CHUNK_SIZE = 4 * 1024 * 1024;
    private final ExecutorService coordinator = Executors.newSingleThreadExecutor();

    @Override
    public void onCreate() {
        super.onCreate();
        NotificationChannel channel = new NotificationChannel(
            CHANNEL_ID, "Wysyłanie do HomeCloud", NotificationManager.IMPORTANCE_LOW);
        getSystemService(NotificationManager.class).createNotificationChannel(channel);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        ArrayList<String> uris = intent.getStringArrayListExtra(EXTRA_URIS);
        ArrayList<String> paths = intent.getStringArrayListExtra(EXTRA_PATHS);
        String token = intent.getStringExtra(EXTRA_TOKEN);
        if (uris == null || paths == null || token == null || uris.size() != paths.size()) {
            stopSelf(startId);
            return START_NOT_STICKY;
        }

        startForeground(NOTIFICATION_ID, notification("Przygotowywanie plików", 0, 0, true));
        coordinator.execute(() -> uploadAll(uris, paths, token, startId));
        return START_REDELIVER_INTENT;
    }

    private void uploadAll(ArrayList<String> uris, ArrayList<String> paths,
                           String token, int startId) {
        long totalBytes = 0;
        for (String value : uris) totalBytes += contentLength(Uri.parse(value));
        AtomicLong transferred = new AtomicLong();
        AtomicInteger completed = new AtomicInteger();
        AtomicInteger failed = new AtomicInteger();
        ExecutorService workers = Executors.newFixedThreadPool(Math.min(3, uris.size()));

        for (int index = 0; index < uris.size(); ++index) {
            final int item = index;
            final long allBytes = totalBytes;
            workers.execute(() -> {
                try {
                    upload(Uri.parse(uris.get(item)), paths.get(item), token,
                           bytes -> updateProgress(transferred.addAndGet(bytes), allBytes,
                                                   completed.get(), uris.size()));
                    int done = completed.incrementAndGet();
                    updateProgress(transferred.get(), allBytes, done, uris.size());
                } catch (Exception ignored) {
                    failed.incrementAndGet();
                }
            });
        }
        workers.shutdown();
        try {
            while (!workers.isTerminated()) Thread.sleep(200);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        }

        int sent = completed.get();
        String result = failed.get() == 0
            ? "Wysłano " + sent + " plików"
            : "Wysłano " + sent + " z " + uris.size() + " plików";
        getSystemService(NotificationManager.class).notify(
            NOTIFICATION_ID, notification(result, 0, 0, false));
        stopForeground(STOP_FOREGROUND_DETACH);
        stopSelf(startId);
    }

    private void upload(Uri uri, String relativePath, String token,
                        ProgressListener progress) throws Exception {
        long total = contentLength(uri);
        if (total <= 0) throw new IOException("Nie można odczytać rozmiaru pliku");
        String uploadId = UUID.randomUUID().toString().replace("-", "");
        long offset = requestOffset(uploadId, token);

        try (InputStream input = new BufferedInputStream(getContentResolver().openInputStream(uri))) {
            if (input == null) throw new IOException("Nie można otworzyć pliku");
            skipFully(input, offset);
            byte[] buffer = new byte[CHUNK_SIZE];
            while (offset < total) {
                int wanted = (int)Math.min(buffer.length, total - offset);
                int length = readFully(input, buffer, wanted);
                if (length <= 0) throw new IOException("Plik zakończył się za wcześnie");
                long before = offset;
                offset = sendChunk(uploadId, relativePath, token, total, before, buffer, length);
                progress.add(offset - before);
            }
        }
    }

    private long sendChunk(String uploadId, String path, String token, long total,
                           long offset, byte[] data, int length) throws Exception {
        Exception last = null;
        for (int attempt = 0; attempt < 4; ++attempt) {
            try {
                String query = "uploadId=" + encode(uploadId) + "&offset=" + offset +
                    "&total=" + total + "&relativePath=" + encode(path);
                HttpURLConnection connection = open("/api/v1/uploads/chunk?" + query, token);
                connection.setRequestMethod("POST");
                connection.setDoOutput(true);
                connection.setFixedLengthStreamingMode(length);
                connection.setRequestProperty("Content-Type", "application/octet-stream");
                try (OutputStream output = connection.getOutputStream()) {
                    output.write(data, 0, length);
                }
                int status = connection.getResponseCode();
                JSONObject body = responseJson(connection, status);
                if (status == 200 || status == 201) return body.getLong("offset");
                if (status == 409) return body.getLong("offset");
                throw new IOException("HTTP " + status);
            } catch (Exception error) {
                last = error;
                Thread.sleep(500L << attempt);
                long serverOffset = requestOffset(uploadId, token);
                if (serverOffset > offset) return serverOffset;
            }
        }
        throw last == null ? new IOException("Upload failed") : last;
    }

    private long requestOffset(String uploadId, String token) throws Exception {
        HttpURLConnection connection = open(
            "/api/v1/uploads/status?uploadId=" + encode(uploadId), token);
        int status = connection.getResponseCode();
        if (status != 200) throw new IOException("HTTP " + status);
        return responseJson(connection, status).getLong("offset");
    }

    private HttpURLConnection open(String path, String token) throws Exception {
        URI base = URI.create(NativeBridge.baseUrl());
        HttpURLConnection connection = (HttpURLConnection)base.resolve(path).toURL().openConnection();
        connection.setConnectTimeout(15_000);
        connection.setReadTimeout(45_000);
        connection.setRequestProperty("Authorization", "Bearer " + token);
        return connection;
    }

    private static JSONObject responseJson(HttpURLConnection connection, int status)
        throws Exception {
        InputStream stream = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
        if (stream == null) return new JSONObject();
        try (stream) {
            ByteArrayOutputStream body = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            int length;
            while ((length = stream.read(buffer)) >= 0) body.write(buffer, 0, length);
            return new JSONObject(body.toString(StandardCharsets.UTF_8.name()));
        }
    }

    private long contentLength(Uri uri) {
        try (android.database.Cursor cursor = getContentResolver().query(
                 uri, new String[]{android.provider.OpenableColumns.SIZE}, null, null, null)) {
            return cursor != null && cursor.moveToFirst() && !cursor.isNull(0) ? cursor.getLong(0) : 0;
        }
    }

    private void updateProgress(long done, long total, int completed, int count) {
        int percent = total == 0 ? 0 : (int)Math.min(100, done * 100 / total);
        String text = completed + "/" + count + " plików · " + percent + "%";
        getSystemService(NotificationManager.class).notify(
            NOTIFICATION_ID, notification(text, percent, 100, true));
    }

    private Notification notification(String text, int progress, int maximum, boolean ongoing) {
        Intent launch = new Intent(this, MainActivity.class);
        PendingIntent pending = PendingIntent.getActivity(
            this, 0, launch, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder builder = new Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_homecloud)
            .setContentTitle("HomeCloud")
            .setContentText(text)
            .setContentIntent(pending)
            .setOngoing(ongoing)
            .setOnlyAlertOnce(true);
        if (maximum > 0) builder.setProgress(maximum, progress, false);
        return builder.build();
    }

    private static int readFully(InputStream input, byte[] buffer, int wanted) throws IOException {
        int read = 0;
        while (read < wanted) {
            int part = input.read(buffer, read, wanted - read);
            if (part < 0) break;
            read += part;
        }
        return read;
    }

    private static void skipFully(InputStream input, long bytes) throws IOException {
        long skipped = 0;
        while (skipped < bytes) {
            long part = input.skip(bytes - skipped);
            if (part <= 0) {
                if (input.read() < 0) throw new IOException("Nie można wznowić pliku");
                part = 1;
            }
            skipped += part;
        }
    }

    private static String encode(String value) {
        try {
            return URLEncoder.encode(value, StandardCharsets.UTF_8.name());
        } catch (java.io.UnsupportedEncodingException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        coordinator.shutdownNow();
        super.onDestroy();
    }

    private interface ProgressListener {
        void add(long bytes);
    }
}
