// 1337clicker.cpp
//
// Reconstructed from 1337clicker.exe
// sha256 46b4055beda0c47cf105d64ea0006638935e87a67e96eece53e9bd787869d346
//
// Function and global names come from the exe's symbol table, which calls the
// source file 1227clicker.cpp. Helpers the compiler inlined have no symbol, so
// Scale, Fill, Label, KeyName, ProcessName, Lower, SendMouse, BuildLayout,
// the Layout field names and the colour names are mine.
//
// Build: see build.sh

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <random>
#include <sstream>
#include <string>
#include <thread>

using namespace std::chrono;
using namespace std::chrono_literals;

enum : UINT {
    WM_APP_TOGGLE_ARMED = WM_APP + 1,
    WM_APP_TOGGLE_HIDE = WM_APP + 2,
};

enum Control {
    CtlNone = 0,
    CtlMinimize = 1,
    CtlClose = 2,
    CtlSlider = 3,
    CtlInventory = 4,
    CtlHotkey = 6,  // 5 is unused in the exe
};

static constexpr int kMinCps = 1;
static constexpr int kMaxCps = 50;
static constexpr int kInventoryCps = 40;
static constexpr int kDefaultHotkey = VK_F6;
static constexpr int kHideKey = VK_F8;

static constexpr COLORREF kBackground = RGB(19, 19, 23);
static constexpr COLORREF kTitleBar = RGB(26, 26, 31);
static constexpr COLORREF kEdge = RGB(5, 5, 7);
static constexpr COLORREF kText = RGB(228, 230, 235);
static constexpr COLORREF kMuted = RGB(148, 152, 160);
static constexpr COLORREF kDim = RGB(110, 114, 122);
static constexpr COLORREF kHover = RGB(38, 38, 45);
static constexpr COLORREF kTrack = RGB(44, 44, 52);
static constexpr COLORREF kAccent = RGB(96, 165, 250);
static constexpr COLORREF kCloseHover = RGB(200, 60, 60);
static constexpr COLORREF kKnob = RGB(210, 213, 219);
static constexpr COLORREF kKnobOff = RGB(120, 123, 130);

struct Layout {
    int w, h;
    RECT title;
    RECT minimize;
    RECT close;
    RECT label;
    RECT value;
    RECT slider;
    RECT inventory;
    RECT hotkey;
    RECT accent;
};

// Cached answer to "is the foreground window Lunar or Minecraft?"
struct LunarGate {
    HWND hwnd;
    DWORD pid;
    DWORD checkedAt;
    bool ok;
};

static Layout L;
static int g_dpi = 96;

static std::atomic<int> g_hotkey{kDefaultHotkey};
static std::atomic<bool> g_inventoryMode{true};
static std::atomic<int> g_cps{20};
static std::atomic<bool> g_captureHotkey{false};
static std::atomic<bool> g_physicalLeft{false};
static std::atomic<bool> g_armed{false};
static std::atomic<bool> g_quit{false};

static int g_hoverCtl = CtlNone;
static POINT g_dragOrigin;
static bool g_dragWindow = false;
static bool g_dragSlider = false;
static HHOOK g_mouseHook;
static HFONT g_fTiny, g_fValue, g_fLabel, g_fTitle;
static int g_bufH, g_bufW;
static HGDIOBJ g_oldBmp;
static HBITMAP g_memBmp;
static HDC g_memDC;
static HWND g_hWnd;

static int Scale(int v) {
    return MulDiv(v, g_dpi, 96);
}

static int CpsFromX(int x) {
    int x0 = L.slider.left + Scale(6);
    int x1 = L.slider.right - Scale(6);
    x = std::min(std::max(x, x0), x1);
    int cps = 1 + (int)((long long)(x - x0) * (kMaxCps - kMinCps) / (x1 - x0));
    return std::max(std::min(cps, kMaxCps), kMinCps);
}

static int ControlAt(POINT pt) {
    if (PtInRect(&L.minimize, pt)) return CtlMinimize;
    if (PtInRect(&L.close, pt)) return CtlClose;

    RECT slider = L.slider;
    slider.left -= Scale(6);
    slider.right += Scale(6);
    slider.top -= Scale(9);
    slider.bottom += Scale(9);
    if (PtInRect(&slider, pt)) return CtlSlider;

    if (PtInRect(&L.inventory, pt)) return CtlInventory;
    return PtInRect(&L.hotkey, pt) ? CtlHotkey : CtlNone;
}

