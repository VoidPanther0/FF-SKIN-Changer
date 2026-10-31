#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

inline HWND g_hwnd = nullptr;

// Set when an update left the previous build as <exe>.old, so the first screen
// can confirm the new version.
inline bool g_updated = false;

inline ID3D11Device* g_pd3dDevice = nullptr;
inline ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
inline IDXGISwapChain* g_pSwapChain = nullptr;
inline ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

namespace app {

constexpr float kWidth = 800.0f;
constexpr float kHeight = 560.0f;
constexpr float kLoginWidth = 460.0f;
constexpr float kLoginHeight = 560.0f;
constexpr wchar_t kTitle[] = L"Cloth Changer";
constexpr char kVersion[] = "1";

void Init();
void Render();
void Shutdown();

} // namespace app

void StartWindowDrag();
void ResizeWindow(int width, int height);
