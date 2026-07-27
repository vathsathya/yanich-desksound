#include "../include/audio_backend.h"
#include "../include/network_server.h"
#include "../include/config_manager.h"
#include "../include/logger.h"
#include "../include/gui_app.h"
#include "../thirdparty/imgui/imgui.h"

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include "../thirdparty/imgui/imgui_impl_win32.h"
#include "../thirdparty/imgui/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON (WM_USER + 1)
#define IDM_TRAY_SHOW          1001
#define IDM_TRAY_TOGGLE_SERVER 1002
#define IDM_TRAY_EXIT          1003

static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static NOTIFYICONDATAW          g_nid = {};

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void InitTrayIcon(HWND hwnd) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    
    HICON hIcon = (HICON)::LoadImageW(GetModuleHandle(NULL), L"app_icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!hIcon) {
        hIcon = ::LoadIcon(NULL, IDI_APPLICATION);
    }
    g_nid.hIcon = hIcon;
    wcscpy_s(g_nid.szTip, L"Yanich DeskSound Server");
    
    ::Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon() {
    if (g_nid.hWnd) {
        ::Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_nid.hIcon) {
            ::DestroyIcon(g_nid.hIcon);
            g_nid.hIcon = NULL;
        }
        g_nid.hWnd = NULL;
    }
}

static void ShowTrayContextMenu(HWND hwnd) {
    POINT pt;
    ::GetCursorPos(&pt);
    HMENU hMenu = ::CreatePopupMenu();
    if (hMenu) {
        ::AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, L"Open Yanich DeskSound");
        ::AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        
        bool isServerRunning = NetworkServer::Instance().IsActive();
        if (isServerRunning) {
            ::AppendMenuW(hMenu, MF_STRING, IDM_TRAY_TOGGLE_SERVER, L"Stop Server");
        } else {
            ::AppendMenuW(hMenu, MF_STRING, IDM_TRAY_TOGGLE_SERVER, L"Start Server");
        }
        
        ::AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        ::AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

        ::SetForegroundWindow(hwnd);
        ::TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        ::DestroyMenu(hMenu);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;

    LOG_INFO("[Server] Starting Yanich DeskSound Windows Server v1.2.6 (Unified ImGui)");

    // Initialize Network Audio Streaming Server
    NetworkServer::Instance().Start(5000, 5001);

    // Initialize Audio Capture Backend
    std::unique_ptr<AudioBackend> audio(CreateAudioBackend());
    if (audio) {
        audio->Initialize();
        audio->SetDataCallback([](const float* data, size_t frames, int channels, int sampleRate) {
            NetworkServer::Instance().BroadcastAudio(data, frames, channels, sampleRate);
        });
        audio->StartCapture();
    }

    // Create Application Window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"YanichDeskSoundClass", NULL };
    ::RegisterClassExW(&wc);
    RECT rect = { 0, 0, 920, 750 };
    ::AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rect.right - rect.left;
    int winHeight = rect.bottom - rect.top;
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Yanich DeskSound Server", WS_OVERLAPPEDWINDOW, 100, 100, winWidth, winHeight, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    InitTrayIcon(hwnd);

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    GuiApp::Instance().Initialize();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ImVec4 clear_color = ImVec4(0.06f, 0.09f, 0.16f, 1.00f);

    // Main loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        GuiApp::Instance().RenderUI(audio.get());

        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    if (audio) audio->StopCapture();
    NetworkServer::Instance().Stop();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    RemoveTrayIcon();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        if ((wParam & 0xfff0) == SC_MINIMIZE) {
            ServerConfig cfg = ConfigManager::Instance().GetConfig();
            if (cfg.minimizeToTray) {
                ::ShowWindow(hWnd, SW_HIDE);
                return 0;
            }
        }
        break;
    case WM_CLOSE:
        {
            ServerConfig cfg = ConfigManager::Instance().GetConfig();
            if (cfg.minimizeToTray) {
                ::ShowWindow(hWnd, SW_HIDE);
                return 0;
            } else {
                ::DestroyWindow(hWnd);
                return 0;
            }
        }
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
            ::ShowWindow(hWnd, SW_RESTORE);
            ::SetForegroundWindow(hWnd);
        } else if (lParam == WM_RBUTTONUP) {
            ShowTrayContextMenu(hWnd);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TRAY_SHOW:
            ::ShowWindow(hWnd, SW_RESTORE);
            ::SetForegroundWindow(hWnd);
            break;
        case IDM_TRAY_TOGGLE_SERVER:
            NetworkServer::Instance().SetActive(!NetworkServer::Instance().IsActive());
            break;
        case IDM_TRAY_EXIT:
            RemoveTrayIcon();
            ::DestroyWindow(hWnd);
            break;
        }
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon();
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

#else

// --- Linux Main Loop (GLFW + OpenGL 3 + ImGui) ---
#include <GLFW/glfw3.h>
#include "../thirdparty/imgui/imgui_impl_glfw.h"
#include "../thirdparty/imgui/imgui_impl_opengl3.h"

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    LOG_INFO("[Server] Starting Yanich DeskSound Linux Server v1.2.6 (Unified ImGui)");

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(480, 520, "Yanich DeskSound Server", NULL, NULL);
    if (!window) return 1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    GuiApp::Instance().Initialize();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    NetworkServer::Instance().Start(5000, 5001);

    std::unique_ptr<AudioBackend> audio(CreateAudioBackend());
    if (audio) {
        audio->Initialize();
        audio->SetDataCallback([](const float* data, size_t frames, int channels, int sampleRate) {
            NetworkServer::Instance().BroadcastAudio(data, frames, channels, sampleRate);
        });
        audio->StartCapture();
    }

    ImVec4 clear_color = ImVec4(0.06f, 0.09f, 0.16f, 1.00f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        GuiApp::Instance().RenderUI(audio.get());

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    if (audio) audio->StopCapture();
    NetworkServer::Instance().Stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

#endif