// Tracks the real left button. Injected events are skipped, so the clicker's
// own SendInput calls never change g_physicalLeft.
static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (!info || !(info->flags & LLMHF_INJECTED)) {
            if (wParam == WM_LBUTTONDOWN) g_physicalLeft.store(true, std::memory_order_relaxed);
            else if (wParam == WM_LBUTTONUP) g_physicalLeft.store(false, std::memory_order_relaxed);
        }
    }
    return CallNextHookEx(g_mouseHook, code, wParam, lParam);
}

static HFONT MakeFont(const std::wstring& face, int size, int weight) {
    return CreateFontW(-Scale(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 0, face.c_str());
}

static void CreateFonts() {
    static const wchar_t* ui[] = {L"Microsoft YaHei UI", L"Microsoft YaHei", L"Segoe UI"};

    HDC dc = GetDC(nullptr);
    std::wstring face = ui[2];
    for (const wchar_t* name : ui) {
        HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 0, name);
        if (!font) continue;

        HGDIOBJ old = SelectObject(dc, font);
        wchar_t actual[64] = {};
        GetTextFaceW(dc, 64, actual);
        SelectObject(dc, old);
        DeleteObject(font);

        if (_wcsicmp(actual, name) == 0) {
            face = name;
            break;
        }
    }
    ReleaseDC(nullptr, dc);

    g_fTitle = MakeFont(face, 13, FW_SEMIBOLD);
    g_fLabel = MakeFont(face, 11, FW_NORMAL);
    g_fValue = MakeFont(face, 20, FW_SEMIBOLD);
    g_fTiny = MakeFont(face, 9, FW_NORMAL);
}

// Toggles armed on a hotkey press, and hides or shows the window when F8 is
// held for 500 ms.
static void KeyWatchThread() {
    int lastKey = kDefaultHotkey;
    bool lastDown = false;
    bool hideHeld = false;
    bool hideFired = false;
    steady_clock::time_point hideStart{};

    while (!g_quit) {
        int key = g_hotkey;
        bool down = (GetAsyncKeyState(key) & 0x8000) != 0;

        if (!g_captureHotkey && !(lastDown && lastKey == key) && down)
            PostMessageW(g_hWnd, WM_APP_TOGGLE_ARMED, 0, 0);

        if (GetAsyncKeyState(kHideKey) & 0x8000) {
            if (!hideHeld) {
                hideStart = steady_clock::now();
                hideHeld = true;
                hideFired = false;
            } else if (!hideFired && steady_clock::now() - hideStart >= 500ms) {
                PostMessageW(g_hWnd, WM_APP_TOGGLE_HIDE, 0, 0);
                hideFired = true;
            }
        } else {
            hideHeld = false;
            hideFired = false;
        }

        std::this_thread::sleep_for(12ms);
        lastKey = key;
        lastDown = down;
    }
}

static std::wstring ProcessName(DWORD pid) {
    std::wstring name;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return name;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == pid) {
            name = entry.szExeFile;
            break;
        }
    }
    CloseHandle(snapshot);
    return name;
}

static std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

// True when the foreground window is lunarclient.exe, or java/javaw with a
// title containing "lunar", "1.7.10" or "1.8.9". Checked at most every 250 ms
// per window. The clicker's own window never counts.
static bool LunarInForeground(LunarGate& gate) {
    DWORD now = GetTickCount();
    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == g_hWnd) return false;

    if (gate.hwnd != foreground) {
        gate.hwnd = foreground;
        gate.ok = false;
        gate.checkedAt = 0;
        GetWindowThreadProcessId(foreground, &gate.pid);
    }
    if (gate.checkedAt && now - gate.checkedAt < 250) return gate.ok;
    gate.checkedAt = now;

    std::wstring exe = Lower(ProcessName(gate.pid));
    if (exe == L"lunarclient.exe") {
        gate.ok = true;
        return true;
    }
    if (exe != L"javaw.exe" && exe != L"java.exe") {
        gate.ok = false;
        return false;
    }

    wchar_t buffer[512] = {};
    GetWindowTextW(foreground, buffer, 512);
    std::wstring title = Lower(buffer);
    bool ok = title.find(L"lunar") != std::wstring::npos ||
              title.find(L"1.7.10") != std::wstring::npos ||
              title.find(L"1.8.9") != std::wstring::npos;
    gate.ok = ok;
    return ok;
}

static void SendMouse(DWORD flags) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    SendInput(1, &input, sizeof(input));
}

