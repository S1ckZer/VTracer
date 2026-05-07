#include "AppWindow.h"

#include <windows.h>
#include <combaseapi.h>

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int showCmd) {
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    int rc = 0;
    {
        vtracer::AppWindow app;
        rc = app.run(inst, showCmd);
    }

    ::CoUninitialize();
    return rc;
}
