package org.remoterig.client;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

/**
 * Service de premier plan.
 *
 * Sans lui, Android suspend l'application des que l'ecran s'eteint et l'audio
 * se coupe en pleine liaison. Le service tient en plus deux verrous :
 *   - WifiLock en mode haute performance, sinon l'economie d'energie du Wi-Fi
 *     provoque des trous dans le flux ;
 *   - WakeLock partiel, pour que le processeur continue a traiter l'audio.
 */
public class RemoteRigService extends Service {

    private static final String CHANNEL_ID = "remoterig_link";
    private static final int NOTIFICATION_ID = 1;

    private WifiManager.WifiLock wifiLock;
    private PowerManager.WakeLock wakeLock;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    // WIFI_MODE_FULL_HIGH_PERF est deprecie depuis Android 10 mais reste le
    // seul moyen d'empecher l'economie d'energie du Wi-Fi de trouer le flux.
    @SuppressWarnings("deprecation")
    @Override
    public void onCreate() {
        super.onCreate();
        createChannel();

        WifiManager wifi = (WifiManager) getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
        if (wifi != null) {
            wifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF,
                                           "RemoteRig:wifi");
            wifiLock.setReferenceCounted(false);
            wifiLock.acquire();
        }

        PowerManager power = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (power != null) {
            wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "RemoteRig:audio");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire();
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Notification notification = buildNotification();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            // Android 14 impose de declarer le type de service.
            startForeground(NOTIFICATION_ID, notification,
                            ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }

        // Si le systeme nous tue faute de memoire, il nous relance.
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        super.onDestroy();
    }

    private void createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "RemoteRig", NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("Liaison avec la station distante");
        channel.setShowBadge(false);
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) manager.createNotificationChannel(channel);
    }

    // Notification.Builder(Context) n'est utilise que sur les versions
    // anterieures a Android 8, ou le constructeur a canal n'existe pas.
    @SuppressWarnings("deprecation")
    private Notification buildNotification() {
        Intent open = new Intent(this, org.qtproject.qt.android.bindings.QtActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);

        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent pending = PendingIntent.getActivity(this, 0, open, flags);

        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }

        return builder
                .setContentTitle("RemoteRig")
                .setContentText("Liaison audio active")
                .setSmallIcon(android.R.drawable.ic_lock_silent_mode_off)
                .setContentIntent(pending)
                .setOngoing(true)
                .build();
    }
}
