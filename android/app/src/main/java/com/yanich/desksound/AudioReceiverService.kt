package com.yanich.desksound

import android.app.*
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.*
import java.io.InputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder

import android.net.wifi.WifiManager
import android.os.PowerManager

class AudioReceiverService : Service() {

    private val binder = LocalBinder()
    private var serviceScope = CoroutineScope(Dispatchers.IO + Job())

    private var socket: Socket? = null
    private var audioTrack: AudioTrack? = null

    private var wakeLock: PowerManager.WakeLock? = null
    private var wifiLock: WifiManager.WifiLock? = null

    private var audioManager: android.media.AudioManager? = null
    private val audioFocusChangeListener = android.media.AudioManager.OnAudioFocusChangeListener { focusChange ->
        when (focusChange) {
            android.media.AudioManager.AUDIOFOCUS_LOSS,
            android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                audioTrack?.setVolume(0.0f)
            }
            android.media.AudioManager.AUDIOFOCUS_GAIN -> {
                audioTrack?.setVolume(this@AudioReceiverService.volume)
            }
        }
    }

    @Volatile var isStreaming = false
        private set

    @Volatile var isConnecting = false
        private set

    // SPEAKER PROTECTION: Strict Volume Clamp Max 1.0f (100% Limit)
    var volume: Float = 1.0f
        set(value) {
            val clamped = value.coerceIn(0.0f, 1.0f)
            field = clamped
            audioTrack?.setVolume(clamped)
        }

    // Buffer Latency Multiplier (1 = ~10ms, 2 = ~30ms, 4 = ~100ms)
    var bufferLatencyMultiplier: Int = 2

    enum class OverrideMode { AUTO, FORCE_LEFT, FORCE_RIGHT }
    var overrideMode: OverrideMode = OverrideMode.AUTO

    var onStatusChangedListener: ((State, String?) -> Unit)? = null
    var onAudioLevelListener: ((Float) -> Unit)? = null
    var onChannelModeListener: ((String) -> Unit)? = null

    enum class State {
        DISCONNECTED,
        CONNECTING,
        STREAMING,
        ERROR
    }

    inner class LocalBinder : Binder() {
        fun getService(): AudioReceiverService = this@AudioReceiverService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val action = intent?.action
        if (action == ACTION_START) {
            val ip = intent.getStringExtra(EXTRA_IP) ?: "192.168.1.100"
            val port = intent.getIntExtra(EXTRA_PORT, 5000)
            startForeground(NOTIFICATION_ID, createNotification("Connecting to $ip:$port..."))
            connectAndStream(ip, port)
        } else if (action == ACTION_STOP) {
            stopStreaming()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
        return START_NOT_STICKY
    }

    fun startStreaming(ip: String, port: Int) {
        val intent = Intent(this, AudioReceiverService::class.java).apply {
            action = ACTION_START
            putExtra(EXTRA_IP, ip)
            putExtra(EXTRA_PORT, port)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent)
        } else {
            startService(intent)
        }
    }

    fun stopStreaming() {
        isStreaming = false
        isConnecting = false
        serviceScope.coroutineContext.cancelChildren()
        releaseLocks()
        
        try {
            socket?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing socket", e)
        }
        socket = null

        try {
            audioTrack?.stop()
            audioTrack?.release()
        } catch (e: Exception) {
            Log.e(TAG, "Error stopping AudioTrack", e)
        }
        audioTrack = null

        notifyStatus(State.DISCONNECTED, null)
    }

    private fun acquireLocks() {
        try {
            if (wakeLock == null) {
                val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
                wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "DeskSound:AudioWakeLock")
                wakeLock?.acquire(10 * 60 * 60 * 1000L)
            }
            if (wifiLock == null) {
                val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
                wifiLock = wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "DeskSound:WifiLock")
                wifiLock?.acquire()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error acquiring locks", e)
        }
    }

    private fun releaseLocks() {
        try {
            if (wakeLock?.isHeld == true) {
                wakeLock?.release()
            }
            wakeLock = null

            if (wifiLock?.isHeld == true) {
                wifiLock?.release()
            }
            wifiLock = null
        } catch (e: Exception) {
            Log.e(TAG, "Error releasing locks", e)
        }
    }

    private fun connectAndStream(ip: String, port: Int) {
        if (isStreaming || isConnecting) return

        isConnecting = true
        notifyStatus(State.CONNECTING, "Connecting to $ip:$port...")
        acquireLocks()

        serviceScope.launch {
            var retryCount = 0
            val maxRetries = 9999 // Persistent Auto-Reconnection Guard

            while (retryCount < maxRetries && (isConnecting || isStreaming)) {
                try {
                    Log.d(TAG, "Connecting to server $ip:$port (Attempt ${retryCount + 1})...")
                    val sock = Socket()
                    sock.connect(InetSocketAddress(ip, port), 5000)
                    sock.tcpNoDelay = true
                    socket = sock

                    isConnecting = false
                    isStreaming = true
                    notifyStatus(State.STREAMING, "Connected to $ip:$port")
                    updateNotification("Streaming desktop audio from $ip:$port")

                    initAudioTrack()
                    readAudioLoop(sock.getInputStream())
                    break

                } catch (e: Exception) {
                    Log.e(TAG, "Connection attempt failed", e)
                    socket?.close()
                    socket = null

                    if (isStreaming || isConnecting) {
                        retryCount++
                        if (retryCount < maxRetries) {
                            notifyStatus(State.CONNECTING, "Auto-Reconnecting... ($retryCount)")
                            delay(1500L)
                        } else {
                            isConnecting = false
                            isStreaming = false
                            notifyStatus(State.ERROR, "Connection lost to $ip:$port")
                            stopStreaming()
                        }
                    }
                }
            }
        }
    }

    private fun initAudioTrack(sampleRate: Int = 48000, channels: Int = 2, isFloat: Boolean = true) {
        val channelConfig = if (channels == 1) AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO
        val audioFormat = if (isFloat) AudioFormat.ENCODING_PCM_FLOAT else AudioFormat.ENCODING_PCM_16BIT

        val minBufferSize = AudioTrack.getMinBufferSize(sampleRate, channelConfig, audioFormat)
        val bufferSize = minBufferSize * bufferLatencyMultiplier

        val audioAttributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
            .build()

        val format = AudioFormat.Builder()
            .setSampleRate(sampleRate)
            .setChannelMask(channelConfig)
            .setEncoding(audioFormat)
            .build()

        val trackBuilder = AudioTrack.Builder()
            .setAudioAttributes(audioAttributes)
            .setAudioFormat(format)
            .setBufferSizeInBytes(bufferSize)
            .setTransferMode(AudioTrack.MODE_STREAM)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            trackBuilder.setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
        }

        audioTrack?.stop()
        audioTrack?.release()

        audioTrack = trackBuilder.build().apply {
            setVolume(this@AudioReceiverService.volume)
            play()
        }
    }

    private suspend fun readAudioLoop(inputStream: InputStream) = withContext(Dispatchers.IO) {
        // Read 32-byte format negotiation header from server
        var sampleRate = 48000
        var channels = 2
        var isFloat = true

        val headerBuf = ByteArray(32)
        var headerBytes = 0
        while (headerBytes < 32 && isStreaming && isActive) {
            val r = inputStream.read(headerBuf, headerBytes, 32 - headerBytes)
            if (r == -1) break
            headerBytes += r
        }

        if (headerBytes == 32) {
            val headerStr = String(headerBuf).trim()
            if (headerStr.startsWith("FORMAT|")) {
                val parts = headerStr.split("|")
                if (parts.size >= 4) {
                    sampleRate = parts[1].toIntOrNull() ?: 48000
                    channels = parts[2].toIntOrNull() ?: 2
                    val bits = parts[3].trim().toIntOrNull() ?: 32
                    isFloat = (bits == 32)
                    val modeStr = if (parts.size >= 5) parts[4].trim() else "STEREO"
                    val channelDisplay = when (modeStr) {
                        "LEFT" -> "🎧 Channel Mode: Left Channel Only (L)"
                        "RIGHT" -> "🎧 Channel Mode: Right Channel Only (R)"
                        else -> "🎧 Channel Mode: Stereo (L + R)"
                    }
                    serviceScope.launch(Dispatchers.Main) {
                        onChannelModeListener?.invoke(channelDisplay)
                    }
                    Log.d(TAG, "Negotiated format: $sampleRate Hz, $channels ch, mode: $modeStr")
                }
            }
        }

        initAudioTrack(sampleRate, channels, isFloat)

        val tagBuffer = ByteArray(4)
        val pcmBuffer = ByteArray(4096)
        val floatBuffer = FloatArray(1024)

        val tagBb = ByteBuffer.wrap(tagBuffer).order(ByteOrder.BIG_ENDIAN)
        val pcmBb = ByteBuffer.wrap(pcmBuffer).order(ByteOrder.LITTLE_ENDIAN)

        var lastLevelReportTime = 0L
        var currentServerTag = -1

        while (isStreaming && isActive) {
            // 1. Read 4-byte big-endian modeTag header
            var tagRead = 0
            while (tagRead < 4 && isStreaming && isActive) {
                val r = inputStream.read(tagBuffer, tagRead, 4 - tagRead)
                if (r == -1) throw Exception("Server closed socket stream")
                tagRead += r
            }

            tagBb.rewind()
            val modeTag = tagBb.int

            if (modeTag != currentServerTag) {
                currentServerTag = modeTag
                val channelDisplay = when (modeTag) {
                    1 -> "🎧 Channel Mode: Left Channel Only (L)"
                    2 -> "🎧 Channel Mode: Right Channel Only (R)"
                    else -> "🎧 Channel Mode: Stereo (L + R)"
                }
                serviceScope.launch(Dispatchers.Main) {
                    onChannelModeListener?.invoke(channelDisplay)
                }
            }

            // 2. Read 4096 bytes of Float PCM Audio payload
            var pcmRead = 0
            while (pcmRead < 4096 && isStreaming && isActive) {
                val r = inputStream.read(pcmBuffer, pcmRead, 4096 - pcmRead)
                if (r == -1) throw Exception("Server closed socket stream")
                pcmRead += r
            }

            if (pcmRead > 0) {
                pcmBb.rewind()
                val floatCount = pcmRead / 4
                val frameCount = floatCount / 2

                val curOverride = overrideMode
                for (i in 0 until frameCount) {
                    var sampleL = pcmBb.float
                    var sampleR = pcmBb.float

                    when (curOverride) {
                        OverrideMode.FORCE_LEFT -> { sampleR = sampleL }
                        OverrideMode.FORCE_RIGHT -> { sampleL = sampleR }
                        OverrideMode.AUTO -> { /* follow server tag */ }
                    }

                    // SPEAKER PROTECTION HARD LIMITER
                    if (sampleL > 1.0f) sampleL = 1.0f else if (sampleL < -1.0f) sampleL = -1.0f
                    if (sampleR > 1.0f) sampleR = 1.0f else if (sampleR < -1.0f) sampleR = -1.0f

                    floatBuffer[i * 2 + 0] = sampleL
                    floatBuffer[i * 2 + 1] = sampleR
                }

                // Play back audio frame block
                audioTrack?.write(floatBuffer, 0, floatCount, AudioTrack.WRITE_BLOCKING)

                // Measure live audio level for VU meter
                val now = System.currentTimeMillis()
                if (now - lastLevelReportTime > 50) {
                    lastLevelReportTime = now
                    var sumSquare = 0.0f
                    for (i in 0 until floatCount) {
                        sumSquare += floatBuffer[i] * floatBuffer[i]
                    }
                    val rms = Math.sqrt((sumSquare / floatCount).toDouble()).toFloat()
                    val normLevel = (rms * 3.0f).coerceIn(0.0f, 1.0f)
                    serviceScope.launch(Dispatchers.Main) {
                        onAudioLevelListener?.invoke(normLevel)
                    }
                }
            }
        }
    }

    private fun notifyStatus(state: State, message: String?) {
        serviceScope.launch(Dispatchers.Main) {
            onStatusChangedListener?.invoke(state, message)
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                getString(R.string.notification_channel_name),
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "DeskSound background audio player"
            }
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(channel)
        }
    }

    private fun createNotification(contentText: String): Notification {
        val intent = Intent(this, MainActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(contentText)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    private fun updateNotification(contentText: String) {
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.notify(NOTIFICATION_ID, createNotification(contentText))
    }

    override fun onDestroy() {
        stopStreaming()
        serviceScope.cancel()
        super.onDestroy()
    }

    companion object {
        private const val TAG = "AudioReceiverService"
        const val CHANNEL_ID = "desksound_audio_channel"
        const val NOTIFICATION_ID = 1001

        const val ACTION_START = "com.yanich.desksound.action.START"
        const val ACTION_STOP = "com.yanich.desksound.action.STOP"
        const val EXTRA_IP = "extra_ip"
        const val EXTRA_PORT = "extra_port"
    }
}
