[![NexusHook](/banner.png)](https://github.com/DevOfTheFour/nexus-hook-x64)
<p align="center">SwapChain hooking functionality for DirectX11 applications.</p>

##

<br>

## What's new in this fork
- **x64 support** — correct pointer-sized scanning across the full user address space.
- **SIMD-accelerated swap chain search** — SSE2 with runtime-detected AVX2, pointer-aligned iteration, heap-only (`MEM_PRIVATE` + RW) region filtering, region-wide SEH guard.
- **Scanner fixes** — no infinite loop on `VirtualQuery` failure, dummy swap chain created without the debug layer (its wrapper class has a different vtable), candidates validated via `GetDevice`.
- **`VMTHook` rewrite** — move semantics, RAII cleanup that restores the original VMT.

<br>

## Installation
Download the latest release, include `NexusHook.h` and link the `.lib` against your project.

<br>

## Usage
```cpp
#include "NexusHook.h"
#include <iostream>
#pragma comment(lib, "NexusHook.lib")

// Signature of IDXGISwapChain::Present
using D3D11PresentHook = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain*, UINT, UINT);

NexusHook hkMngr;

// Called every frame instead of the original Present.
// pThis is the game's swap chain - use it to grab the device/context on the first call.
// Always forward to the original through oFunctions, otherwise rendering stops.
HRESULT STDMETHODCALLTYPE SwapChainPresentHook(IDXGISwapChain* pThis, UINT SyncInterval, UINT Flags) {
    std::cout << "Hook called!" << std::endl;

    const auto original = reinterpret_cast<D3D11PresentHook>(hkMngr.oFunctions[SC_PRESENT]);
    return original(pThis, SyncInterval, Flags);
}

int main() {
    // Creates a dummy device, finds the game's swap chain in memory
    if (!hkMngr.Init()) {
        std::cerr << "Init failed" << std::endl;
        return 1;
    }

    // Redirects vtable slot SC_PRESENT to SwapChainPresentHook
    if (!hkMngr.HookSwapChain(reinterpret_cast<std::uintptr_t>(&SwapChainPresentHook), SC_PRESENT)) {
        std::cerr << "Hook failed" << std::endl;
        return 2;
    }
}
```

> **Note:** the snippet above is a console demo. In a real setup this code lives inside a DLL injected into the target process.

<br>
<br>

## API

### Methods
```cpp
bool Init();
```
>Initializes the hook manager. Returns true on success, false otherwise.

| Argument | Description | Default |
|:------------- |:------------- |:------------- |
| None | None | None |

<br>

```cpp
bool HookSwapChain(uintptr_t newFunc, int index);
```
>Hooks the specified function of the current SwapChain. Returns true on success, false otherwise.

| Argument | Description | Default |
|:------------- |:------------- |:------------- |
| newFunc | Pointer to your replacement function. | None |
| index | Vtable index of the function you want to hook. | None |

<br>

### Fields
```cpp
const static int iHookNumber = SC_GETLASTPRESENTCOUNT + 1;
```
>Number of functions you can hook. Specified in Dx11Indexes.h

<br>

```cpp
VMTHook hkHooks[iHookNumber];
```
>All hooks stored in their respective index number.

<br>

```cpp
std::uintptr_t oFunctions[iHookNumber] = { NULL };
```
>All original functions from the original SwapChain.

<br>

```cpp
SwapChainManager hMngr;
```
>SwapChainManager, you shouldn't need to access this.

<br>

## Credits
Based on [nexus-devs/nexus-hook](https://github.com/nexus-devs). Original swap chain location method by smallC (UnknownCheats), Dx11Indexes by c5 (guidedhacking.com).

<br>

## License
[MIT](LICENSE)
