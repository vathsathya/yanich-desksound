# 🔊 Yanich DeskSound `v1.0.0`

[![Release](https://img.shields.io/badge/Release-v1.0.0-00E5FF.svg)](https://github.com/vathsathya/yanich-desksound/releases/tag/v1.0.0)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Android-blue.svg)](https://github.com/vathsathya/yanich-desksound)

> **Ultra-Low Latency Desktop Audio Streaming System (Windows PC Server GUI + Native Android Client)**

**Yanich DeskSound v1.0.0** turns your Android smartphones into high-definition wireless speakers or dual-channel Left/Right stereo sound systems for your Windows PC with ultra-low latency (**~3ms USB Tethering / ~15ms 5GHz Wi-Fi**).

Created by **Vath Sathya**.

---

## 📦 Downloads (v1.0.0 Release)

- 🖥️ **Windows Desktop Server GUI**: **[`desksound.exe`](https://github.com/vathsathya/yanich-desksound/raw/main/desksound.exe)** *(318 KB, Portable Standalone Executable - Zero Installation Required)*
- 📱 **Android Receiver Client**: **[`app-release.apk`](https://github.com/vathsathya/yanich-desksound/raw/main/app-release.apk)** *(4.88 MB, Release APK for Android 7.0+)*

---

## 📖 User Guide & Quick Start

### 1. Windows PC Setup (Server)
1. Download and run **[`desksound.exe`](https://github.com/vathsathya/yanich-desksound/raw/main/desksound.exe)**. No installation needed!
2. Ensure **Server Status** displays `RUNNING (Port 5000)`.
3. Note your PC's Local IP address displayed in the Server status card (e.g. `192.168.1.100` or `192.168.42.x`).

### 2. Android Phone Setup (Receiver)
1. Download and install **[`app-release.apk`](https://github.com/vathsathya/yanich-desksound/raw/main/app-release.apk)** on your Android device.
2. Open the app. It will **automatically scan your local Wi-Fi / USB network and connect instantly (0 clicks required!)**.
3. Desktop audio will start playing through your phone speaker or headphones immediately!

---

## 🌐 Network Connection Modes & Performance

| Connection Type | Typical Latency | Recommended For |
| :--- | :--- | :--- |
| 🚀 **USB Tethering** | **~3ms (Zero-Lag)** | Gaming, Competitive Esports, Music Production |
| 📶 **5 GHz Wi-Fi** | **~15ms (Ultra-Fast)** | Movies, Streaming, General Daily Use |
| ⚠️ **2.4 GHz Wi-Fi** | **~45ms** | Background Music |

> [!TIP]
> For **Zero-Lag Gaming (~3ms)**, plug your phone into your PC via USB cable and enable **USB Tethering** in Android Settings. Yanich DeskSound will automatically detect USB mode!

---

## 🎧 Smart Auto Sync & Channel Controls

Yanich DeskSound features an intelligent dual-device channel sync engine:

### 1. Automatic Dual-Device Stereo Split
- **1 Phone Connected**: Plays full **Stereo (L + R)** audio stream.
- **2 Phones Connected**:
  - **Phone #1** automatically assigned to **Left Channel (L)**.
  - **Phone #2** automatically assigned to **Right Channel (R)**.
- **Instant Fallback**: If either phone disconnects, the remaining phone automatically switches back to **Stereo Mode (L + R)** in under 50ms without audio pops or clicks!

### 2. Master Server Channel Controls (Windows GUI)
Control audio channels directly from your PC screen:
- 🔄 **`Auto Sync`**: Automatic 1-phone / 2-phone channel assignment.
- 🔀 **`Swap L ⇄ R`**: 1-click channel swapper! Instantly swaps Left and Right phone channels.
- 🎧 **`Force Stereo`**: Force both connected phones to receive full Stereo simultaneously.
- 🔴 **`Disconnect`**: Disconnect/Kick any connected phone directly from the PC interface.

### 3. Android Manual Override
On your Android phone, choose between:
- 🔄 **Auto Sync (Default)**
- 🎧 **Force Left (L)**
- 🎧 **Force Right (R)**

---

## 🎛️ Volume & Gain Control

- **Master Volume**: Smooth slider (0% - 100%).
- **Left (L) Gain**: Independent Left channel gain adjustment (**-10dB to +10dB**).
- **Right (R) Gain**: Independent Right channel gain adjustment (**-10dB to +10dB**).
- **Step Buttons**: `-10`, `+10`, `-2dB`, `+2dB`, `Reset`.
- **Speaker Protection Hard Limiter**: Enforces strict 0% to 100% volume boundaries with IEEE 32-bit Float PCM clipping protection to preserve smartphone speaker hardware.

---

## 🛡️ 24/7 Long-Term Reliability & Memory Footprint

- **0% Memory Leak**: Thread-local static buffer reuse avoids continuous heap allocations.
- **Ultra-Lightweight**:
  - Windows Server RAM: **~10.5 MB**
  - Android App RAM: **~14.0 MB**
- **24/7 WASAPI Auto-Recovery**: If Windows audio devices change or headphones are unplugged, the server auto-reinitializes audio loopback within 1 second without crashing.
- **Persistent Auto-Reconnect**: If Wi-Fi drops or the PC reboots, the Android app automatically reconnects when the server becomes available again.

---

## 🛠️ Build Instructions

### Windows Server GUI (`main.cpp`)
Requires Visual Studio C++ Compiler (`cl.exe`).

```cmd
rc.exe /fo resource.res resource.rc
cl.exe /EHsc /std:c++17 main.cpp resource.res /Fe:desksound.exe /link /subsystem:windows
```

### Android Receiver App (`android/`)
Requires JDK 17+ and Android SDK.

```powershell
cd android
.\gradlew.bat assembleRelease
```

---

## 👤 Author & License

**Created by Vath Sathya**

Released under the **MIT License**.
