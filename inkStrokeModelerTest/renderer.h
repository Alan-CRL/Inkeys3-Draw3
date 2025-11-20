#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <atlbase.h>
#include <string>
#include <vector>

#include "resource.h"

using namespace DirectX;

struct ShaderBlob
{
	const void* data;
	size_t      size;
};
ShaderBlob LoadShaderFromResource(int resourceID)
{
	HMODULE hModule = ::GetModuleHandle(nullptr);
	HRSRC   hRes = ::FindResource(hModule, MAKEINTRESOURCE(resourceID), L"SHADER");
	if (!hRes) return { nullptr, 0 };

	HGLOBAL hMem = ::LoadResource(hModule, hRes);
	if (!hMem) return { nullptr, 0 };

	DWORD size = ::SizeofResource(hModule, hRes);
	void* data = ::LockResource(hMem);

	return { data, static_cast<size_t>(size) };
}

// ----------------- 常量缓冲和顶点结构 -----------------

struct CB_ScreenSize {
	float width;   // 渲染目标宽度(像素)
	float height;  // 渲染目标高度(像素)
	float padding[2]; // 对齐填充
};

struct SimpleVertex {
	XMFLOAT2 pos;   // 顶点位置(屏幕像素坐标)
	XMFLOAT4 color; // 顶点颜色(RGBA)
};

// ----------------- InkRenderer 类 -----------------

class InkRenderer {
public:
	// --- Core D3D 对象 ---
	CComPtr<ID3D11Device>           device;          // D3D11 设备
	CComPtr<ID3D11DeviceContext>    context;         // D3D11 上下文
	CComPtr<ID3D11RenderTargetView> renderTargetView;// 后备缓冲 RTV

	// --- 着色器 & 输入布局 ---
	CComPtr<ID3D11VertexShader>     vertexShader;    // 顶点着色器
	CComPtr<ID3D11PixelShader>      pixelShader;     // 像素着色器
	CComPtr<ID3D11InputLayout>      inputLayout;     // 顶点输入布局

	// --- 常量缓冲 ---
	CComPtr<ID3D11Buffer>           screenCB;        // 屏幕尺寸常量缓冲

	// --- 状态对象 ---
	CComPtr<ID3D11BlendState>       alphaBlendState; // Alpha 混合状态
	CComPtr<ID3D11RasterizerState>  rasterState;     // 光栅化状态(填充/背面裁剪)

	// --- 顶点缓冲 (复用) ---
	CComPtr<ID3D11Buffer>           dynamicVB;       // 动态顶点缓冲(例如画矩形/线条)

public:
	bool Init(ID3D11Device* inDevice,
		ID3D11DeviceContext* inContext,
		IDXGISwapChain1* swapChain)
	{
		if (!inDevice || !inContext || !swapChain)
			return false;

		device = inDevice;
		context = inContext;

		// 1. 创建 RTV
		CComPtr<ID3D11Texture2D> backBuffer;
		HRESULT hr = swapChain->GetBuffer(
			0,
			__uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(&backBuffer)
		);
		if (FAILED(hr)) return false;

		hr = device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
		if (FAILED(hr)) return false;

		// 2. 加载嵌入着色器并创建 VS / PS / InputLayout
		if (!LoadEmbeddedShaders())
			return false;

		// 3. 创建常量缓冲(屏幕尺寸)
		{
			D3D11_BUFFER_DESC cbDesc = {};
			cbDesc.ByteWidth = sizeof(CB_ScreenSize);
			cbDesc.Usage = D3D11_USAGE_DYNAMIC;
			cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			hr = device->CreateBuffer(&cbDesc, nullptr, &screenCB);
			if (FAILED(hr)) return false;
		}

		// 4. 创建混合状态(标准 Alpha 混合)
		{
			D3D11_BLEND_DESC blendDesc = {};
			blendDesc.AlphaToCoverageEnable = FALSE;
			blendDesc.IndependentBlendEnable = FALSE;
			auto& rt0 = blendDesc.RenderTarget[0];
			rt0.BlendEnable = TRUE;
			rt0.SrcBlend = D3D11_BLEND_SRC_ALPHA;
			rt0.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			rt0.BlendOp = D3D11_BLEND_OP_ADD;
			rt0.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt0.DestBlendAlpha = D3D11_BLEND_ZERO;
			rt0.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			rt0.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			hr = device->CreateBlendState(&blendDesc, &alphaBlendState);
			if (FAILED(hr)) return false;
		}

		// 5. 创建光栅化状态
		{
			D3D11_RASTERIZER_DESC rasterDesc = {};
			rasterDesc.FillMode = D3D11_FILL_SOLID;
			rasterDesc.CullMode = D3D11_CULL_NONE;
			rasterDesc.FrontCounterClockwise = FALSE;
			rasterDesc.DepthClipEnable = TRUE;

			hr = device->CreateRasterizerState(&rasterDesc, &rasterState);
			if (FAILED(hr)) return false;
		}

		// 6. 预创建动态顶点缓冲区 (可按需扩展容量)
		{
			D3D11_BUFFER_DESC vbDesc = {};
			vbDesc.ByteWidth = sizeof(SimpleVertex) * 6; // 目前够画一个矩形(2 三角形)
			vbDesc.Usage = D3D11_USAGE_DYNAMIC;
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			hr = device->CreateBuffer(&vbDesc, nullptr, &dynamicVB);
			if (FAILED(hr)) return false;
		}

		return true;
	}

	// 更新屏幕尺寸常量缓冲
	void SetSize(float targetW, float targetH)
	{
		if (!screenCB || !context) return;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(screenCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr)) return;

		auto* data = reinterpret_cast<CB_ScreenSize*>(mapped.pData);
		data->width = targetW;
		data->height = targetH;

		context->Unmap(screenCB, 0);
	}

private:
	// 加载嵌入的 VS/PS，并创建 InputLayout
	bool LoadEmbeddedShaders()
	{
		// --- 顶点着色器 ---
		ShaderBlob vsBlob = LoadShaderFromResource(IDR_VS1);
		if (!vsBlob.data) {
			::MessageBoxW(nullptr, L"Failed to load Vertex Shader Resource!", L"Error", MB_OK);
			return false;
		}

		HRESULT hr = device->CreateVertexShader(
			vsBlob.data,
			vsBlob.size,
			nullptr,
			&vertexShader
		);
		if (FAILED(hr)) return false;

		// --- 像素着色器 ---
		ShaderBlob psBlob = LoadShaderFromResource(IDR_PS1);
		if (!psBlob.data) {
			::MessageBoxW(nullptr, L"Failed to load Pixel Shader Resource!", L"Error", MB_OK);
			return false;
		}

		hr = device->CreatePixelShader(
			psBlob.data,
			psBlob.size,
			nullptr,
			&pixelShader
		);
		if (FAILED(hr)) return false;

		// --- 输入布局 ---
		// 对应 SimpleVertex: POSITION(float2) + COLOR(float4)
		D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
			// 语义, 索引, 格式, 输入槽, 偏移, 输入类型, 实例数据步长
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,         0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		hr = device->CreateInputLayout(
			layoutDesc,
			_countof(layoutDesc),
			vsBlob.data,
			vsBlob.size,
			&inputLayout
		);
		if (FAILED(hr)) return false;

		return true;
	}
};