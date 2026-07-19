/*
DirectInput8 Proxy DLL - Moboalien Input Suite

This proxy DLL intercepts DirectInput8 API calls to inject keyboard/mouse input
from shared memory. It works by wrapping COM interfaces to intercept device
state queries.

How it works:
1. Game calls DirectInput8Create() → intercepted by this proxy
2. Proxy creates wrapper objects around IDirectInput8 and IDirectInputDevice8
3. When game calls GetDeviceState() → wrapper injects shared memory data
4. Original device state is preserved and enhanced with injected input

This enables remote keyboard/mouse input injection for games using DirectInput.

Architecture:
- DirectInput8Create: Entry point, wraps IDirectInput8 interface
- DirectInput8Wrapper: Wraps IDirectInput8, intercepts CreateDevice calls
- DirectInputDevice8Wrapper: Wraps IDirectInputDevice8, intercepts GetDeviceState

Notes:
- Uses ANSI version (IDirectInput8A) for broader compatibility
- Loads real dinput8.dll from system32 to avoid recursion
- Minimal error handling for performance
*/

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <memory>
#include "shared_input.h"

// Handle to the real dinput8.dll loaded from system32
static HMODULE real_dinput8 = NULL;

// Function pointer type for DirectInput8Create
typedef HRESULT (WINAPI *DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t real_DirectInput8Create = nullptr;

/*
DirectInputDevice8Wrapper - Intercepts device input queries

This wrapper class implements the IDirectInputDevice8A interface and forwards
most calls to the real device, but intercepts GetDeviceState to inject input
from shared memory.

Key method: GetDeviceState() - where input injection happens
*/
class DirectInputDevice8Wrapper : public IDirectInputDevice8A {
    IDirectInputDevice8A* real_;  // Pointer to real DirectInput device
    LONG ref_;                    // COM reference count
    HANDLE hMapFile_;             // Cached shared memory handle
    SharedInput* pSharedMem_;     // Cached shared memory pointer
    GUID deviceGuid_;             // Device type identifier
public:
    DirectInputDevice8Wrapper(IDirectInputDevice8A* real, REFGUID rguid): real_(real), ref_(1), hMapFile_(NULL), pSharedMem_(nullptr), deviceGuid_(rguid) {
        if (real_) real_->AddRef();
        // Cache shared memory handle for performance
        hMapFile_ = OpenFileMappingA(FILE_MAP_READ, FALSE, "MoboalienInputSharedMemory");
        if (hMapFile_) {
            pSharedMem_ = (SharedInput*)MapViewOfFile(hMapFile_, FILE_MAP_READ, 0, 0, 0);
        }
    }
    virtual ~DirectInputDevice8Wrapper() {
        if (pSharedMem_) {
            UnmapViewOfFile(pSharedMem_);
            pSharedMem_ = nullptr;
        }
        if (hMapFile_) {
            CloseHandle(hMapFile_);
            hMapFile_ = NULL;
        }
        if (real_) real_->Release();
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, LPVOID *ppvObj) {
        return real_->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG, AddRef)() {
        InterlockedIncrement(&ref_);
        return real_->AddRef();
    }
    STDMETHOD_(ULONG, Release)() {
        ULONG r = real_->Release();
        if (InterlockedDecrement(&ref_) == 0) delete this;
        return r;
    }

    // IDirectInputDevice8 methods - forward most, but intercept GetDeviceState / GetDeviceData
    STDMETHOD(GetCapabilities)(LPDIDEVCAPS lpDIDevCaps) { return real_->GetCapabilities(lpDIDevCaps); }
    STDMETHOD(EnumObjects)(LPDIENUMDEVICEOBJECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags){ return real_->EnumObjects(lpCallback,pvRef,dwFlags); }
    STDMETHOD(GetProperty)(REFGUID rguidProp, LPDIPROPHEADER pdiph){ return real_->GetProperty(rguidProp,pdiph); }
    STDMETHOD(SetProperty)(REFGUID rguidProp, LPCDIPROPHEADER pdiph){ return real_->SetProperty(rguidProp,pdiph); }
    STDMETHOD(Acquire)(){ return real_->Acquire(); }
    STDMETHOD(Unacquire)(){ return real_->Unacquire(); }
    /*
    GetDeviceState - Main input injection point
    
    This is where the magic happens. We:
    1. Get the real device state from hardware
    2. Check shared memory for injected input
    3. Overlay injected input on top of real input
    
    Parameters:
    - cbData: Size of data buffer (helps identify device type)
    - lpvData: Buffer to receive device state
    
    Device type detection:
    - cbData >= 256: Likely keyboard (256 key states)
    - cbData == 16 or 20: Likely mouse (DIMOUSESTATE/DIMOUSESTATE2)
    - Other sizes: Could be joystick
    */
    STDMETHOD(GetDeviceState)(DWORD cbData, LPVOID lpvData){
        // First get the real device state
        HRESULT hr = real_->GetDeviceState(cbData, lpvData);
        if (FAILED(hr) || !lpvData) {
            return hr;
        }
        
        // Fast path: use cached shared memory for zero-latency injection
        if (pSharedMem_) {
            // Keyboard injection
            if (IsEqualGUID(deviceGuid_, GUID_SysKeyboard)) {
                BYTE* keys = (BYTE*)lpvData;
                const uint32_t* downMask = pSharedMem_->kb.downMask;
                
                for (int i = 0; i < 8; ++i) {
                    uint32_t mask = downMask[i];
                    if (mask) {
                        int base = i << 5;
                        while (mask) {
                            unsigned long bit;
                            _BitScanForward(&bit, mask);
                            keys[base + bit] = 0x80;
                            mask &= mask - 1;
                        }
                    }
                }
            }
            // Mouse injection
            else if (IsEqualGUID(deviceGuid_, GUID_SysMouse)) {
                struct DIMOUSESTATE { LONG x, y, z; BYTE buttons[4]; };
                DIMOUSESTATE* mouse = (DIMOUSESTATE*)lpvData;
                
                mouse->x += pSharedMem_->mouse.x;
                mouse->y += pSharedMem_->mouse.y;
                mouse->z += pSharedMem_->mouse.wheel;
                
                for (int i = 0; i < 4; ++i) {
                    if (pSharedMem_->mouse.buttons & (1 << i)) {
                        mouse->buttons[i] = 0x80;
                    }
                }
            }
        }
        
        return hr;
    }
    STDMETHOD(GetDeviceData)(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags){
        return real_->GetDeviceData(cbObjectData, rgdod, pdwInOut, dwFlags);
    }

    // Forward remaining methods
    STDMETHOD(SetDataFormat)(LPCDIDATAFORMAT lpdf) { return real_->SetDataFormat(lpdf); }
    STDMETHOD(SetEventNotification)(HANDLE hEvent){ return real_->SetEventNotification(hEvent); }
    STDMETHOD(SetCooperativeLevel)(HWND hwnd, DWORD dwFlags){ return real_->SetCooperativeLevel(hwnd,dwFlags); }
    STDMETHOD(GetObjectInfo)(LPDIDEVICEOBJECTINSTANCEA pdidoi, DWORD dwObj, DWORD dwHow){ return real_->GetObjectInfo(pdidoi,dwObj,dwHow); }
    STDMETHOD(GetDeviceInfo)(LPDIDEVICEINSTANCEA pdidi){ return real_->GetDeviceInfo(pdidi); }
    STDMETHOD(RunControlPanel)(HWND hwndOwner, DWORD dwFlags){ return real_->RunControlPanel(hwndOwner,dwFlags); }
    STDMETHOD(Initialize)(HINSTANCE hinst, DWORD dwVersion, REFGUID rguid){ return real_->Initialize(hinst,dwVersion,rguid); }
    STDMETHOD(EnumEffectsInFile)(LPCSTR lpszFileName, LPDIENUMEFFECTSINFILECALLBACK pec, LPVOID pvRef, DWORD dwFlags){ return E_NOTIMPL; }
    STDMETHOD(WriteEffectToFile)(LPCSTR lpszFileName, DWORD dwEntries, LPDIFILEEFFECT rgDiFileEft, DWORD dwFlags){ return E_NOTIMPL; }
    STDMETHOD(BuildActionMap)(LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags){ return E_NOTIMPL; }
    STDMETHOD(SetActionMap)(LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags){ return E_NOTIMPL; }
    STDMETHOD(GetImageInfo)(LPDIDEVICEIMAGEINFOHEADERA lpdiDevImageInfoHeader){ return E_NOTIMPL; }
    STDMETHOD(CreateEffect)(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT *ppdeff, LPUNKNOWN punkOuter){ return real_->CreateEffect(rguid,lpeff,ppdeff,punkOuter); }
    STDMETHOD(EnumEffects)(LPDIENUMEFFECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwEffType){ return real_->EnumEffects(lpCallback,pvRef,dwEffType); }
    STDMETHOD(GetEffectInfo)(LPDIEFFECTINFOA pdei, REFGUID rguid){ return real_->GetEffectInfo(pdei,rguid); }
    STDMETHOD(GetForceFeedbackState)(LPDWORD pdwOut){ return real_->GetForceFeedbackState(pdwOut); }
    STDMETHOD(SendForceFeedbackCommand)(DWORD dwToSend){ return real_->SendForceFeedbackCommand(dwToSend); }
    STDMETHOD(EnumCreatedEffectObjects)(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl){ return real_->EnumCreatedEffectObjects(lpCallback,pvRef,fl); }
    STDMETHOD(Escape)(LPDIEFFESCAPE pesc){ return real_->Escape(pesc); }
    STDMETHOD(Poll)(){ return real_->Poll(); }
    STDMETHOD(SendDeviceData)(DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl){ return real_->SendDeviceData(cbObjectData,rgdod,pdwInOut,fl); }
    // Many methods omitted for brevity. In production wrap all methods properly.
};

/*
DirectInput8Wrapper - Intercepts device creation

This wrapper implements IDirectInput8A and forwards most calls to the real
DirectInput object, but intercepts CreateDevice to wrap returned devices
with our DirectInputDevice8Wrapper.

Key method: CreateDevice() - wraps returned devices for input injection
*/
class DirectInput8Wrapper : public IDirectInput8A {
    IDirectInput8A* real_;  // Pointer to real DirectInput8 object
    LONG ref_;              // COM reference count
public:
    DirectInput8Wrapper(IDirectInput8A* real): real_(real), ref_(1) {
        if (real_) real_->AddRef();
    }
    virtual ~DirectInput8Wrapper() {
        if (real_) real_->Release();
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, LPVOID *ppvObj) {
        return real_->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG, AddRef)() {
        InterlockedIncrement(&ref_);
        return real_->AddRef();
    }
    STDMETHOD_(ULONG, Release)() {
        ULONG r = real_->Release();
        if (InterlockedDecrement(&ref_) == 0) delete this;
        return r;
    }

    /*
    CreateDevice - Wraps created devices for input injection
    
    When games create DirectInput devices (keyboard, mouse, joystick), we
    wrap them with our DirectInputDevice8Wrapper so we can intercept
    GetDeviceState calls.
    
    Parameters:
    - rguid: Device GUID (GUID_SysKeyboard, GUID_SysMouse, etc.)
    - lplpDirectInputDevice: Receives pointer to created device
    - pUnkOuter: COM aggregation (usually NULL)
    */
    STDMETHOD(CreateDevice)(REFGUID rguid, LPDIRECTINPUTDEVICE8A *lplpDirectInputDevice, LPUNKNOWN pUnkOuter){
        // Create the real device first
        HRESULT hr = real_->CreateDevice(rguid, lplpDirectInputDevice, pUnkOuter);
        if (SUCCEEDED(hr) && lplpDirectInputDevice && *lplpDirectInputDevice) {
            // Wrap the returned device with our interceptor
            IDirectInputDevice8A* realDev = *lplpDirectInputDevice;
            DirectInputDevice8Wrapper* wrapped = new DirectInputDevice8Wrapper(realDev, rguid);
            *lplpDirectInputDevice = wrapped;
            
            // Release the original reference since wrapper holds its own
            realDev->Release();
        }
        return hr;
    }
    STDMETHOD(EnumDevices)(DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags){ return real_->EnumDevices(dwDevType,lpCallback,pvRef,dwFlags); }
    STDMETHOD(GetDeviceStatus)(REFGUID rguidInstance){ return real_->GetDeviceStatus(rguidInstance); }
    STDMETHOD(RunControlPanel)(HWND hwndOwner, DWORD dwFlags){ return real_->RunControlPanel(hwndOwner,dwFlags); }
    STDMETHOD(Initialize)(HINSTANCE hinst, DWORD dwVersion){ return real_->Initialize(hinst,dwVersion); }
    // Other methods forwarded...
    STDMETHOD(FindDevice)(REFGUID rguidClass, LPCSTR ptszName, LPGUID pguidInstance){ return real_->FindDevice(rguidClass,ptszName,pguidInstance); }
    STDMETHOD(EnumDevicesBySemantics)(LPCSTR ptszUserName, LPDIACTIONFORMAT lpdiActionFormat, LPDIENUMDEVICESBYSEMANTICSCBA lpCallback, LPVOID pvRef, DWORD dwFlags){ return real_->EnumDevicesBySemantics(ptszUserName,lpdiActionFormat,lpCallback,pvRef,dwFlags); }
    STDMETHOD(ConfigureDevices)(LPDICONFIGUREDEVICESCALLBACK lpdiCallback, LPDICONFIGUREDEVICESPARAMSA lpdiCDParams, DWORD dwFlags, LPVOID pvRefData){ return E_NOTIMPL; }
};

/*
DirectInput8Create - Main entry point for DirectInput proxy

This is the exported function that games call to create DirectInput objects.
We intercept this call to:
1. Load the real dinput8.dll from system32 (to avoid recursion)
2. Create the real DirectInput object
3. Wrap it with our DirectInput8Wrapper for device interception

Parameters:
- hinst: Application instance handle
- dwVersion: DirectInput version (should be 0x0800)
- riidltf: Interface ID (usually IID_IDirectInput8A)
- ppvOut: Receives pointer to created DirectInput object
- punkOuter: COM aggregation (usually NULL)

Returns: HRESULT indicating success/failure
*/
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, LPUNKNOWN punkOuter) {
    OutputDebugStringA("DirectInput8Create intercepted by proxy\n");
    
    // Lazy load the real dinput8.dll on first call
    if (!real_dinput8) {
        // Load system dinput8.dll from system32 to avoid loading ourselves recursively
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH);
        strcat_s(sysPath, "\\dinput8.dll");
        
        real_dinput8 = LoadLibraryA(sysPath);
        if (!real_dinput8) return E_FAIL;
        
        // Get the real DirectInput8Create function
        real_DirectInput8Create = (DirectInput8Create_t)GetProcAddress(real_dinput8, "DirectInput8Create");
        if (!real_DirectInput8Create) return E_FAIL;
    }

    // Create the real DirectInput object
    HRESULT hr = real_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    if (FAILED(hr) || !ppvOut || !*ppvOut) {
        return hr;
    }

    // If the requested interface is IDirectInput8A, wrap it for device interception
    if (riidltf == IID_IDirectInput8A) {
        IDirectInput8A* real = (IDirectInput8A*)(*ppvOut);
        DirectInput8Wrapper* wrapped = new DirectInput8Wrapper(real);
        *ppvOut = wrapped;
        
        // Release original reference since wrapper holds its own
        real->Release();
    }
    // Note: We only support IDirectInput8A for now. Games using Unicode version
    // (IDirectInput8W) will get the unwrapped interface.

    return hr;
}

/*
DLL Entry Point

Called when the DLL is loaded/unloaded by the process.
*/
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            real_dinput8 = nullptr;
            real_DirectInput8Create = nullptr;
            OutputDebugStringA("dinput8.dll proxy loaded\n");
            break;
            
        case DLL_PROCESS_DETACH:
            // DLL unloaded - cleanup
            if (real_dinput8) {
                FreeLibrary(real_dinput8);
                real_dinput8 = nullptr;
            }
            break;
    }
    return TRUE;
}
