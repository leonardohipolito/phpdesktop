// Copyright (c) 2012-2014 PHP Desktop Authors. All rights reserved.
// License: New BSD License.
// Website: http://code.google.com/p/phpdesktop/

#include "../defines.h"
#include "browser_window.h"

#include <map>
#include <string>

#include "../executable.h"
#include "../log.h"
#include "../settings.h"
#include "../string_utils.h"
#include "../fatal_error.h"
#include "../file_utils.h"
#include "../window_utils.h"

std::map<HWND, BrowserWindow*> g_browserWindows;
extern std::string g_webServerUrl;
extern wchar_t g_windowClassName[256];

BrowserWindow* GetBrowserWindow(HWND hwnd) {
    std::map<HWND, BrowserWindow*>::iterator it;
    it = g_browserWindows.find(hwnd);
    if (it != g_browserWindows.end()) {
        return it->second;
    }
    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner) {
        // hwnd is CEF host handle. 
        // This condition is for popups.
        it = g_browserWindows.find(owner);
        if (it != g_browserWindows.end()) {
            return it->second;
        }
    }
    HWND parent = GetParent(hwnd);
    if (parent) {
        // This condition is for main window.
        it = g_browserWindows.find(parent);
        if (it != g_browserWindows.end()) {
            return it->second;
        }
    }
    // GetBrowserWindow() may fail during window creation, so log
    // severity is only DEBUG.
    LOG_DEBUG << "GetBrowserWindow(): not found, hwnd = " << (int)hwnd;
    return NULL;
}
void StoreBrowserWindow(HWND hwnd, BrowserWindow* browser) {
    LOG_DEBUG << "StoreBrowserWindow(): hwnd = " << (int)hwnd;
    std::map<HWND, BrowserWindow*>::iterator it;
    it = g_browserWindows.find(hwnd);
    if (it == g_browserWindows.end()) {
        g_browserWindows[hwnd] = browser;
    } else {
        LOG_WARNING << "StoreBrowserWindow() failed: already stored";
    }
}
void RemoveBrowserWindow(HWND hwnd) {
    LOG_DEBUG << "RemoveBrowserWindow(): hwnd = " << (int)hwnd;
    std::map<HWND, BrowserWindow*>::iterator it;
    it = g_browserWindows.find(hwnd);
    if (it != g_browserWindows.end()) {
        BrowserWindow* browser = it->second;
        g_browserWindows.erase(it);
        delete browser;
    } else {
        LOG_WARNING << "RemoveBrowserWindow() failed: not found";
    }
}

