# Windows client

The desktop client is a native C++20 Win32 shell with the HomeCloud interface
embedded through Microsoft's WebView2 Runtime. It uses the exact same interface
as Android and the browser, so thumbnails, photo gallery navigation, nested
folders, rename/delete actions, uploads, downloads and the 30-minute remembered
session stay in sync across devices.

Before building, replace `https://your-homecloud.example/` with your own secured
HomeCloud address. WebView2 Runtime
is included with Windows 11 and current Windows 10 installations.
