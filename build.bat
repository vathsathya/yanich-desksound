call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cd server
rc.exe /nologo /fo resource.res resource.rc
cl.exe /utf-8 /EHsc /std:c++17 src\main.cpp src\gui_app.cpp src\custom_widgets.cpp src\config_manager.cpp src\logger.cpp src\network_server.cpp src\audio_wasapi.cpp thirdparty\imgui\imgui.cpp thirdparty\imgui\imgui_draw.cpp thirdparty\imgui\imgui_widgets.cpp thirdparty\imgui\imgui_tables.cpp thirdparty\imgui\imgui_impl_win32.cpp thirdparty\imgui\imgui_impl_dx11.cpp resource.res /Iinclude /Ithirdparty\imgui /Fe:..\desksound.exe /link /subsystem:windows d3d11.lib d3dcompiler.lib Ws2_32.lib Advapi32.lib Ole32.lib
cd ..
