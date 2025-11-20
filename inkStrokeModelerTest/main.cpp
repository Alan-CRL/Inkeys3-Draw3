#include "main.h"

#include "renderer.h"

// 调测专用
void Test()
{
	MessageBoxW(NULL, L"标记处", L"标记", MB_OK | MB_SYSTEMMODAL);
}
void Testb(bool t)
{
	MessageBoxW(NULL, t ? L"true" : L"false", L"真否标记", MB_OK | MB_SYSTEMMODAL);
}
void Testi(long long t)
{
	MessageBoxW(NULL, to_wstring(t).c_str(), L"数值标记", MB_OK | MB_SYSTEMMODAL);
}
void Testd(double t)
{
	MessageBoxW(NULL, to_wstring(t).c_str(), L"浮点标记", MB_OK | MB_SYSTEMMODAL);
}
void Testw(wstring t)
{
	MessageBoxW(NULL, t.c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL);
}

WindowInfoClass windowInfo;
InkRenderer g_InkRenderer;

int main()
{
	timeBeginPeriod(1); // 全局高精度计时器

	// 初始化 D3D 设备
	CComPtr<ID3D11DeviceContext> d3dDeviceContext; // DC
	{
		// 创建 HARDWARE 设备

		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

		D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};

		D3D11CreateDevice(
			nullptr,                    // 指定 nullptr 使用默认适配器
			D3D_DRIVER_TYPE_HARDWARE,   // 使用 HARDWARE 硬件加速渲染器
			nullptr,                    // 没有软件模块
			creationFlags,              // 设置支持 BGRA 格式
			featureLevels,              // 功能级别数组
			ARRAYSIZE(featureLevels),   // 数组大小
			D3D11_SDK_VERSION,          // SDK 版本
			&d3dDevice_HARDWARE,        // 返回创建的设备
			nullptr,                    // 返回实际的功能级别
			&d3dDeviceContext           // 返回设备上下文
		);
	}

	// 窗口创建
	{
		windowHWND = hiex::initgraph_win32(windowInfo.w, windowInfo.h);
	}

	// SwapChain
	CComPtr<IDXGISwapChain1> swapChain;
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = windowInfo.w;
		swapChainDesc.Height = windowInfo.h;
		swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_NONE;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.Flags = 0;

		CComPtr<IDXGIDevice> dxgiDevice;
		d3dDevice_HARDWARE.QueryInterface(&dxgiDevice);

		CComPtr<IDXGIAdapter> dxgiAdapter;
		dxgiDevice->GetAdapter(&dxgiAdapter);

		CComPtr<IDXGIFactory2> dxgiFactory;
		dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

		dxgiFactory->CreateSwapChainForHwnd(
			d3dDevice_HARDWARE,
			windowHWND,
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChain
		);

		// win7 上 SetBackgroundColor 会因 E_NOTIMPL 失败
		DXGI_RGBA color = { 1.0f, 1.0f, 1.0f, 1.0f };
		swapChain->SetBackgroundColor(&color);
	}

	if (!g_InkRenderer.Init(d3dDevice_HARDWARE, d3dDeviceContext, swapChain)) {
		// 初始化失败，可能是 hlsl 文件没找到
		MessageBox(NULL, L"Failed to init D3D Renderer. Check InkShader.hlsl location.", L"Error", MB_OK);
	}

	// --- 开始 D3D 绘制 ---
	// 1. 不需要 BeginDraw，但可以手动 Clear (如果需要背景色)
	float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
	// 清空我们的 RTV (注意：这里用 g_InkRenderer.m_pRTV)
	d3dDeviceContext->ClearRenderTargetView(g_InkRenderer.m_pRTV, clearColor);

	// 2. 绘制我们的测试矩形
	// 参数：x=100, y=100, w=200, h=100, 红色(RGBA), 屏幕宽800, 高600
	g_InkRenderer.DrawRect(100.0f, 100.0f, 200.0f, 100.0f,
		XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f), // 半透明红色
		(float)windowInfo.w, (float)windowInfo.h);

	swapChain->Present(0, 0); // 第一个参数为 1 则开启垂直同步

	getmessage(EM_KEY);
	return 0;
}