// While armed and the real left button is held in Lunar, each click is an
// injected up, 1 ms, then an injected down, so the button ends each click held.
// With inventory mode on and left shift held, the rate is fixed at 40 CPS.
static void ClickerThread() {
    timeBeginPeriod(1);

    std::mt19937 rng((unsigned)GetTickCount64());
    LunarGate gate{};
    auto next = steady_clock::now();

    while (!g_quit) {
        bool physical = g_physicalLeft;
        bool shift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
        bool inventory = g_inventoryMode;
        bool lunar = LunarInForeground(gate);

        if (!g_armed || !physical || !lunar) {
            std::this_thread::sleep_for(2ms);
            continue;
        }

        auto now = steady_clock::now();
        if (now < next) {
            long long left = duration_cast<microseconds>(next - now).count();
            if (left > 2500)
                std::this_thread::sleep_for(microseconds(left - 2000));
            else if (left > 1200)
                std::this_thread::sleep_for(500us);
            else
                std::this_thread::yield();
            continue;
        }

        SendMouse(MOUSEEVENTF_LEFTUP);
        std::this_thread::sleep_for(1ms);

        if (g_quit || !g_armed || !g_physicalLeft) continue;

        int cps;
        if (shift && inventory) {
            if (!(GetAsyncKeyState(VK_LSHIFT) & 0x8000)) continue;
            if (!LunarInForeground(gate)) continue;
            SendMouse(MOUSEEVENTF_LEFTDOWN);
            cps = kInventoryCps;
        } else {
            if (!LunarInForeground(gate)) continue;
            SendMouse(MOUSEEVENTF_LEFTDOWN);
            cps = g_cps;
        }

        // The only randomness: each interval uses cps or cps - 1.
        cps -= (int)(rng() & 1);
        if (cps < 1) cps = 1;

        nanoseconds step = microseconds((long long)(1000000.0 / cps));
        next += step;
        if (next < now) next = now + step;
    }

    timeEndPeriod(1);
}

static void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

static void Label(HDC dc, HFONT font, int x, int y, const std::wstring& text, COLORREF color) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, text.c_str(), (int)text.size());
    SelectObject(dc, old);
}

static std::wstring KeyName(int vk) {
    wchar_t name[64] = {};
    LONG lParam = (LONG)(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
    if ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE)
        lParam |= 1 << 24;  // extended key
    if (GetKeyNameTextW(lParam, name, 64) > 0) return name;
    return L"Key";
}