BrowserWindow::BrowserWindow(HWND inWindowHandle, bool isPopup) 
        : windowHandle_(inWindowHandle),
          isPopup_(isPopup){      
    _ASSERT(windowHandle_);

    SetTitleFromSettings();
    SetIconFromSettings();

    if (IsPopup()) {
        LOG_DEBUG << "BrowserWindow::BrowserWindow() created for Popup";
    } else {
        if (!CreateBrowserControl(Utf8ToWide(g_webServerUrl).c_str())) {
            FatalError(windowHandle_, "Could not create Browser control.\n"
                    "Exiting application.");
        }
    }
}
BrowserWindow::~BrowserWindow() {
    LOG_DEBUG << "BrowserWindow::~BrowserWindow() destroyed";
}
CefRefPtr<CefBrowser> BrowserWindow::GetCefBrowser() {
    return cefBrowser_;
}
void BrowserWindow::SetCefBrowser(CefRefPtr<CefBrowser> cefBrowser) {
    // Called from ClientHandler::OnAfterCreated().
    cefBrowser_ = cefBrowser;
    // OnSize was called from WM_SIZE, but cefBrowser_ was not yet
    // set, so the window wasn't yet positioned correctly.
    this->OnSize();
}
bool BrowserWindow::CreateBrowserControl(const wchar_t* navigateUrl) {
    LOG_DEBUG << "BrowserWindow::CreateBrowserControl()";
    // This is called only for the main window.
    // Popup cef browsers are created internally by CEF,
    // see OnBeforePopup, OnAfterCreated.
    json_value* settings = GetApplicationSettings();
    
    RECT rect;
    BOOL b = GetWindowRect(windowHandle_, &rect);
    if (!b) {
        LOG_ERROR << "GetWindowRect() failed in "
                     "BrowserWindow::CreateBrowserControl()";
    }

    // Information used when creating the native window.
    CefWindowInfo window_info;
    window_info.SetAsChild(windowHandle_, rect);
    // SimpleHandler implements browser-level callbacks.
    CefRefPtr<ClientHandler> handler(new ClientHandler());
    // Specify CEF browser settings here.
    CefBrowserSettings browser_settings;
    // Create the first browser window.
    CefBrowserHost::CreateBrowser(window_info, handler.get(), g_webServerUrl,
                                      browser_settings, NULL);

    return true;
}
HWND BrowserWindow::GetWindowHandle() {
    _ASSERT(windowHandle_);
    return windowHandle_;
}
void BrowserWindow::SetTitle(const wchar_t* title) {
    BOOL b = SetWindowText(windowHandle_, title);
    _ASSERT(b);
}
bool BrowserWindow::IsPopup() {
    return isPopup_;
}
bool BrowserWindow::IsUsingMetaTitle() {
    if (IsPopup()) {
        json_value* settings = GetApplicationSettings();
        std::string fixed_title = (*settings)["popup_window"]["fixed_title"];
        return fixed_title.empty();
    }
    return false;
}
void BrowserWindow::OnGetMinMaxInfo(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (!IsPopup()) {
        json_value* settings = GetApplicationSettings();
        static long minimum_width = 
                (*settings)["main_window"]["minimum_size"][0];
        static long minimum_height = 
                (*settings)["main_window"]["minimum_size"][1];
        static long maximum_width = 
                (*settings)["main_window"]["maximum_size"][0];
        static long maximum_height = 
                (*settings)["main_window"]["maximum_size"][1];
        MINMAXINFO* pMMI = (MINMAXINFO*)lParam;
        if (minimum_width)
            pMMI->ptMinTrackSize.x = minimum_width;
        if (minimum_height)
            pMMI->ptMinTrackSize.y = minimum_height;
        if (maximum_width)        
            pMMI->ptMaxTrackSize.x = maximum_width;
        if (maximum_height)
            pMMI->ptMaxTrackSize.y = maximum_height;
    }
}
void BrowserWindow::OnSize() {
    if (cefBrowser_) {
        RECT rect;
        GetClientRect(windowHandle_, &rect);
        HDWP hdwp = BeginDeferWindowPos(1);
        CefWindowHandle cefHwnd = cefBrowser_->GetHost()->GetWindowHandle();
        hdwp = DeferWindowPos(hdwp, cefHwnd, NULL,
                rect.left, rect.top,
                rect.right - rect.left,
                rect.bottom - rect.top,
                SWP_NOZORDER);
        EndDeferWindowPos(hdwp);
    } else {
        LOG_WARNING << "BrowserWindow::OnSize() failed: "
                       "CefBrowser object not created yet";
    }
}
void BrowserWindow::SetTitleFromSettings() {
    if (IsPopup()) {
        json_value* settings = GetApplicationSettings();
        std::wstring popup_title = (*settings)["popup_window"]["fixed_title"];
        if (popup_title.empty())
            popup_title = (*settings)["main_window"]["title"];
        if (popup_title.empty())
            popup_title = Utf8ToWide(GetExecutableName());
        SetTitle(popup_title.c_str());
    }
    // Main window title is set in CreateMainWindow().
}
void BrowserWindow::SetIconFromSettings() {
    json_value* settings = GetApplicationSettings();
    const char* iconPath;
    if (IsPopup())
        iconPath = (*settings)["popup_window"]["icon"];
    else 
        iconPath = (*settings)["main_window"]["icon"];
    if (iconPath && iconPath[0] != 0) {
        wchar_t iconPathW[MAX_PATH];
        Utf8ToWide(iconPath, iconPathW, _countof(iconPathW));

        int bigX = GetSystemMetrics(SM_CXICON);
        int bigY = GetSystemMetrics(SM_CYICON);
        HANDLE bigIcon = LoadImage(0, iconPathW, IMAGE_ICON, bigX, bigY, 
                                   LR_LOADFROMFILE);
        if (bigIcon) {
            SendMessage(windowHandle_, WM_SETICON, ICON_BIG, (LPARAM)bigIcon);
        } else {
            LOG_WARNING << "Setting icon from settings file failed "
                           "(ICON_BIG)";
        }
        int smallX = GetSystemMetrics(SM_CXSMICON);
        int smallY = GetSystemMetrics(SM_CYSMICON);
        HANDLE smallIcon = LoadImage(0, iconPathW, IMAGE_ICON, smallX, 
                                     smallY, LR_LOADFROMFILE);
        if (smallIcon) {
            SendMessage(windowHandle_, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
        } else {
            LOG_WARNING << "Setting icon from settings file failed "
                           "(ICON_SMALL)";
        }
    }
}
bool BrowserWindow::SetFocus() {
    // Calling SetFocus() on shellBrowser handle does not work.
    if (cefBrowser_.get()) {
        cefBrowser_->GetHost()->SetFocus(true);
	}
    return true;
}
