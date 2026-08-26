#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include "../resources/resource.h"

#include <string>

using namespace std;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t window_class[] = L"HomeCloudWindow";
constexpr wchar_t app_title[] = L"HomeCloud";
constexpr wchar_t home_url[] = L"https://your-homecloud.example/";

ComPtr<ICoreWebView2Controller> controller;
ComPtr<ICoreWebView2> webview;

void resize_webview(HWND window) {
    if (!controller) return;
    RECT bounds{};
    GetClientRect(window, &bounds);
    controller->put_Bounds(bounds);
}

void show_startup_error(HWND window, const wchar_t* details) {
    wstring message = L"Nie udało się uruchomić HomeCloud.\n\n";
    message += details;
    MessageBoxW(window, message.c_str(), app_title, MB_OK | MB_ICONERROR);
}

void create_webview(HWND window) {
    const auto environment_created = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [window](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(result) || !environment) {
                show_startup_error(window, L"Brakuje Microsoft Edge WebView2 Runtime.");
                return result;
            }

            return environment->CreateCoreWebView2Controller(
                window,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [window](HRESULT controller_result, ICoreWebView2Controller* created) -> HRESULT {
                        if (FAILED(controller_result) || !created) {
                            show_startup_error(window, L"Nie można utworzyć okna aplikacji.");
                            return controller_result;
                        }

                        controller = created;
                        controller->get_CoreWebView2(&webview);
                        resize_webview(window);

                        ComPtr<ICoreWebView2Settings> settings;
                        webview->get_Settings(&settings);
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_AreDevToolsEnabled(FALSE);
                        settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                        settings->put_IsZoomControlEnabled(TRUE);

                        webview->Navigate(home_url);
                        return S_OK;
                    }).Get());
        });

    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr, environment_created.Get());
    if (FAILED(result)) show_startup_error(window, L"Nie można wczytać silnika WebView2.");
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE:
        resize_webview(window);
        return 0;
    case WM_SETFOCUS:
        if (controller) controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
    case WM_DESTROY:
        webview.Reset();
        if (controller) controller->Close();
        controller.Reset();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    WNDCLASSEXW window_type{};
    window_type.cbSize = sizeof(window_type);
    window_type.hInstance = instance;
    window_type.lpfnWndProc = window_proc;
    window_type.lpszClassName = window_class;
    window_type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_HOMECLOUD));
    window_type.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_HOMECLOUD), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    window_type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&window_type);

    HWND window = CreateWindowExW(
        0, window_class, app_title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);
    create_webview(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CoUninitialize();
    return static_cast<int>(message.wParam);
}
