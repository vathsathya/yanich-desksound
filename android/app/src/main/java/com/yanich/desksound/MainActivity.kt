package com.yanich.desksound

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.view.GestureDetector
import android.view.MotionEvent
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.google.android.material.slider.Slider
import com.yanich.desksound.databinding.ActivityMainBinding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.NetworkInterface

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var audioService: AudioReceiverService? = null
    private var isBound = false
    private var currentTab = AppTab.CONNECTION
    private var currentConnectionMode = ConnectionMode.WIFI

    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private var audioDeviceCallback: android.media.AudioDeviceCallback? = null

    enum class AppTab {
        CONNECTION, MONITOR
    }

    enum class ConnectionMode {
        WIFI, USB
    }

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as AudioReceiverService.LocalBinder
            audioService = binder.getService()
            isBound = true

            audioService?.onStatusChangedListener = { state, message ->
                runOnUiThread {
                    updateUiState(state, message)
                }
            }

            audioService?.onAudioLevelListener = { level ->
                runOnUiThread {
                    animateEqualizerSpectrum(level)
                }
            }

            val currentState = if (audioService?.isStreaming == true) AudioReceiverService.State.STREAMING else if (audioService?.isConnecting == true) AudioReceiverService.State.CONNECTING else AudioReceiverService.State.DISCONNECTED
            updateUiState(currentState, null)
            audioService?.overrideMode?.let { updateChannelModeButtons(it) }
            audioService?.volume?.let { vol ->
                binding.sliderVolume.value = vol
                updateVolumeLabel(vol)
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            audioService = null
            isBound = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        loadSavedPrefs()

        binding.btnTabConnection.setOnClickListener { switchTab(AppTab.CONNECTION) }
        binding.btnTabMonitor.setOnClickListener { switchTab(AppTab.MONITOR) }

        binding.btnModeWifi.setOnClickListener { switchConnectionMode(ConnectionMode.WIFI) }
        binding.btnModeUsb.setOnClickListener { switchConnectionMode(ConnectionMode.USB) }

        binding.btnConnect.setOnClickListener {
            val service = audioService ?: return@setOnClickListener

            if (service.isStreaming || service.isConnecting) {
                service.stopStreaming()
            } else {
                val ip = if (currentConnectionMode == ConnectionMode.USB) "USB_AUTO" else binding.etServerIp.text.toString().trim()
                val portStr = binding.etServerPort.text.toString().trim()

                if (ip.isEmpty() && currentConnectionMode == ConnectionMode.WIFI) {
                    binding.etServerIp.error = "Enter Server IP Address"
                    return@setOnClickListener
                }

                val port = portStr.toIntOrNull() ?: 5000
                if (currentConnectionMode == ConnectionMode.WIFI) {
                    savePrefs(ip, port)
                }

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

        // Channel Mode Selectors
        binding.btnChannelAuto.setOnClickListener { setChannelMode(AudioReceiverService.OverrideMode.AUTO) }
        binding.btnChannelLeft.setOnClickListener { setChannelMode(AudioReceiverService.OverrideMode.FORCE_LEFT) }
        binding.btnChannelRight.setOnClickListener { setChannelMode(AudioReceiverService.OverrideMode.FORCE_RIGHT) }

        updateNetworkAndAudioRouteInfo()

        val ver = try {
            packageManager.getPackageInfo(packageName, 0).versionName ?: "1.0.2"
        } catch (e: Exception) {
            "1.0.2"
        }
        binding.tvAppFooter.text = "DeskSound Mobile v$ver • Built by @vathsathya"
    }

    private fun animateEqualizerSpectrum(rmsLevel: Float) {
        val maxBarPx = dpToPx(110f)
        val minBarPx = dpToPx(16f)

        val multipliers = listOf(0.45f, 0.75f, 1.15f, 1.45f, 1.05f, 0.65f, 0.35f)
        val bars = listOf(
            binding.barEq1, binding.barEq2, binding.barEq3,
            binding.barEq4, binding.barEq5, binding.barEq6, binding.barEq7
        )

        for (i in bars.indices) {
            val factor = multipliers[i]
            val calcHeight = ((rmsLevel * factor) * maxBarPx).toInt().coerceIn(minBarPx, maxBarPx)
            val lp = bars[i].layoutParams
            lp.height = calcHeight
            bars[i].layoutParams = lp
        }
    }

    private fun resetEqualizerSpectrumToBaseline() {
        val defaultHeightsDp = listOf(24f, 48f, 76f, 104f, 64f, 40f, 20f)
        val bars = listOf(
            binding.barEq1, binding.barEq2, binding.barEq3,
            binding.barEq4, binding.barEq5, binding.barEq6, binding.barEq7
        )
        for (i in bars.indices) {
            val lp = bars[i].layoutParams
            lp.height = dpToPx(defaultHeightsDp[i])
            bars[i].layoutParams = lp
        }
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
        val activeBg = android.graphics.Color.parseColor("#182A42")
        val inactiveBg = android.graphics.Color.parseColor("#121A2D")
        val activeStroke = ContextCompat.getColor(this, R.color.accent_cyan)
        val inactiveStroke = android.graphics.Color.parseColor("#1E2C44")

        val isAuto = mode == AudioReceiverService.OverrideMode.AUTO
        binding.btnChannelAuto.setTextColor(if (isAuto) activeColor else inactiveColor)
        binding.btnChannelAuto.backgroundTintList = android.content.res.ColorStateList.valueOf(if (isAuto) activeBg else inactiveBg)
        binding.btnChannelAuto.strokeColor = android.content.res.ColorStateList.valueOf(if (isAuto) activeStroke else inactiveStroke)
        binding.btnChannelAuto.strokeWidth = dpToPx(if (isAuto) 1.5f else 1.0f)

        val isLeft = mode == AudioReceiverService.OverrideMode.FORCE_LEFT
        binding.btnChannelLeft.setTextColor(if (isLeft) activeColor else inactiveColor)
        binding.btnChannelLeft.backgroundTintList = android.content.res.ColorStateList.valueOf(if (isLeft) activeBg else inactiveBg)
        binding.btnChannelLeft.strokeColor = android.content.res.ColorStateList.valueOf(if (isLeft) activeStroke else inactiveStroke)
        binding.btnChannelLeft.strokeWidth = dpToPx(if (isLeft) 1.5f else 1.0f)

        val isRight = mode == AudioReceiverService.OverrideMode.FORCE_RIGHT
        binding.btnChannelRight.setTextColor(if (isRight) activeColor else inactiveColor)
        binding.btnChannelRight.backgroundTintList = android.content.res.ColorStateList.valueOf(if (isRight) activeBg else inactiveBg)
        binding.btnChannelRight.strokeColor = android.content.res.ColorStateList.valueOf(if (isRight) activeStroke else inactiveStroke)
        binding.btnChannelRight.strokeWidth = dpToPx(if (isRight) 1.5f else 1.0f)
    }

    private fun setLatencyMode(multiplier: Int) {
        val service = audioService ?: return
        service.bufferLatencyMultiplier = multiplier
    }

    private fun updateNetworkAndAudioRouteInfo() {
        try {
            val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            val activeNetwork = cm.activeNetwork
            val caps = cm.getNetworkCapabilities(activeNetwork)

            val localIp = getLocalWifiIpAddress() ?: ""

            if (localIp.startsWith("192.168.42.") || localIp.startsWith("192.168.49.") || caps?.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) == true) {
                binding.tvNetworkMode.text = "USB Tethering (~3ms Latency)"
                binding.tvNetworkMode.setTextColor(ContextCompat.getColor(this, R.color.status_connected))
            } else if (caps?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true) {
                binding.tvNetworkMode.text = "Wi-Fi Network ($localIp)"
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
                    android.media.AudioDeviceInfo.TYPE_USB_HEADSET -> hasWired = true
                    android.media.AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
                    android.media.AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> hasBt = true
                }
            }

            if (hasWired) {
                binding.tvHeadphoneRoute.text = "Audio Route: Wired Headphones (0-Lag Direct)"
                binding.tvHeadphoneRoute.setTextColor(ContextCompat.getColor(this, R.color.status_connected))
            } else if (hasBt) {
                binding.tvHeadphoneRoute.text = "Audio Route: Bluetooth Audio (+150ms Delay)"
                binding.tvHeadphoneRoute.setTextColor(ContextCompat.getColor(this, R.color.status_connecting))
            } else {
                binding.tvHeadphoneRoute.text = "Audio Route: Phone Speaker Output"
                binding.tvHeadphoneRoute.setTextColor(ContextCompat.getColor(this, R.color.text_secondary))
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    private fun performServerScan(autoConnect: Boolean = false) {
        lifecycleScope.launch(Dispatchers.IO) {
            withContext(Dispatchers.Main) {
                Toast.makeText(this@MainActivity, "Scanning local network for DeskSound Server...", Toast.LENGTH_SHORT).show()
                binding.btnScanServer.isEnabled = false
                binding.btnScanServer.text = "SCANNING..."
            }

            var foundIp: String? = null

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
                        foundIp = results.firstOrNull { it != null }
                    }
                }
            }

            withContext(Dispatchers.Main) {
                binding.btnScanServer.isEnabled = true
                binding.btnScanServer.text = "SCAN"

                if (foundIp != null) {
                    binding.etServerIp.setText(foundIp)
                    savePrefs(foundIp!!, 5000)
                    Toast.makeText(this@MainActivity, "DeskSound Server Found: $foundIp", Toast.LENGTH_LONG).show()

                    if (autoConnect) {
                        checkNotificationPermission()
                        audioService?.startStreaming(foundIp!!, 5000)
                    }
                } else if (!autoConnect) {
                    Toast.makeText(this@MainActivity, "No DeskSound Server detected. Check PC app & Wi-Fi.", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun getLocalWifiIpAddress(): String? {
        try {
            val interfaces = NetworkInterface.getNetworkInterfaces()
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
        registerDynamicMonitors()
    }

    override fun onStop() {
        super.onStop()
        unregisterDynamicMonitors()
        if (isBound) {
            audioService?.onStatusChangedListener = null
            audioService?.onAudioLevelListener = null
            unbindService(serviceConnection)
            isBound = false
        }
    }

    private fun registerDynamicMonitors() {
        try {
            val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            val builder = android.net.NetworkRequest.Builder()
            networkCallback = object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: android.net.Network) {
                    runOnUiThread { updateNetworkAndAudioRouteInfo() }
                }
                override fun onLost(network: android.net.Network) {
                    runOnUiThread { updateNetworkAndAudioRouteInfo() }
                }
                override fun onCapabilitiesChanged(network: android.net.Network, networkCapabilities: NetworkCapabilities) {
                    runOnUiThread { updateNetworkAndAudioRouteInfo() }
                }
            }
            cm.registerNetworkCallback(builder.build(), networkCallback!!)
        } catch (e: Exception) {
            e.printStackTrace()
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            try {
                val audioManager = getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager
                audioDeviceCallback = object : android.media.AudioDeviceCallback() {
                    override fun onAudioDevicesAdded(addedDevices: Array<out android.media.AudioDeviceInfo>?) {
                        runOnUiThread { updateNetworkAndAudioRouteInfo() }
                    }
                    override fun onAudioDevicesRemoved(removedDevices: Array<out android.media.AudioDeviceInfo>?) {
                        runOnUiThread { updateNetworkAndAudioRouteInfo() }
                    }
                }
                audioManager.registerAudioDeviceCallback(audioDeviceCallback, null)
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }

    private fun unregisterDynamicMonitors() {
        try {
            networkCallback?.let {
                val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
                cm.unregisterNetworkCallback(it)
            }
            networkCallback = null
        } catch (e: Exception) {
            e.printStackTrace()
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            try {
                audioDeviceCallback?.let {
                    val audioManager = getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager
                    audioManager.unregisterAudioDeviceCallback(it)
                }
                audioDeviceCallback = null
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }

    private fun switchTab(tab: AppTab) {
        currentTab = tab

        val activeText = ContextCompat.getColor(this, R.color.primary)
        val inactiveText = ContextCompat.getColor(this, R.color.text_secondary)

        try {
            val isConn = tab == AppTab.CONNECTION
            binding.imgNavConnection.imageTintList = android.content.res.ColorStateList.valueOf(if (isConn) activeText else inactiveText)
            binding.tvNavConnection.setTextColor(if (isConn) activeText else inactiveText)
            binding.tvNavConnection.typeface = if (isConn) android.graphics.Typeface.DEFAULT_BOLD else android.graphics.Typeface.DEFAULT

            val isMon = tab == AppTab.MONITOR
            binding.imgNavMonitor.imageTintList = android.content.res.ColorStateList.valueOf(if (isMon) activeText else inactiveText)
            binding.tvNavMonitor.setTextColor(if (isMon) activeText else inactiveText)
            binding.tvNavMonitor.typeface = if (isMon) android.graphics.Typeface.DEFAULT_BOLD else android.graphics.Typeface.DEFAULT
        } catch (e: Exception) {
            e.printStackTrace()
        }

        binding.layoutTabConnection.visibility = if (tab == AppTab.CONNECTION) android.view.View.VISIBLE else android.view.View.GONE
        binding.layoutTabMonitor.visibility = if (tab == AppTab.MONITOR) android.view.View.VISIBLE else android.view.View.GONE
    }

    private fun switchConnectionMode(mode: ConnectionMode, userTriggered: Boolean = true) {
        currentConnectionMode = mode

        val activeBg = ContextCompat.getColor(this, R.color.card)
        val inactiveBg = ContextCompat.getColor(this, R.color.surface)
        val activeTextColor = ContextCompat.getColor(this, R.color.text_primary)
        val inactiveTextColor = ContextCompat.getColor(this, R.color.text_secondary)
        val activeStroke = ContextCompat.getColor(this, R.color.primary)
        val inactiveStroke = ContextCompat.getColor(this, R.color.border_muted)

        if (mode == ConnectionMode.WIFI) {
            binding.btnModeWifi.setTextColor(activeTextColor)
            binding.btnModeWifi.iconTint = android.content.res.ColorStateList.valueOf(activeStroke)
            binding.btnModeWifi.backgroundTintList = android.content.res.ColorStateList.valueOf(activeBg)
            binding.btnModeWifi.strokeColor = android.content.res.ColorStateList.valueOf(activeStroke)
            binding.btnModeWifi.strokeWidth = dpToPx(1.5f)

            binding.btnModeUsb.setTextColor(inactiveTextColor)
            binding.btnModeUsb.iconTint = android.content.res.ColorStateList.valueOf(inactiveTextColor)
            binding.btnModeUsb.backgroundTintList = android.content.res.ColorStateList.valueOf(inactiveBg)
            binding.btnModeUsb.strokeColor = android.content.res.ColorStateList.valueOf(inactiveStroke)
            binding.btnModeUsb.strokeWidth = dpToPx(1.0f)

            binding.tvModeTip.text = "Wi-Fi Mode active (Normal local network streaming)"
            binding.tvModeTip.setTextColor(inactiveTextColor)
            binding.layoutServerIpSection.visibility = android.view.View.VISIBLE

            if (userTriggered) {
                val prefs = getSharedPreferences("desksound_prefs", MODE_PRIVATE)
                val wifiIp = prefs.getString("wifi_ip", "192.168.1.100") ?: "192.168.1.100"
                binding.etServerIp.setText(wifiIp)
                savePrefs(wifiIp, 5000, mode = ConnectionMode.WIFI)
                performServerScan()
            }
        } else {
            binding.btnModeUsb.setTextColor(activeTextColor)
            binding.btnModeUsb.iconTint = android.content.res.ColorStateList.valueOf(activeStroke)
            binding.btnModeUsb.backgroundTintList = android.content.res.ColorStateList.valueOf(activeBg)
            binding.btnModeUsb.strokeColor = android.content.res.ColorStateList.valueOf(activeStroke)
            binding.btnModeUsb.strokeWidth = dpToPx(1.5f)

            binding.btnModeWifi.setTextColor(inactiveTextColor)
            binding.btnModeWifi.iconTint = android.content.res.ColorStateList.valueOf(inactiveTextColor)
            binding.btnModeWifi.backgroundTintList = android.content.res.ColorStateList.valueOf(inactiveBg)
            binding.btnModeWifi.strokeColor = android.content.res.ColorStateList.valueOf(inactiveStroke)
            binding.btnModeWifi.strokeWidth = dpToPx(1.0f)

            binding.tvModeTip.text = "USB Mode active (~1ms Latency): Automated ADB reverse loopback configured (no IP needed)"
            binding.tvModeTip.setTextColor(ContextCompat.getColor(this, R.color.primary))
            binding.layoutServerIpSection.visibility = android.view.View.GONE

            setLatencyMode(1)

            if (userTriggered) {
                val usbIp = detectUsbHostIp()
                binding.etServerIp.setText(usbIp)
                savePrefs(usbIp, 5000, mode = ConnectionMode.USB)
                Toast.makeText(this, "USB Mode Activated: Low Latency preset applied (~3ms)", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun dpToPx(dp: Float): Int {
        return (dp * resources.displayMetrics.density).toInt()
    }

    private fun detectUsbHostIp(): String {
        try {
            val localIp = getLocalWifiIpAddress() ?: ""
            if (localIp.startsWith("192.168.42.")) {
                return "192.168.42.129"
            } else if (localIp.startsWith("192.168.49.")) {
                return "192.168.49.1"
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return "127.0.0.1"
    }

    private fun loadSavedPrefs() {
        val prefs = getSharedPreferences("desksound_prefs", MODE_PRIVATE)
        val modeStr = prefs.getString("connection_mode", "WIFI") ?: "WIFI"
        val savedMode = if (modeStr == "USB") ConnectionMode.USB else ConnectionMode.WIFI
        val defaultIp = if (savedMode == ConnectionMode.USB) "127.0.0.1" else "192.168.1.100"
        binding.etServerIp.setText(prefs.getString("ip", defaultIp))
        binding.etServerPort.setText(prefs.getInt("port", 5000).toString())
        switchConnectionMode(savedMode, userTriggered = false)
    }

    private fun savePrefs(ip: String, port: Int, mode: ConnectionMode = currentConnectionMode) {
        val prefs = getSharedPreferences("desksound_prefs", MODE_PRIVATE)
        val editor = prefs.edit().putString("ip", ip).putInt("port", port).putString("connection_mode", mode.name)
        if (mode == ConnectionMode.WIFI) {
            editor.putString("wifi_ip", ip)
        } else {
            editor.putString("usb_ip", ip)
        }
        editor.apply()
    }

    private fun checkNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.POST_NOTIFICATIONS), 101)
            }
        }
    }

    private fun updateUiState(state: AudioReceiverService.State, message: String?) {
        when (state) {
            AudioReceiverService.State.DISCONNECTED -> {
                binding.statusDot.setBackgroundColor(ContextCompat.getColor(this, R.color.status_disconnected))
                binding.tvStatus.text = "DISCONNECTED"
                binding.tvStatus.setTextColor(ContextCompat.getColor(this, R.color.status_disconnected))
                binding.btnConnect.text = "START STREAMING"
                binding.btnConnect.setTextColor(android.graphics.Color.parseColor("#080D1A"))
                binding.btnConnect.isEnabled = true
                binding.btnConnect.backgroundTintList = android.content.res.ColorStateList.valueOf(ContextCompat.getColor(this, R.color.primary))
                resetEqualizerSpectrumToBaseline()
            }
            AudioReceiverService.State.CONNECTING -> {
                binding.statusDot.setBackgroundColor(ContextCompat.getColor(this, R.color.warning))
                binding.tvStatus.text = "CONNECTING"
                binding.tvStatus.setTextColor(ContextCompat.getColor(this, R.color.warning))
                binding.btnConnect.text = "CONNECTING..."
                binding.btnConnect.setTextColor(android.graphics.Color.parseColor("#080D1A"))
                binding.btnConnect.isEnabled = true
                binding.btnConnect.backgroundTintList = android.content.res.ColorStateList.valueOf(ContextCompat.getColor(this, R.color.warning))
            }
            AudioReceiverService.State.STREAMING -> {
                binding.statusDot.setBackgroundColor(ContextCompat.getColor(this, R.color.status_connected))
                binding.tvStatus.text = "STREAMING"
                binding.tvStatus.setTextColor(ContextCompat.getColor(this, R.color.status_connected))
                binding.btnConnect.text = "DISCONNECT STREAM"
                binding.btnConnect.setTextColor(android.graphics.Color.WHITE)
                binding.btnConnect.isEnabled = true
                binding.btnConnect.backgroundTintList = android.content.res.ColorStateList.valueOf(ContextCompat.getColor(this, R.color.status_disconnected))
                switchTab(AppTab.MONITOR)
            }
            AudioReceiverService.State.ERROR -> {
                binding.statusDot.setBackgroundColor(ContextCompat.getColor(this, R.color.status_disconnected))
                binding.tvStatus.text = "ERROR"
                binding.tvStatus.setTextColor(ContextCompat.getColor(this, R.color.status_disconnected))
                binding.btnConnect.text = "RETRY CONNECTION"
                binding.btnConnect.setTextColor(android.graphics.Color.parseColor("#080D1A"))
                binding.btnConnect.isEnabled = true
                binding.btnConnect.backgroundTintList = android.content.res.ColorStateList.valueOf(ContextCompat.getColor(this, R.color.primary))
                resetEqualizerSpectrumToBaseline()
            }
        }
    }
}
