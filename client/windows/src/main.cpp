#include "homecloud_client/client.hpp"
#include "win_http.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using namespace homecloud_client;
using homecloud_windows::WinHttpTransport;
using Gdiplus::Bitmap;
using Gdiplus::Graphics;

namespace {

constexpr wchar_t window_class[] = L"HomeCloudWindow";
constexpr wchar_t server_url[] = L"https://your-homecloud.example";
enum Id { user_id = 101, password_id, login_id, files_id, back_id, refresh_id,
          upload_id, folder_id, trash_id, status_id, path_id, logout_id, rename_id };

Client client;
WinHttpTransport transport(server_url);
vector<CloudFile> files;
HFONT title_font{}, normal_font{};
HBRUSH background_brush{}, panel_brush{}, edit_brush{};
ULONG_PTR gdiplus_token{};
unique_ptr<Bitmap> wood_texture;
HIMAGELIST tile_images{};
HWND user_box{}, password_box{}, login_button{}, list_view{}, status_label{}, path_label{};
vector<HWND> app_controls;
vector<HWND> login_controls;

constexpr COLORREF wood_dark = RGB(47, 32, 22);
constexpr COLORREF wood_light = RGB(83, 57, 39);
constexpr COLORREF ivory = RGB(246, 239, 224);
constexpr COLORREF paper = RGB(255, 252, 244);
constexpr COLORREF forest = RGB(64, 82, 43);
constexpr COLORREF ink = RGB(55, 43, 31);

wstring widen(const string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

string narrow(const wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

wstring text(HWND control) {
    const int size = GetWindowTextLengthW(control);
    wstring result(static_cast<size_t>(size) + 1, L'\0');
    GetWindowTextW(control, result.data(), size + 1);
    result.resize(static_cast<size_t>(size));
    return result;
}

wstring size_text(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) { value /= 1024.0; ++unit; }
    wostringstream output;
    output << fixed << setprecision(unit ? 1 : 0) << value << L' ' << units[unit];
    return output.str();
}

void message(HWND window, const wstring& value, bool error = false) {
    SetWindowTextW(status_label, value.c_str());
    if (error) MessageBoxW(window, value.c_str(), L"HomeCloud", MB_OK | MB_ICONWARNING);
}

void show_app(bool visible) {
    for (const auto control : login_controls) ShowWindow(control, visible ? SW_HIDE : SW_SHOW);
    for (const auto control : app_controls) ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    InvalidateRect(GetParent(login_button), nullptr, TRUE);
}

void refresh(HWND window) {
    try {
        files = client.parse_files(transport.send(client.list_request(client.current_path())));
        ListView_DeleteAllItems(list_view);
        for (size_t index = 0; index < files.size(); ++index) {
            const auto& file = files[index];
            const filesystem::path path = widen(file.path);
            const wstring name = path.filename().wstring();
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
            item.iItem = static_cast<int>(index);
            item.pszText = const_cast<wchar_t*>(name.c_str());
            item.lParam = static_cast<LPARAM>(index);
            item.iImage = file.directory ? 0 : 1;
            ListView_InsertItem(list_view, &item);
        }
        const filesystem::path current = widen(client.current_path());
        const wstring breadcrumb = client.current_path() == "." ? L"Moje pliki" :
                                   L"Moje pliki  ›  " + current.filename().wstring();
        SetWindowTextW(path_label, breadcrumb.c_str());
        const auto storage = client.parse_storage(transport.send(client.storage_request()));
        message(window, L"Zajęte " + size_text(storage.used_bytes) + L" z " +
                        size_text(storage.quota_bytes));
    } catch (const exception& error) {
        message(window, L"Nie udało się odświeżyć: " + widen(error.what()), true);
    }
}

void login(HWND window) {
    try {
        const string username = narrow(text(user_box));
        client.accept_login(username, transport.send(client.login_request(username,
                                                    narrow(text(password_box)))));
        SetWindowTextW(password_box, L"");
        show_app(true);
        refresh(window);
    } catch (...) {
        message(window, L"Nieprawidłowa nazwa, hasło albo brak połączenia z HomeCloud.", true);
    }
}

string child_path(const wstring& name) {
    const string utf8 = narrow(name);
    return client.current_path() == "." ? utf8 : client.current_path() + "/" + utf8;
}

void upload_file(HWND window) {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW picker{sizeof(picker)};
    picker.hwndOwner = window;
    picker.lpstrFile = path;
    picker.nMaxFile = MAX_PATH;
    picker.lpstrFilter = L"Wszystkie pliki\0*.*\0";
    picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&picker)) return;
    try {
        ifstream stream(path, ios::binary);
        vector<uint8_t> data((istreambuf_iterator<char>(stream)), {});
        const auto name = filesystem::path(path).filename().wstring();
        static_cast<void>(transport.send(client.upload_request(child_path(name), move(data))));
        message(window, L"Plik został wysłany.");
        refresh(window);
    } catch (const exception& error) { message(window, widen(error.what()), true); }
}

