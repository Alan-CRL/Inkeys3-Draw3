#include "main.h"

#include "renderer.h"

WindowInfoClass windowInfo;
InkRenderer inkRenderer;

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
		d3dDevice_HARDWARE.QueryInterface(&dxgiDevice);
	}

	// 窗口创建
	{
		windowHWND = hiex::initgraph_win32(windowInfo.w, windowInfo.h, EW_SHOWCONSOLE);
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

	// 交换链应该保证指定脏区，而不是全部重绘
	// 后续修改，非 flip_discard

	inkRenderer.Init(d3dDevice_HARDWARE, d3dDeviceContext, swapChain);
	inkRenderer.SetScreenSize((float)windowInfo.w, (float)windowInfo.h);

	vector<InkVertex> list;
	{
		float x1 = 100.0f;
		float y1 = 100.0f;
		float r1 = 25.0f;

		float x2 = 500.0f;
		float y2 = 500.0f;
		float r2 = 150.0f;

		list.emplace_back(InkVertex(x1, y1, r1, x2, y2, r2, XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f)));
	}

	// 开始绘制
	float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);

	inkRenderer.DrawStrokeSegment2(list, 0, list.size());

	// 同步本帧完成
	d3dDeviceContext->End(inkRenderer.g_frameFinishQuery);
	BOOL done = FALSE;
	// 注意：GetData 会在 GPU 还没执行到这个 Query 时返回 S_FALSE
	while (S_OK != d3dDeviceContext->GetData(inkRenderer.g_frameFinishQuery, &done, sizeof(done), 0))
	{
		this_thread::yield();
	}

	swapChain->Present(0, 0); // 第一个参数为 1 则开启垂直同

	getmessage(EM_KEY);
	return 0;
}