#!/bin/bash
set -e

echo "======================================================================="
echo " 🔊 Yanich DeskSound - Unified Linux Server Build Script"
echo "======================================================================="

VERSION=$(cat version.txt | tr -d '\r\n')
echo "[1/2] Building Unified Cross-Platform Server (Linux x64)..."

mkdir -p bin

g++ -O3 -std=c++17 \
  server/src/main.cpp \
  server/src/gui_app.cpp \
  server/src/custom_widgets.cpp \
  server/src/config_manager.cpp \
  server/src/logger.cpp \
  server/src/network_server.cpp \
  server/src/audio_pulse.cpp \
  server/thirdparty/imgui/imgui.cpp \
  server/thirdparty/imgui/imgui_draw.cpp \
  server/thirdparty/imgui/imgui_widgets.cpp \
  server/thirdparty/imgui/imgui_tables.cpp \
  server/thirdparty/imgui/imgui_impl_glfw.cpp \
  server/thirdparty/imgui/imgui_impl_opengl3.cpp \
  -Iserver/include \
  -Iserver/thirdparty/imgui \
  -lglfw -lGL -lpulse-simple -lpulse -lpthread -ldl \
  -o "yanich-desksound_v${VERSION}-linux-x64"

echo "[+] Linux Unified Server compiled successfully: yanich-desksound_v${VERSION}-linux-x64"
