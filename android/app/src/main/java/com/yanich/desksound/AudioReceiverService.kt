package com.yanich.desksound

import android.app.*
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.*
import java.io.InputStream
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder

import android.net.ConnectivityManager
import android.net.wifi.WifiManager
import android.os.PowerManager

class AudioReceiverService : Service() {

    private val binder = LocalBinder()
    private var serviceScope = CoroutineScope(Dispatchers.IO + Job())

    private var socket: Socket? = null
    private var audioTrack: AudioTrack? = null

    private var wakeLock: PowerManager.WakeLock? = null
    private var wifiLock: WifiManager.WifiLock? = null

    private var audioManager: AudioManager? = null
    private val audioFocusChangeListener = AudioManager.OnAudioFocusChangeListener { focusChange ->
        when (focusChange) {
            AudioManager.AUDIOFOCUS_LOSS,
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                audioTrack?.setVolume(0.0f)
            }
            AudioManager.AUDIOFOCUS_GAIN -> {
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
            try {
                audioTrack?.setVolume(clamped)
            } catch (e: Exception) {
                Log.e(TAG, "Error setting volume", e)
            }
        }

    // Buffer Latency Multiplier (1 = ~10ms, 2 = ~30ms, 4 = ~100ms)
    var bufferLatencyMultiplier: Int = 2
        set(value) {
            field = value
            if (isStreaming) {
                // Re-initialize AudioTrack with new buffer size
                try {
                    initAudioTrack()
                } catch (e: Exception) {
                    Log.e(TAG, "Error updating buffer latency", e)
                }
            }
        }

    enum class OverrideMode { AUTO, FORCE_LEFT, FORCE_RIGHT }
    var overrideMode: OverrideMode = OverrideMode.AUTO

    var currentChannelModeText: String = "Stereo (L+R)"

    var onStatusChangedListener: ((State, String?) -> Unit)? = null
    var onAudioLevelListener: ((Float) -> Unit)? = null
    var onChannelModeListener: ((String) -> Unit)? = null
        set(value) {
            field = value
            value?.invoke(currentChannelModeText)
        }

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
        audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val action = intent?.action
        if (action == ACTION_START) {
            val ip = intent.getStringExtra(EXTRA_IP) ?: "192.168.1.100"
            val port = intent.getIntExtra(EXTRA_PORT, 5000)
            
            val notification = createNotification("Connecting to $ip:$port...")
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(NOTIFICATION_ID, notification, android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
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
        abandonAudioFocus()
        
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

    private fun requestAudioFocus() {
        try {
            audioManager?.requestAudioFocus(
                audioFocusChangeListener,
                AudioManager.STREAM_MUSIC,
                AudioManager.AUDIOFOCUS_GAIN
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error requesting Audio Focus", e)
        }
    }

    private fun abandonAudioFocus() {
        try {
            audioManager?.abandonAudioFocus(audioFocusChangeListener)
        } catch (e: Exception) {
            Log.e(TAG, "Error abandoning Audio Focus", e)
        }
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
                val mode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                } else {
                    @Suppress("DEPRECATION")
                    WifiManager.WIFI_MODE_FULL_HIGH_PERF
                }
                wifiLock = wifiManager.createWifiLock(mode, "DeskSound:WifiLock").apply {
                    setReferenceCounted(false)
                }
                try {
                    wifiLock?.acquire()
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to acquire WifiLock", e)
                }
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

    private fun getUsbCandidates(inputIp: String): List<String> {
        val candidates = mutableListOf<String>()

        if (inputIp.isNotEmpty() && inputIp != "USB_AUTO" && inputIp != "127.0.0.1") {
            candidates.add(inputIp)
        }

        // 1. ADB Reverse Port Forwarding Loopback (Primary for USB ADB)
        if (!candidates.contains("127.0.0.1")) candidates.add("127.0.0.1")

        // 2. Dynamic scan of all network interfaces for USB/RNDIS/NCM gateways and subnets
        try {
            val interfaces = NetworkInterface.getNetworkInterfaces()
            while (interfaces.hasMoreElements()) {
                val iface = interfaces.nextElement()
                val addrs = iface.inetAddresses
                while (addrs.hasMoreElements()) {
                    val addr = addrs.nextElement()
                    if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                        val host = addr.hostAddress ?: continue
                        if (host.contains(".")) {
                            val prefix = host.substringBeforeLast(".") + "."
                            val gate1 = prefix + "1"
                            val gate129 = prefix + "129"
                            val gate254 = prefix + "254"
                            if (!candidates.contains(gate1)) candidates.add(gate1)
                            if (!candidates.contains(gate129)) candidates.add(gate129)
                            if (!candidates.contains(gate254)) candidates.add(gate254)
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error scanning local interface candidates", e)
        }

        // 3. Known Standard Android RNDIS & USB Tethering Host IPs fallback
        val standardIps = listOf("192.168.42.129", "192.168.42.1", "192.168.49.1", "192.168.43.1", "192.168.137.1")
        for (sip in standardIps) {
            if (!candidates.contains(sip)) candidates.add(sip)
        }

        return candidates
    }

    private fun connectAndStream(ip: String, port: Int) {
        if (isStreaming || isConnecting) return

        isConnecting = true
        notifyStatus(State.CONNECTING, "Connecting USB Mode ($ip:$port)...")
        acquireLocks()
        requestAudioFocus()

        serviceScope.launch {
            var retryCount = 0
            val maxRetries = 9999 // Persistent Auto-Reconnection Guard

            while (retryCount < maxRetries && (isConnecting || isStreaming)) {
                val targetIps = if (ip == "USB_AUTO" || ip == "127.0.0.1" || ip.startsWith("192.168.")) {
                    getUsbCandidates(ip)
                } else {
                    listOf(ip)
                }

                var connectedSock: Socket? = null
                var connectedIp: String? = null

                val channel = kotlinx.coroutines.channels.Channel<Pair<String, Socket>>(kotlinx.coroutines.channels.Channel.CONFLATED)
                val probeJobs = targetIps.map { targetIp ->
                    launch(Dispatchers.IO) {
                        try {
                            val sock = Socket()
                            sock.receiveBufferSize = 64 * 1024
                            sock.sendBufferSize = 64 * 1024
                            sock.trafficClass = 0x10 // IPTOS_LOWDELAY
                            sock.tcpNoDelay = true
                            sock.keepAlive = true
                            sock.connect(InetSocketAddress(targetIp, port), 800)
                            channel.trySend(Pair(targetIp, sock))
                        } catch (_: Exception) {}
                    }
                }

                try {
                    withTimeout(1500) {
                        val pair = channel.receive()
                        connectedIp = pair.first
                        connectedSock = pair.second
                    }
                } catch (_: Exception) {}

                probeJobs.forEach { it.cancel() }

                if (connectedSock != null && connectedIp != null) {
                    socket = connectedSock
                    isConnecting = false
                    isStreaming = true
                    notifyStatus(State.STREAMING, "Connected to $connectedIp:$port")
                    updateNotification("Streaming desktop audio from $connectedIp:$port")

                    initAudioTrack()
                    readAudioLoop(connectedSock!!.getInputStream())
                    break
                }

                if (isStreaming || isConnecting) {
                    retryCount++
                    if (retryCount < maxRetries) {
                        notifyStatus(State.CONNECTING, "Auto-Reconnecting USB... ($retryCount)")
                        delay(1500L)
                    } else {
                        isConnecting = false
                        isStreaming = false
                        notifyStatus(State.ERROR, "USB connection failed. Check USB Tethering / ADB.")
                        stopStreaming()
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
            .setFlags(AudioAttributes.FLAG_LOW_LATENCY)
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

        try {
            audioTrack?.stop()
            audioTrack?.release()
        } catch (e: Exception) {
            Log.e(TAG, "Exception releasing old AudioTrack", e)
        }

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
                        "LEFT" -> "Left Channel (L)"
                        "RIGHT" -> "Right Channel (R)"
                        else -> "Stereo (L+R)"
                    }
                    currentChannelModeText = channelDisplay
                    serviceScope.launch(Dispatchers.Main) {
                        onChannelModeListener?.invoke(channelDisplay)
                    }
                    Log.d(TAG, "Negotiated format: $sampleRate Hz, $channels ch, mode: $modeStr")
                }
            }
        }

        initAudioTrack(sampleRate, channels, isFloat = false)

        val headerBuffer = ByteArray(8)
        val headerBb = ByteBuffer.wrap(headerBuffer).order(ByteOrder.BIG_ENDIAN)

        var pcmBuffer = ByteArray(8192)
        var shortBuffer = ShortArray(2048)
        var floatBuffer = FloatArray(2048)

        var lastLevelReportTime = 0L
        var currentServerTag = -1

        var lastReadPacketTime = System.currentTimeMillis()
        var isFadingIn = false
        var fadeCounter = 0
        val FADE_FRAMES = 240 // 5ms smooth fade-in at 48kHz

        var dcPrevInL = 0.0f
        var dcPrevOutL = 0.0f
        var dcPrevInR = 0.0f
        var dcPrevOutR = 0.0f

        while (isStreaming && isActive) {
            // Check time gap between packet reads for video switches / pauses
            val nowRead = System.currentTimeMillis()
            if (nowRead - lastReadPacketTime > 150) {
                // Video switch gap detected! Flush stale AudioTrack buffer
                try {
                    audioTrack?.flush()
                } catch (_: Exception) {}
                isFadingIn = true
                fadeCounter = 0
            }
            lastReadPacketTime = nowRead

            // 1. Read 8-byte big-endian header: [4 bytes modeTag] [4 bytes audioLength]
            var hRead = 0
            while (hRead < 8 && isStreaming && isActive) {
                val r = inputStream.read(headerBuffer, hRead, 8 - hRead)
                if (r == -1) throw Exception("Server closed socket stream")
                hRead += r
            }

            headerBb.rewind()
            val modeTag = headerBb.int
            val audioLength = headerBb.int

            if (audioLength <= 0 || audioLength > 65536) {
                throw Exception("Invalid audio packet length: $audioLength")
            }

            if (modeTag != currentServerTag) {
                currentServerTag = modeTag
                val channelDisplay = when (modeTag) {
                    1 -> "Left Channel (L)"
                    2 -> "Right Channel (R)"
                    else -> "Stereo (L+R)"
                }
                currentChannelModeText = channelDisplay
                serviceScope.launch(Dispatchers.Main) {
                    onChannelModeListener?.invoke(channelDisplay)
                }
            }

            // 2. Read exactly audioLength bytes of Float PCM Audio payload
            if (pcmBuffer.size < audioLength) {
                pcmBuffer = ByteArray(audioLength)
            }

            var pcmRead = 0
            while (pcmRead < audioLength && isStreaming && isActive) {
                val r = inputStream.read(pcmBuffer, pcmRead, audioLength - pcmRead)
                if (r == -1) throw Exception("Server closed socket stream")
                pcmRead += r
            }

            if (pcmRead > 0) {
                val floatCount = pcmRead / 4
                val frameCount = floatCount / 2
                if (shortBuffer.size < floatCount) {
                    shortBuffer = ShortArray(floatCount)
                    floatBuffer = FloatArray(floatCount)
                }

                val pcmBb = ByteBuffer.wrap(pcmBuffer, 0, pcmRead).order(ByteOrder.LITTLE_ENDIAN)
                val curOverride = overrideMode
                for (i in 0 until frameCount) {
                    var sampleL = pcmBb.float
                    var sampleR = pcmBb.float

                    when (curOverride) {
                        OverrideMode.FORCE_LEFT -> { sampleR = sampleL }
                        OverrideMode.FORCE_RIGHT -> { sampleL = sampleR }
                        OverrideMode.AUTO -> { /* follow server tag */ }
                    }

                    // 1. DC-Blocker Filter (Eliminates DC offset pops/clicks)
                    val outL = sampleL - dcPrevInL + 0.995f * dcPrevOutL
                    dcPrevInL = sampleL
                    dcPrevOutL = outL
                    sampleL = outL

                    val outR = sampleR - dcPrevInR + 0.995f * dcPrevOutR
                    dcPrevInR = sampleR
                    dcPrevOutR = outR
                    sampleR = outR

                    // 2. Noise Gate Guard (Completely silences background hiss/hum when silent)
                    if (Math.abs(sampleL) < 0.0003f) sampleL = 0.0f
                    if (Math.abs(sampleR) < 0.0003f) sampleR = 0.0f

                    // 3. Smooth Anti-Pop Fade-In on Stream Resume / Video Switch
                    if (isFadingIn) {
                        val fadeFactor = fadeCounter.toFloat() / FADE_FRAMES
                        sampleL *= fadeFactor
                        sampleR *= fadeFactor
                        fadeCounter++
                        if (fadeCounter >= FADE_FRAMES) {
                            isFadingIn = false
                        }
                    }

                    // 4. Soft-Knee Saturation Limiter Guard
                    sampleL = (Math.tanh(sampleL.toDouble()).toFloat()) * 0.85f
                    sampleR = (Math.tanh(sampleR.toDouble()).toFloat()) * 0.85f

                    // 5. Convert 32-bit Float (-1.0f..1.0f) to 16-bit Signed Integer (-32768..32767)
                    // Eliminates Android HAL Float underflow ground hum completely!
                    val shortL = (sampleL.coerceIn(-1.0f, 1.0f) * 32767.0f).toInt().toShort()
                    val shortR = (sampleR.coerceIn(-1.0f, 1.0f) * 32767.0f).toInt().toShort()

                    shortBuffer[i * 2 + 0] = shortL
                    shortBuffer[i * 2 + 1] = shortR
                    floatBuffer[i * 2 + 0] = sampleL
                    floatBuffer[i * 2 + 1] = sampleR
                }

                // Play back 16-bit Integer PCM frame block (100% compatible with all Android DACs)
                audioTrack?.write(shortBuffer, 0, floatCount, AudioTrack.WRITE_BLOCKING)

                // Measure live audio level for VU meter
                val now = System.currentTimeMillis()
                if (now - lastLevelReportTime > 40) {
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
        isServiceStreaming = (state == State.STREAMING)
        serviceScope.launch(Dispatchers.Main) {
            onStatusChangedListener?.invoke(state, message)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                try {
                    android.service.quicksettings.TileService.requestListeningState(
                        applicationContext,
                        android.content.ComponentName(applicationContext, DeskSoundTileService::class.java)
                    )
                } catch (e: Exception) {
                    Log.e(TAG, "Error requesting QS tile state update", e)
                }
            }
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

        val stopIntent = Intent(this, AudioReceiverService::class.java).apply {
            action = ACTION_STOP
        }
        val stopPendingIntent = PendingIntent.getService(
            this, 1, stopIntent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(contentText)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(pendingIntent)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, "DISCONNECT", stopPendingIntent)
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

        @Volatile var isServiceStreaming = false
    }
}