void create_folder(HWND window) {
    wstring name = L"Nowy folder";
    int suffix = 2;
    const auto exists = [&](const wstring& candidate) {
        for (const auto& file : files) {
            if (filesystem::path(widen(file.path)).filename().wstring() == candidate) return true;
        }
        return false;
    };
    while (exists(name)) name = L"Nowy folder (" + to_wstring(suffix++) + L")";
    try {
        static_cast<void>(transport.send(client.create_folder_request(child_path(name))));
        message(window, L"Utworzono „" + name + L"”.");
        refresh(window);
    } catch (...) { message(window, L"Nie udało się utworzyć folderu.", true); }
}

int selected_index() { return ListView_GetNextItem(list_view, -1, LVNI_SELECTED); }

void open_selected(HWND window) {
    const int index = selected_index();
    if (index < 0 || static_cast<size_t>(index) >= files.size() || !files[index].directory) return;
    client.open_folder(files[index].path);
    refresh(window);
}

void trash_selected(HWND window) {
    const int index = selected_index();
    if (index < 0 || static_cast<size_t>(index) >= files.size()) return;
    if (MessageBoxW(window, L"Przenieść wybrany element do kosza?", L"HomeCloud",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    try { static_cast<void>(transport.send(client.trash_request(files[index].path))); refresh(window); }
    catch (...) { message(window, L"Nie udało się przenieść elementu do kosza.", true); }
}

void rename_selected(HWND window) {
    const int index = selected_index();
    if (index >= 0) {
        SetFocus(list_view);
        ListView_EditLabel(list_view, index);
    } else message(window, L"Najpierw wybierz folder lub plik.");
}

HBITMAP tile_bitmap(HWND window, bool folder) {
    HDC screen = GetDC(window);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 64, 64);
    auto old = SelectObject(memory, bitmap);
    RECT rect{0, 0, 64, 64}; FillRect(memory, &rect, edit_brush);
    HBRUSH green = CreateSolidBrush(forest);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(45, 60, 30));
    auto old_brush = SelectObject(memory, green); auto old_pen = SelectObject(memory, pen);
    if (folder) {
        RoundRect(memory, 8, 20, 56, 52, 8, 8);
        Rectangle(memory, 12, 14, 34, 25);
    } else {
        RoundRect(memory, 16, 8, 49, 56, 6, 6);
        SelectObject(memory, GetStockObject(WHITE_PEN));
        MoveToEx(memory, 24, 25, nullptr); LineTo(memory, 42, 25);
        MoveToEx(memory, 24, 33, nullptr); LineTo(memory, 42, 33);
        MoveToEx(memory, 24, 41, nullptr); LineTo(memory, 38, 41);
    }
    SelectObject(memory, old_brush); SelectObject(memory, old_pen);
    DeleteObject(green); DeleteObject(pen); SelectObject(memory, old);
    DeleteDC(memory); ReleaseDC(window, screen);
    return bitmap;
}

void layout(HWND window) {
    RECT area{}; GetClientRect(window, &area);
    const int width = max(860L, area.right);
    const int height = max(620L, area.bottom);
    MoveWindow(GetDlgItem(window, logout_id), width - 142, 35, 104, 34, TRUE);
    MoveWindow(path_label, 270, 35, width - 440, 38, TRUE);
    MoveWindow(list_view, 270, 160, width - 308, height - 198, TRUE);
    InvalidateRect(window, nullptr, TRUE);
}

HWND control(HWND parent, const wchar_t* type, const wchar_t* caption, DWORD style,
             int x, int y, int width, int height, int id) {
    HWND result = CreateWindowExW(wcscmp(type, WC_EDITW) == 0 ? WS_EX_CLIENTEDGE : 0, type, caption,
                                  WS_CHILD | style, x, y, width, height, parent,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(normal_font), TRUE);
    return result;
}

void round_panel(HDC dc, const RECT& rect, int radius, COLORREF color) {
    const auto brush = CreateSolidBrush(color);
    const auto pen = CreatePen(PS_SOLID, 1, RGB(220, 207, 186));
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, old_brush); SelectObject(dc, old_pen);
    DeleteObject(brush); DeleteObject(pen);
}

