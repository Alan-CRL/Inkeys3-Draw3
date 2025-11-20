#pragma once

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <string>

#include <windows.h>
#include <locale>
#include <atlbase.h>

#include "HiEasyX.h"

#include <ink_stroke_modeler/stroke_modeler.h>
#pragma comment(lib, "ink_stroke_modeler_combined.lib")

#include <d3d11.h>
#include <DirectXMath.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")

using namespace std;
using namespace ink::stroke_model;

class WindowInfoClass
{
public:
	int w = 2000;
	int h = 1000;
};
extern WindowInfoClass windowInfo;

//CComPtr<ID2D1Factory1> d2dFactory1;
CComPtr<ID3D11Device> d3dDevice_HARDWARE;
//CComPtr<ID2D1Device> d2dDevice_HARDWARE;

HWND windowHWND;