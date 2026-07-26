# DeskSound Android Client Receiver

This is the official native Android receiver app for **Yanich DeskSound**. It streams low-latency desktop audio from your Windows PC (`desksound.exe`) over your local Wi-Fi / LAN network directly to your Android device.

---

## Technical Specifications

- **Audio Pipeline**: Android `AudioTrack` API.
- **Audio Format**: 48,000 Hz • Stereo • 32-bit Float PCM (`AudioFormat.ENCODING_PCM_FLOAT`).
- **Network Protocol**: Raw TCP Socket with `TCP_NODELAY` enabled for minimal buffering latency.
- **Background Playback**: Android `ForegroundService` with notification control so audio playback continues uninterrupted when switching apps or locking the screen.
- **Visualizer**: Real-time RMS peak level meter (20 fps UI update).

---

## Project Structure

```
android/
├── build.gradle                   # Root Gradle build configuration
├── settings.gradle                # Module inclusions
└── app/
    ├── build.gradle               # Android app dependencies (Kotlin, Material3)
    └── src/
        └── main/
            ├── AndroidManifest.xml
            ├── java/com/yanich/desksound/
            │   ├── MainActivity.kt          # UI controller & binding
            │   └── AudioReceiverService.kt  # TCP background streaming service
            └── res/
                ├── layout/activity_main.xml # UI Layout
                └── values/
                    ├── colors.xml
                    ├── strings.xml
                    └── themes.xml
```

---

## How to Build & Install

### Option 1: Android Studio (Recommended)
1. Launch **Android Studio**.
2. Select **Open** and choose the `android/` folder inside `yanich-desksound`.
3. Connect your Android device via USB (or start an Emulator).
4. Click **Run 'app'** (`Shift + F10`) or build the APK via **Build > Build Bundle(s) / APK(s) > Build APK(s)**.

### Option 2: Command Line (Gradle)
If you have Java JDK and Android SDK installed on your PATH:

```bash
cd android
gradlew assembleDebug
```

The compiled APK will be output to:
`android/app/build/outputs/apk/debug/app-debug.apk`

Install on your connected Android phone using ADB:
```bash
adb install app-debug.apk
```

---

## Quick Start Guide

1. **Start the Windows Server**:
   Run `desksound.exe` on your Windows PC:
   ```cmd
   .\desksound.exe
   ```
   *Note: Ensure port 5000 is allowed through Windows Defender Firewall for Private networks.*

2. **Open the Android App**:
   - Enter your PC's local IP address (e.g. `192.168.1.100`).
   - Enter port `5000`.
   - Tap **CONNECT**.

3. **Enjoy Low-Latency Desktop Audio**:
   - Desktop audio will instantly stream through your phone speaker / connected headphones.
   - Adjust volume directly using the slider or physical volume buttons.
