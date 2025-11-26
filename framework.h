#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

// DirectX 11
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Direct2D and DirectWrite
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

// C++ standard library
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <cstdint>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;
