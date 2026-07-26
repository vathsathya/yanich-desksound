package com.yanich.desksound

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.view.GestureDetector
import android.view.MotionEvent
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.google.android.material.slider.Slider
import com.yanich.desksound.databinding.ActivityMainBinding
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.*

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    private var audioService: AudioReceiverService? = null
    private var isBound = false

    private var smoothRms = 0.0f

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as AudioReceiverService.LocalBinder
            audioService = binder.getService().apply {
                onStatusChangedListener = { state, msg ->
                    updateUiState(state, msg)
                }
                onAudioLevelListener = { rms ->
                    updateVisualizer(rms)
                }
                onChannelModeListener = { modeText ->
                    binding.tvChannelMode.text = modeText
                }
                // Sync current state
                if (isStreaming) {
                    updateUiState(AudioReceiverService.State.STREAMING, "Streaming active")
                } else if (isConnecting) {
                    updateUiState(AudioReceiverService.State.CONNECTING, "Connecting...")
                } else {
                    updateUiState(AudioReceiverService.State.DISCONNECTED, null)
                    autoScanAndConnectOnStartup()
                }
                binding.sliderVolume.value = volume
                updateVolumeLabel(volume)
                updateChannelModeButtons(overrideMode)
                updateLatencyButtons(bufferLatencyMultiplier)
            }
            isBound = true
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            audioService = null
            isBound = false
        }
    }

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted: Boolean ->
        if (!isGranted) {
            Toast.makeText(this, "Notification permission required for background audio playback", Toast.LENGTH_LONG).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        loadSavedPrefs()

        binding.btnConnect.setOnClickListener {
            val service = audioService ?: return@setOnClickListener

            if (service.isStreaming || service.isConnecting) {
                service.stopStreaming()
            } else {
                val ip = binding.etServerIp.text.toString().trim()
                val portStr = binding.etServerPort.text.toString().trim()

                if (ip.isEmpty()) {
                    binding.etServerIp.error = "Enter Server IP Address"
                    return@setOnClickListener
                }

                val port = portStr.toIntOrNull() ?: 5000
                savePrefs(ip, port)

                checkNotificationPermission()
                service.startStreaming(ip, port)
            }
        }

        binding.btnScanServer.setOnClickListener {
            performServerScan()
        }

        // Volume Slider & Quick Presets
        binding.sliderVolume.addOnChangeListener { _: Slider, value: Float, fromUser: Boolean ->
            if (fromUser) {
                audioService?.volume = value
                updateVolumeLabel(value)
            }
        }

        val gestureDetector = GestureDetector(this, object : GestureDetector.SimpleOnGestureListener() {
            override fun onDoubleTap(e: MotionEvent): Boolean {
                binding.sliderVolume.value = 1.0f
                audioService?.volume = 1.0f
                updateVolumeLabel(1.0f)
                Toast.makeText(this@MainActivity, "Volume Reset to 100%", Toast.LENGTH_SHORT).show()
                return true
            }
        })
        binding.sliderVolume.setOnTouchListener { _, event -> gestureDetector.onTouchEvent(event) }

        binding.btnVolMute.setOnClickListener {
            binding.sliderVolume.value = 0.0f
            audioService?.volume = 0.0f
            updateVolumeLabel(0.0f)
        }
        binding.btnVol50.setOnClickListener {
            binding.sliderVolume.value = 0.5f
            audioService?.volume = 0.5f
            updateVolumeLabel(0.5f)
        }
        binding.btnVol100.setOnClickListener {
            binding.sliderVolume.value = 1.0f
            audioService?.volume = 1.0f
            updateVolumeLabel(1.0f)
        }

        // Channel Mode Selectors
        binding.btnChannelAuto.setOnClickListener { setChannelMode(AudioReceiverService.OverrideMode.AUTO) }
        binding.btnChannelLeft.setOnClickListener { setChannelMode(AudioReceiverService.OverrideMode.FORCE_LEFT) }
        binding.btnChannelRight.setOnClickListener { setChannelMode(AudioReceiverService.OverrideMode.FORCE_RIGHT) }

        // Latency Mode Selectors
        binding.btnLatencyLow.setOnClickListener { setLatencyMode(1) }
        binding.btnLatencyBal.setOnClickListener { setLatencyMode(2) }
        binding.btnLatencyHigh.setOnClickListener { setLatencyMode(4) }

        updateNetworkAndAudioRouteInfo()
    }

    private fun updateVolumeLabel(value: Float) {
        val percent = (value * 100).toInt()
        binding.tvVolumeVal.text = "$percent%"
    }

    private fun setChannelMode(mode: AudioReceiverService.OverrideMode) {
        val service = audioService ?: return
        service.overrideMode = mode
        updateChannelModeButtons(mode)
    }

    private fun updateChannelModeButtons(mode: AudioReceiverService.OverrideMode) {
        val activeColor = ContextCompat.getColor(this, R.color.accent_cyan)
        val inactiveColor = ContextCompat.getColor(this, R.color.text_secondary)
        val activeStroke = ContextCompat.getColor(this, R.color.accent_cyan)
        val inactiveStroke = android.graphics.Color.parseColor("#2A3042")

        binding.btnChannelAuto.setTextColor(if (mode == AudioReceiverService.OverrideMode.AUTO) activeColor else inactiveColor)
        binding.btnChannelAuto.strokeColor = android.content.res.ColorStateList.valueOf(if (mode == AudioReceiverService.OverrideMode.AUTO) activeStroke else inactiveStroke)

        binding.btnChannelLeft.setTextColor(if (mode == AudioReceiverService.OverrideMode.FORCE_LEFT) activeColor else inactiveColor)
        binding.btnChannelLeft.strokeColor = android.content.res.ColorStateList.valueOf(if (mode == AudioReceiverService.OverrideMode.FORCE_LEFT) activeStroke else inactiveStroke)

        binding.btnChannelRight.setTextColor(if (mode == AudioReceiverService.OverrideMode.FORCE_RIGHT) activeColor else inactiveColor)
        binding.btnChannelRight.strokeColor = android.content.res.ColorStateList.valueOf(if (mode == AudioReceiverService.OverrideMode.FORCE_RIGHT) activeStroke else inactiveStroke)
    }

    private fun setLatencyMode(multiplier: Int) {
        val service = audioService ?: return
        service.bufferLatencyMultiplier = multiplier
        updateLatencyButtons(multiplier)
    }

    private fun updateLatencyButtons(multiplier: Int) {
        val activeColor = ContextCompat.getColor(this, R.color.accent_cyan)
        val inactiveColor = ContextCompat.getColor(this, R.color.text_secondary)
        val activeStroke = ContextCompat.getColor(this, R.color.accent_cyan)
        val inactiveStroke = android.graphics.Color.parseColor("#2A3042")

        binding.btnLatencyLow.setTextColor(if (multiplier == 1) activeColor else inactiveColor)
        binding.btnLatencyLow.strokeColor = android.content.res.ColorStateList.valueOf(if (multiplier == 1) activeStroke else inactiveStroke)

        binding.btnLatencyBal.setTextColor(if (multiplier == 2) activeColor else inactiveColor)
        binding.btnLatencyBal.strokeColor = android.content.res.ColorStateList.valueOf(if (multiplier == 2) activeStroke else inactiveStroke)

        binding.btnLatencyHigh.setTextColor(if (multiplier == 4) activeColor else inactiveColor)
        binding.btnLatencyHigh.strokeColor = android.content.res.ColorStateList.valueOf(if (multiplier == 4) activeStroke else inactiveStroke)
    }

    private fun updateNetworkAndAudioRouteInfo() {
        try {
            val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
            val activeNetwork = cm.activeNetwork
            val caps = cm.getNetworkCapabilities(activeNetwork)

            val localIp = getLocalWifiIpAddress() ?: ""

            if (localIp.startsWith("192.168.42.") || localIp.startsWith("192.168.49.") || caps?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_ETHERNET) == true) {
                binding.tvNetworkMode.text = "🚀 USB Tethering (~3ms Latency)"
                binding.tvNetworkMode.setTextColor(ContextCompat.getColor(this, R.color.status_connected))
            } else if (caps?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) == true) {
                binding.tvNetworkMode.text = "📶 Wi-Fi Network ($localIp)"
                binding.tvNetworkMode.setTextColor(ContextCompat.getColor(this, R.color.accent_cyan))
            } else {
                binding.tvNetworkMode.text = "Network Active ($localIp)"
            }

            val audioManager = getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager
            val devices = audioManager.getDevices(android.media.AudioManager.GET_DEVICES_OUTPUTS)
            var hasWired = false
            var hasBt = false

            for (dev in devices) {
                when (dev.type) {
                    android.media.AudioDeviceInfo.TYPE_WIRED_HEADSET,
                    android.media.AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
                    android.media.AudioDeviceInfo.TYPE_USB_HEADSET,
                    android.media.AudioDeviceInfo.TYPE_USB_DEVICE -> hasWired = true
                    android.media.AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
                    android.media.AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> hasBt = true
                }
            }

            if (hasWired) {
                binding.tvHeadphoneRoute.text = "Audio Route: 🎧 Wired Headphones (0-Lag Direct)"
                binding.tvHeadphoneRoute.setTextColor(ContextCompat.getColor(this, R.color.status_connected))
            } else if (hasBt) {
                binding.tvHeadphoneRoute.text = "Audio Route: 📶 Bluetooth Audio (+150ms Delay)"
                binding.tvHeadphoneRoute.setTextColor(ContextCompat.getColor(this, R.color.status_connecting))
            } else {
                binding.tvHeadphoneRoute.text = "Audio Route: 🔊 Phone Speaker Output"
                binding.tvHeadphoneRoute.setTextColor(ContextCompat.getColor(this, R.color.text_secondary))
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    private fun autoScanAndConnectOnStartup() {
        val service = audioService ?: return
        val savedIp = binding.etServerIp.text.toString().trim()
        val portStr = binding.etServerPort.text.toString().trim()
        val port = portStr.toIntOrNull() ?: 5000

        lifecycleScope.launch(Dispatchers.IO) {
            // 1. Fast probe on saved IP (<100ms)
            if (savedIp.isNotEmpty()) {
                try {
                    val s = java.net.Socket()
                    s.connect(java.net.InetSocketAddress(savedIp, port), 200)
                    s.close()
                    withContext(Dispatchers.Main) {
                        checkNotificationPermission()
                        service.startStreaming(savedIp, port)
                    }
                    return@launch
                } catch (e: Exception) {
                    // Fallback to auto-scan
                }
            }

            // 2. Auto-Scan local network
            performServerScan(autoConnect = true)
        }
    }

    private fun performServerScan(autoConnect: Boolean = false) {
        binding.btnScanServer.isEnabled = false
        binding.btnScanServer.text = "SCANNING..."

        lifecycleScope.launch(Dispatchers.IO) {
            var foundIp: String? = null

            // Phase 1: Try UDP Discovery Broadcast (Port 5001)
            try {
                val socket = java.net.DatagramSocket()
                socket.soTimeout = 1500
                socket.broadcast = true

                val msg = "DESKSOUND_DISCOVER".toByteArray()
                val broadcastAddr = java.net.InetAddress.getByName("255.255.255.255")
                val packet = java.net.DatagramPacket(msg, msg.size, broadcastAddr, 5001)
                socket.send(packet)

                val recvBuf = ByteArray(256)
                val recvPacket = java.net.DatagramPacket(recvBuf, recvBuf.size)
                socket.receive(recvPacket)

                val response = String(recvPacket.data, 0, recvPacket.length)
                if (response.startsWith("DESKSOUND_SERVER")) {
                    foundIp = recvPacket.address.hostAddress
                }
                socket.close()
            } catch (e: Exception) {
                // UDP broadcast timeout fallback to TCP subnet scan
            }

            // Phase 2: Parallel Subnet TCP Scan Fallback (Port 5000)
            if (foundIp == null) {
                val localIp = getLocalWifiIpAddress()
                if (localIp != null && localIp.contains(".")) {
                    val prefix = localIp.substringBeforeLast(".") + "."
                    coroutineScope {
                        val deferreds = (1..254).map { i ->
                            async(Dispatchers.IO) {
                                val testIp = "$prefix$i"
                                try {
                                    val s = java.net.Socket()
                                    s.connect(java.net.InetSocketAddress(testIp, 5000), 200)
                                    s.close()
                                    testIp
                                } catch (e: Exception) {
                                    null
                                }
                            }
                        }
                        val results = deferreds.awaitAll()
                        foundIp = results.firstOrNull { resIp -> resIp != null }
                    }
                }
            }

            withContext(Dispatchers.Main) {
                binding.btnScanServer.isEnabled = true
                binding.btnScanServer.text = "🔍 SCAN"

                val targetIp = foundIp
                if (targetIp != null) {
                    binding.etServerIp.setText(targetIp)
                    savePrefs(targetIp, 5000)
                    if (autoConnect) {
                        checkNotificationPermission()
                        audioService?.startStreaming(targetIp, 5000)
                    } else {
                        Toast.makeText(this@MainActivity, "Found Server at $targetIp!", Toast.LENGTH_LONG).show()
                    }
                } else if (!autoConnect) {
                    Toast.makeText(this@MainActivity, "No DeskSound Server found on local network.", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun getLocalWifiIpAddress(): String? {
        try {
            val interfaces = java.net.NetworkInterface.getNetworkInterfaces()
            while (interfaces.hasMoreElements()) {
                val networkInterface = interfaces.nextElement()
                val addresses = networkInterface.inetAddresses
                while (addresses.hasMoreElements()) {
                    val inetAddress = addresses.nextElement()
                    if (!inetAddress.isLoopbackAddress && inetAddress is java.net.Inet4Address) {
                        return inetAddress.hostAddress
                    }
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return null
    }

    override fun onStart() {
        super.onStart()
        val intent = Intent(this, AudioReceiverService::class.java)
        bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    override fun onStop() {
        super.onStop()
        if (isBound) {
            audioService?.onStatusChangedListener = null
            audioService?.onAudioLevelListener = null
            unbindService(serviceConnection)
            isBound = false
        }
    }

    private fun updateUiState(state: AudioReceiverService.State, message: String?) {
        when (state) {
            AudioReceiverService.State.DISCONNECTED -> {
                binding.statusDot.setBackgroundColor(ContextCompat.getColor(this, R.color.status_disconnected))
                binding.tvStatus.text = getString(R.string.status_disconnected)
                binding.btnConnect.text = getString(R.string.btn_connect)
                binding.btnConnect.isEnabled = true
                binding.btnConnect.setBackgroundColor(ContextCompat.getColor(this, R.color.accent_cyan))
                binding.pbAudioLevel.progress = 0
            }
            AudioReceiverService.State.CONNECTING -> {
                binding.statusDot.setBackgroundColor(ContextCompat.getColor(this, R.color.status_connecting))
                binding.tvStatus.text = message ?: getString(R.string.status_connecting)
                binding.btnConnect.text = "CANCEL"
                binding.btnConnect.isEnabled = true
                binding.btnConnect.setBackgroundColor(ContextCompat.getColor(this, R.color.status_connecting))
            }
            AudioReceiverService.State.STREAMING -> {
                binding.statusDot.setBackgroundColor(ContextCompat.getColor(this, R.color.status_connected))
                binding.tvStatus.text = getString(R.string.status_streaming)
                binding.btnConnect.text = getString(R.string.btn_disconnect)
                binding.btnConnect.isEnabled = true
                binding.btnConnect.setBackgroundColor(ContextCompat.getColor(this, R.color.status_disconnected))
            }
            AudioReceiverService.State.ERROR -> {
                binding.statusDot.setBackgroundColor(ContextCompat.getColor(this, R.color.status_disconnected))
                binding.tvStatus.text = message ?: "Error connecting"
                binding.btnConnect.text = getString(R.string.btn_connect)
                binding.btnConnect.isEnabled = true
                binding.btnConnect.setBackgroundColor(ContextCompat.getColor(this, R.color.accent_cyan))
                binding.pbAudioLevel.progress = 0
            }
        }
    }

    private fun updateVisualizer(rms: Float) {
        val targetVal = (rms * 250).coerceIn(0f, 100f)
        smoothRms = if (targetVal > smoothRms) targetVal else (smoothRms * 0.8f + targetVal * 0.2f)
        binding.pbAudioLevel.progress = smoothRms.toInt()
    }

    private fun checkNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                requestPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }

    private fun loadSavedPrefs() {
        val prefs = getSharedPreferences("desksound_prefs", MODE_PRIVATE)
        binding.etServerIp.setText(prefs.getString("ip", "192.168.1.100"))
        binding.etServerPort.setText(prefs.getInt("port", 5000).toString())
    }

    private fun savePrefs(ip: String, port: Int) {
        val prefs = getSharedPreferences("desksound_prefs", MODE_PRIVATE)
        prefs.edit().putString("ip", ip).putInt("port", port).apply()
    }
}