static void PaintInto(HDC dc, int w, int h) {
    Fill(dc, RECT{0, 0, w, h}, kBackground);
    Fill(dc, L.title, kTitleBar);
    Fill(dc, RECT{0, L.title.bottom - 1, w, L.title.bottom}, kEdge);
    Label(dc, g_fTitle, Scale(13), Scale(8), L"1337clicker", kText);

    // Minimize button
    bool minHover = g_hoverCtl == CtlMinimize;
    Fill(dc, L.minimize, minHover ? kHover : kTitleBar);
    int minY = L.minimize.top + Scale(6);
    Fill(dc, RECT{L.minimize.left + Scale(4), minY, L.minimize.right - Scale(4), minY + 1},
         minHover ? kText : kMuted);

    // Close button
    bool closeHover = g_hoverCtl == CtlClose;
    Fill(dc, L.close, closeHover ? kCloseHover : kTitleBar);
    HPEN closePen = CreatePen(PS_SOLID, Scale(1), closeHover ? RGB(255, 255, 255) : kMuted);
    HGDIOBJ oldPen = SelectObject(dc, closePen);
    int d = Scale(4);
    int closeY = (L.close.top + L.close.bottom) / 2;
    MoveToEx(dc, L.close.left + d, closeY - d, nullptr);
    LineTo(dc, L.close.right - d, closeY + d);
    MoveToEx(dc, L.close.right - d, closeY - d, nullptr);
    LineTo(dc, L.close.left + d, closeY + d);
    SelectObject(dc, oldPen);
    DeleteObject(closePen);

    int cps = g_cps;
    bool armed = g_armed;
    bool inventory = g_inventoryMode;
    COLORREF accent = armed ? kAccent : kTrack;

    // Stripe on the left edge shows the armed state
    Fill(dc, L.accent, accent);

    Label(dc, g_fLabel, L.label.left, L.label.top, L"clicks per second", kMuted);

    std::wstringstream value;
    value << cps;
    Label(dc, g_fValue, L.value.left, L.value.top - Scale(2), value.str(), armed ? kText : kDim);

    // Slider
    int sliderY = (L.slider.top + L.slider.bottom) / 2;
    int x0 = L.slider.left + Scale(6);
    int x1 = L.slider.right - Scale(6);
    int knobX = x0 + (int)((long long)(x1 - x0) * (cps - 1) / (kMaxCps - kMinCps));

    Fill(dc, RECT{L.slider.left, sliderY - Scale(2), L.slider.right, sliderY + Scale(2)}, kTrack);
    Fill(dc, RECT{L.slider.left, sliderY - Scale(2), knobX, sliderY + Scale(2)}, accent);

    int radius = Scale(7);
    COLORREF knob = kKnobOff;
    if (armed) knob = (g_hoverCtl == CtlSlider || g_dragSlider) ? kText : kKnob;
    HBRUSH knobBrush = CreateSolidBrush(knob);
    HGDIOBJ oldKnobPen = SelectObject(dc, GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(dc, knobBrush);
    Ellipse(dc, knobX - radius, sliderY - radius, knobX + radius, sliderY + radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldKnobPen);
    DeleteObject(knobBrush);

    // Inventory checkbox
    RECT box = L.inventory;
    COLORREF boxText;
    if (inventory) {
        Fill(dc, box, kAccent);
        HPEN tick = CreatePen(PS_SOLID, Scale(1), kText);
        HGDIOBJ oldTick = SelectObject(dc, tick);
        MoveToEx(dc, box.left + Scale(3), box.top + Scale(6), nullptr);
        LineTo(dc, box.left + Scale(6), box.bottom - Scale(3));
        LineTo(dc, box.right - Scale(3), box.top + Scale(3));
        SelectObject(dc, oldTick);
        DeleteObject(tick);
        boxText = kMuted;
    } else {
        Fill(dc, box, g_hoverCtl == CtlInventory ? kHover : kTrack);
        boxText = kDim;
    }
    Label(dc, g_fTiny, box.right + Scale(7), box.top, L"clicking in inventory", boxText);

    // Hotkey row
    bool capturing = g_captureHotkey;
    std::wstring hotkey = capturing ? std::wstring(L"Press a key (Esc/Del resets)")
                                    : L"Hotkey: " + KeyName(g_hotkey);
    Label(dc, g_fTiny, L.hotkey.left, L.hotkey.top, hotkey, capturing ? kText : kMuted);

    // Window border
    Fill(dc, RECT{0, 0, w, Scale(1)}, kEdge);
    Fill(dc, RECT{0, h - Scale(1), w, h}, kEdge);
    Fill(dc, RECT{0, 0, Scale(1), h}, kEdge);
    Fill(dc, RECT{w - Scale(1), 0, w, h}, kEdge);
}

static void SetCps(HWND hwnd, int cps) {
    g_cps = std::max(std::min(cps, kMaxCps), kMinCps);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void SetTaskbarVisible(HWND hwnd, bool visible) {
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    style &= ~(LONG_PTR)(WS_EX_APPWINDOW | WS_EX_TOOLWINDOW);
    style |= visible ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void SetHoverCursor(POINT pt, int control) {
    LPCWSTR cursor = IDC_ARROW;
    if (control == CtlNone && pt.y < L.title.bottom) cursor = IDC_SIZEALL;
    SetCursor(LoadCursorW(nullptr, cursor));
}

static void DragWindowTo(HWND hwnd, int x, int y) {
    RECT window;
    GetWindowRect(hwnd, &window);
    int left = window.left + x - g_dragOrigin.x;
    int top = window.top + y - g_dragOrigin.y;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    int width = window.right - window.left;
    int height = window.bottom - window.top;
    left = std::max(left, (int)work.left);
    top = std::max(top, (int)work.top);
    if (left + width > work.right) left = work.right - width;
    if (top + height > work.bottom) top = work.bottom - height;

    SetWindowPos(hwnd, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    RECT client;
    GetClientRect(hwnd, &client);
    int w = client.right;
    int h = client.bottom;

    if (w > 0 && h > 0) {
        if (!g_memDC || w != g_bufW || h != g_bufH) {
            if (g_memDC) {
                SelectObject(g_memDC, g_oldBmp);
                DeleteObject(g_memBmp);
                DeleteDC(g_memDC);
            }
            g_memDC = CreateCompatibleDC(dc);
            g_memBmp = CreateCompatibleBitmap(dc, w, h);
            g_oldBmp = SelectObject(g_memDC, g_memBmp);
            g_bufW = w;
            g_bufH = h;
        }

        HRGN clip = CreateRectRgn(0, 0, w, h);
        SelectClipRgn(g_memDC, clip);
        DeleteObject(clip);

        PaintInto(g_memDC, w, h);
        BitBlt(dc, 0, 0, w, h, g_memDC, 0, 0, SRCCOPY);
    }

    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 100, nullptr);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_mouseHook) {
            UnhookWindowsHookEx(g_mouseHook);
            g_mouseHook = nullptr;
        }
        g_quit = true;
        PostQuitMessage(0);
        return 0;

    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_TIMER:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_NCHITTEST:
        return HTCLIENT;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetHoverCursor(pt, ControlAt(pt));
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);

        if (g_dragSlider) {
            SetCps(hwnd, CpsFromX(x));
            return 0;
        }
        if (g_dragWindow && (wParam & MK_LBUTTON)) {
            DragWindowTo(hwnd, x, y);
            return 0;
        }

        POINT pt{x, y};
        int control = ControlAt(pt);
        if (control != g_hoverCtl) {
            g_hoverCtl = control;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        SetHoverCursor(pt, control);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt{(short)LOWORD(lParam), (short)HIWORD(lParam)};

        if (PtInRect(&L.close, pt)) {
            DestroyWindow(hwnd);
        } else if (PtInRect(&L.minimize, pt)) {
            ShowWindow(hwnd, SW_MINIMIZE);
        } else if (ControlAt(pt) == CtlSlider) {
            g_dragSlider = true;
            SetCapture(hwnd);
            SetCps(hwnd, CpsFromX(pt.x));
        } else if (PtInRect(&L.inventory, pt)) {
            g_inventoryMode = !g_inventoryMode;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (PtInRect(&L.hotkey, pt)) {
            g_captureHotkey = true;
            SetFocus(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (pt.y < L.title.bottom) {
            g_dragOrigin = pt;
            g_dragWindow = true;
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_dragSlider || g_dragWindow) {
            g_dragSlider = false;
            g_dragWindow = false;
            ReleaseCapture();
        }
        return 0;

    case WM_MOUSEWHEEL:
        SetCps(hwnd, g_cps + GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
        return 0;

    case WM_KEYDOWN:
        if (g_captureHotkey) {
            bool reset = wParam == VK_ESCAPE || wParam == VK_DELETE;
            g_hotkey = reset ? kDefaultHotkey : (int)wParam;
            g_captureHotkey = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        } else if (wParam == VK_LEFT || wParam == VK_RIGHT) {
            SetCps(hwnd, g_cps + (wParam == VK_RIGHT ? 1 : -1));
        }
        return 0;

    case WM_APP_TOGGLE_ARMED:
        g_armed = !g_armed;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_APP_TOGGLE_HIDE:
        if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
            SetTaskbarVisible(hwnd, false);
            ShowWindow(hwnd, SW_HIDE);
        } else {
            SetTaskbarVisible(hwnd, true);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void BuildLayout() {
    L.w = Scale(238);
    L.h = Scale(156);
    L.title = RECT{0, 0, L.w, Scale(30)};
    L.close = RECT{L.w - Scale(39), Scale(9), L.w - Scale(13), Scale(22)};
    L.minimize = RECT{L.close.left - Scale(26), Scale(9), L.close.left - Scale(6), Scale(22)};
    L.label = RECT{Scale(14), Scale(38), L.w - Scale(14), Scale(54)};
    L.value = RECT{Scale(14), Scale(54), L.w - Scale(14), Scale(80)};
    L.slider = RECT{Scale(14), Scale(88), L.w - Scale(14), Scale(94)};
    L.inventory = RECT{Scale(14), Scale(104), Scale(27), Scale(117)};
    L.hotkey = RECT{Scale(14), Scale(130), L.w - Scale(14), Scale(148)};
    L.accent = RECT{0, Scale(30), Scale(3), L.h};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    static const wchar_t kClass[] = L"SlinkyClickerWnd";

    // A second copy brings the first one back and exits.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SlinkyClicker41");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kClass, nullptr)) {
            SetTaskbarVisible(existing, true);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    SetProcessDPIAware();
    HDC screen = GetDC(nullptr);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);

    BuildLayout();
    CreateFonts();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = std::max(work.left + (work.right - work.left - L.w) / 2, work.left);
    int y = work.top + (work.bottom - work.top - L.h) / 3;

    g_hWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, kClass, L"1337clicker", WS_POPUP,
                             x, y, L.w, L.h, nullptr, nullptr, instance, nullptr);
    if (!g_hWnd) return 1;

    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);

    g_physicalLeft = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, instance, 0);

    std::thread clicker(ClickerThread);
    std::thread keyWatch(KeyWatchThread);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_quit = true;
    clicker.join();
    keyWatch.join();

    if (g_memDC) {
        SelectObject(g_memDC, g_oldBmp);
        DeleteObject(g_memBmp);
        DeleteDC(g_memDC);
    }
    DeleteObject(g_fTitle);
    DeleteObject(g_fLabel);
    DeleteObject(g_fValue);
    DeleteObject(g_fTiny);
    if (mutex) CloseHandle(mutex);
    return 0;
}
