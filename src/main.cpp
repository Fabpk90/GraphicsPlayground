
extern "C" { __declspec(dllexport) extern const unsigned D3D12SDKVersion = 614;}

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\..\\external\\AgilitySDK\\build\\native\\bin\\x64\\"; }


#include <mimalloc.h>
#include <mimalloc-new-delete.h>
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    auto ptr = mi_new(size);
    TracySecureAlloc(ptr, size);
    return ptr;
}
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    /*if(allocator == nullptr)
    {
        allocator = new eastl::allocator_malloc("Allocator");
    }*/

    auto ptr = mi_new_aligned(size, alignment);
    TracySecureAlloc(ptr, size);

    return ptr;
}

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <memory>
#include <iomanip>
#include <iostream>


#ifndef NOMINMAX
#    define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#ifndef PLATFORM_WIN32
#    define PLATFORM_WIN32 1
#endif


#include "Engine.h"

using namespace Diligent;

std::unique_ptr<Engine> engine;

struct WindowMessageData
{
    HWND   hWnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
};

// Called every time the NativeNativeAppBase receives a message
LRESULT CALLBACK MessageProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WindowMessageData data{wnd, message, wParam, lParam};

    if(engine)
    {
        engine->m_inputController.HandleNativeMessage(static_cast<WindowMessageData*>(&data));
        engine->m_imguiRenderer->Win32_ProcHandler(wnd, message, wParam, lParam);
    }

    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(wnd, &ps);
            EndPaint(wnd, &ps);
            return 0;
        }
        case WM_SIZE: // Window size has been changed
            if (engine) {
                engine->windowResize(LOWORD(lParam), HIWORD(lParam));
            }
            return 0;

        case WM_CHAR:
            if (wParam == VK_ESCAPE)
                PostQuitMessage(0);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_GETMINMAXINFO: {
            LPMINMAXINFO lpMMI = (LPMINMAXINFO) lParam;

            lpMMI->ptMinTrackSize.x = 320;
            lpMMI->ptMinTrackSize.y = 240;
            return 0;
        }

        default:
            return DefWindowProc(wnd, message, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int cmdShow)
{
    ZoneScoped;
    #if defined(_DEBUG) || defined(DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    #endif

    engine = std::make_unique<Engine>();

    std::wstring Title(L"Graphics Playground");
    switch (engine->getRenderType())
    {
        case RENDER_DEVICE_TYPE_D3D11: Title.append(L" (D3D11)"); break;
        case RENDER_DEVICE_TYPE_D3D12: Title.append(L" (D3D12)"); break;
        case RENDER_DEVICE_TYPE_GL: Title.append(L" (GL)"); break;
        case RENDER_DEVICE_TYPE_VULKAN: Title.append(L" (VK)"); break;
    }
    // Register our window class
    WNDCLASSEX wcex = {sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, MessageProc,
                       0L, 0L, instance, nullptr, nullptr, nullptr, nullptr,TEXT("SampleApp"), nullptr};
    RegisterClassEx(&wcex);

    // Create a window
    LONG WindowWidth  = 1280;
    LONG WindowHeight = 1024;
    RECT rc           = {0, 0, WindowWidth, WindowHeight};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND wnd = CreateWindow(TEXT("SampleApp"), Title.c_str(),
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, instance, nullptr);
    if (!wnd)
    {
        MessageBox(nullptr, (TEXT("Cannot create window")), TEXT("Error"), MB_OK | MB_ICONERROR);
        return 0;
    }
    ShowWindow(wnd, cmdShow);
    UpdateWindow(wnd);

    if (!engine->initializeDiligentEngine(wnd))
        return -1;

    engine->createResources();

    // Main message loop
    MSG msg = {nullptr};
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            engine->render();
            engine->present();
        }
    }

    return (int)msg.wParam;
}