void paint_background(HWND window, HDC dc) {
    RECT area{}; GetClientRect(window, &area);
    if (wood_texture && wood_texture->GetLastStatus() == Gdiplus::Ok) {
        Graphics graphics(dc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(wood_texture.get(), 0, 0, area.right, area.bottom);
    } else FillRect(dc, &area, background_brush);
    if (client.authenticated()) {
        round_panel(dc, RECT{18, 14, 226, area.bottom - 18}, 24, ivory);
        round_panel(dc, RECT{246, 14, area.right - 18, 116}, 24, ivory);
        round_panel(dc, RECT{246, 132, area.right - 18, area.bottom - 18}, 24, ivory);
    } else {
        round_panel(dc, RECT{42, 34, 466, 390}, 28, ivory);
    }
}

void draw_button(const DRAWITEMSTRUCT& item) {
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const COLORREF fill = disabled ? RGB(150, 150, 140) : pressed ? RGB(48, 64, 32) : forest;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(42, 57, 28));
    auto old_brush = SelectObject(item.hDC, brush);
    auto old_pen = SelectObject(item.hDC, pen);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right,
              item.rcItem.bottom, 14, 14);
    SelectObject(item.hDC, old_brush); SelectObject(item.hDC, old_pen);
    DeleteObject(brush); DeleteObject(pen);
    wchar_t caption[128]{}; GetWindowTextW(item.hwndItem, caption, 128);
    SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, RGB(255, 252, 244));
    SelectObject(item.hDC, normal_font);
    DrawTextW(item.hDC, caption, -1, const_cast<RECT*>(&item.rcItem),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void create_controls(HWND window) {
    HWND logo = CreateWindowW(L"STATIC", L"⌂  HomeCloud", WS_CHILD | WS_VISIBLE, 72, 66, 350, 54,
                              window, nullptr, nullptr, nullptr);
    SendMessageW(logo, WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
    login_controls.push_back(logo);
    HWND welcome = CreateWindowW(L"STATIC", L"Twoje bezpieczne miejsce na pliki", WS_CHILD | WS_VISIBLE,
                                 72, 126, 350, 30, window, nullptr, nullptr, nullptr);
    SendMessageW(welcome, WM_SETFONT, reinterpret_cast<WPARAM>(normal_font), TRUE);
    login_controls.push_back(welcome);
    user_box = control(window, WC_EDITW, L"", WS_VISIBLE | ES_AUTOHSCROLL, 72, 184, 350, 40, user_id);
    SendMessageW(user_box, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Nazwa użytkownika"));
    password_box = control(window, WC_EDITW, L"", WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
                           72, 238, 350, 40, password_id);
    SendMessageW(password_box, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Hasło"));
    login_button = control(window, WC_BUTTONW, L"Zaloguj się", WS_VISIBLE | BS_OWNERDRAW,
                           72, 296, 350, 46, login_id);
    login_controls.insert(login_controls.end(), {user_box, password_box, login_button});

    HWND brand = control(window, WC_STATICW, L"⌂  HomeCloud", SS_CENTER, 35, 45, 174, 42, 0);
    SendMessageW(brand, WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
    app_controls.push_back(brand);
    path_label = control(window, WC_STATICW, L"Moje pliki", 0, 270, 35, 430, 38, path_id);
    SendMessageW(path_label, WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
    app_controls.push_back(path_label);
    app_controls.push_back(control(window, WC_BUTTONW, L"←  Wstecz", BS_OWNERDRAW, 270, 78, 105, 36, back_id));
    app_controls.push_back(control(window, WC_BUTTONW, L"↻  Odśwież", BS_OWNERDRAW, 387, 78, 112, 36, refresh_id));
    app_controls.push_back(control(window, WC_BUTTONW, L"＋  Wyślij plik", BS_OWNERDRAW, 511, 78, 132, 36, upload_id));
    app_controls.push_back(control(window, WC_BUTTONW, L"Kosz", BS_OWNERDRAW, 54, 188, 136, 42, trash_id));
    app_controls.push_back(control(window, WC_BUTTONW, L"Wyloguj", BS_OWNERDRAW, 940, 35, 104, 34, logout_id));
    list_view = control(window, WC_LISTVIEWW, L"", LVS_ICON | LVS_AUTOARRANGE | LVS_SINGLESEL |
                        LVS_SHOWSELALWAYS | LVS_EDITLABELS,
                        270, 160, 774, 450, files_id);
    ListView_SetExtendedListViewStyle(list_view, LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT);
    ListView_SetBkColor(list_view, paper); ListView_SetTextBkColor(list_view, paper);
    ListView_SetTextColor(list_view, ink);
    tile_images = ImageList_Create(64, 64, ILC_COLOR32, 2, 1);
    HBITMAP folder_image = tile_bitmap(window, true);
    HBITMAP file_image = tile_bitmap(window, false);
    ImageList_Add(tile_images, folder_image, nullptr); ImageList_Add(tile_images, file_image, nullptr);
    DeleteObject(folder_image); DeleteObject(file_image);
    ListView_SetImageList(list_view, tile_images, LVSIL_NORMAL);
    ListView_SetIconSpacing(list_view, 154, 112);
    app_controls.push_back(list_view);
    status_label = control(window, WC_STATICW, L"Zaloguj się, aby zobaczyć pliki.", SS_CENTER,
                           46, 300, 150, 110, status_id);
    app_controls.push_back(status_label);
    show_app(false);
    layout(window);
}

LRESULT CALLBACK window_proc(HWND window, UINT message_id, WPARAM wparam, LPARAM lparam) {
    switch (message_id) {
    case WM_CREATE: create_controls(window); return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case login_id: login(window); break;
        case refresh_id: refresh(window); break;
        case back_id: client.go_back(); refresh(window); break;
        case upload_id: upload_file(window); break;
        case folder_id: create_folder(window); break;
        case rename_id: rename_selected(window); break;
        case files_id: open_selected(window); break;
        case trash_id: trash_selected(window); break;
        case logout_id: client.clear_session(); show_app(false); message(window, L"Zaloguj się, aby zobaczyć pliki."); break;
        } return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lparam)->idFrom == files_id) {
            const auto code = reinterpret_cast<NMHDR*>(lparam)->code;
            if (code == NM_DBLCLK) open_selected(window);
            if (code == LVN_ENDLABELEDITW) {
                const auto info = reinterpret_cast<NMLVDISPINFOW*>(lparam);
                if (info->item.pszText && *info->item.pszText && info->item.iItem >= 0) {
                    try {
                        static_cast<void>(transport.send(client.rename_request(
                            files[static_cast<size_t>(info->item.iItem)].path,
                            narrow(info->item.pszText))));
                        refresh(window);
                        return TRUE;
                    } catch (...) { message(window, L"Nie udało się zmienić nazwy.", true); }
                }
            }
        }
        return 0;
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wparam) == list_view) {
            HMENU menu = CreatePopupMenu();
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            POINT client_point = point; ScreenToClient(list_view, &client_point);
            LVHITTESTINFO hit{}; hit.pt = client_point;
            const int hit_index = ListView_HitTest(list_view, &hit);
            if (hit_index >= 0) {
                ListView_SetItemState(list_view, hit_index, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                if (files[static_cast<size_t>(hit_index)].directory)
                    AppendMenuW(menu, MF_STRING, files_id, L"Otwórz");
                AppendMenuW(menu, MF_STRING, rename_id, L"Zmień nazwę");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, trash_id, L"Usuń");
            } else {
                AppendMenuW(menu, MF_STRING, folder_id, L"Utwórz nowy folder");
                AppendMenuW(menu, MF_STRING, upload_id, L"Wyślij plik tutaj");
            }
            if (point.x == -1 && point.y == -1) {
                RECT list_rect{}; GetWindowRect(list_view, &list_rect);
                point = {list_rect.left + 30, list_rect.top + 30};
            }
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    case WM_SIZE:
        if (list_view) layout(window);
        return 0;
    case WM_DRAWITEM:
        draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        paint_background(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), ink);
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), ink);
        SetBkColor(reinterpret_cast<HDC>(wparam), paper);
        return reinterpret_cast<LRESULT>(edit_brush);
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message_id, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    Gdiplus::GdiplusStartupInput gdiplus_input;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    const auto texture_path = filesystem::path(executable).parent_path() / L"walnut-background-v1.png";
    wood_texture = make_unique<Bitmap>(texture_path.c_str());
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    normal_font = CreateFontW(19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Segoe UI");
    title_font = CreateFontW(29, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    background_brush = CreateSolidBrush(wood_dark);
    panel_brush = CreateSolidBrush(ivory);
    edit_brush = CreateSolidBrush(paper);
    WNDCLASSW klass{};
    klass.hInstance = instance; klass.lpfnWndProc = window_proc; klass.lpszClassName = window_class;
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW); klass.hbrBackground = background_brush;
    klass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&klass);
    HWND window = CreateWindowExW(0, window_class, L"HomeCloud", WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700, nullptr, nullptr,
                                  instance, nullptr);
    if (!window) {
        MessageBoxW(nullptr, L"Nie udało się utworzyć okna HomeCloud.", L"HomeCloud",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    ShowWindow(window, show == SW_HIDE ? SW_SHOWNORMAL : show);
    SetForegroundWindow(window);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) { TranslateMessage(&message); DispatchMessageW(&message); }
    DeleteObject(normal_font); DeleteObject(title_font); DeleteObject(background_brush);
    DeleteObject(panel_brush); DeleteObject(edit_brush);
    wood_texture.reset();
    if (tile_images) ImageList_Destroy(tile_images);
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return static_cast<int>(message.wParam);
}
