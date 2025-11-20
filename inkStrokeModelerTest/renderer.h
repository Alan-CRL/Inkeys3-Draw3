#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <atlbase.h>
#include <string>
#include <vector>

#include "resource.h"

struct ShaderBlob {
	const void* data;
	size_t size;
};

ShaderBlob LoadShaderFromResource(int resourceID) {
	// 1. 获取当前模块句柄 (exe 自身)
	HMODULE hModule = GetModuleHandle(NULL);

	// 2. 查找资源 (注意类型 "SHADER" 必须和你在 rc 文件里填的一模一样)
	HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(resourceID), L"SHADER");
	if (!hRes) return { nullptr, 0 };

	// 3. 加载资源
	HGLOBAL hMem = LoadResource(hModule, hRes);
	if (!hMem) return { nullptr, 0 };

	// 4. 获取大小和指针
	DWORD size = SizeofResource(hModule, hRes);
	void* data = LockResource(hMem); // 锁定内存，获取只读指针

	return { data, (size_t)size };
}

using namespace DirectX;

struct CB_ScreenSize {
	float width;
	float height;
	float padding[2];
};

struct SimpleVertex {
	XMFLOAT2 pos;
	XMFLOAT4 color;
};

class InkRenderer {
public:
	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pContext;
	CComPtr<ID3D11RenderTargetView> m_pRTV;
	CComPtr<ID3D11VertexShader> m_pVS;
	CComPtr<ID3D11PixelShader> m_pPS;
	CComPtr<ID3D11InputLayout> m_pLayout;
	CComPtr<ID3D11Buffer> m_pConstantBuffer;
	CComPtr<ID3D11BlendState> m_pBlendState;
	CComPtr<ID3D11RasterizerState> m_pRasterizerState;

	// 优化：复用顶点缓冲区
	CComPtr<ID3D11Buffer> m_pVertexBuffer;

	bool Init(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain1* swapChain) {
		m_pDevice = device;
		m_pContext = context;

		// 1. RTV
		CComPtr<ID3D11Texture2D> backBuffer;
		HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		if (FAILED(hr)) return false;
		hr = m_pDevice->CreateRenderTargetView(backBuffer, nullptr, &m_pRTV);
		if (FAILED(hr)) return false;

		// 2. 加载二进制着色器 (这是本次修改的核心)
		if (!LoadEmbeddedShaders()) return false;

		// 3. 常量缓冲区
		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.ByteWidth = sizeof(CB_ScreenSize);
		cbDesc.Usage = D3D11_USAGE_DYNAMIC;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_pDevice->CreateBuffer(&cbDesc, nullptr, &m_pConstantBuffer);

		// 4. 混合状态
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		m_pDevice->CreateBlendState(&blendDesc, &m_pBlendState);

		// 5. 光栅化状态
		D3D11_RASTERIZER_DESC rasterDesc = {};
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.CullMode = D3D11_CULL_NONE;
		rasterDesc.FrontCounterClockwise = FALSE;
		rasterDesc.DepthClipEnable = TRUE;
		m_pDevice->CreateRasterizerState(&rasterDesc, &m_pRasterizerState);

		// 6. 预创建动态顶点缓冲区 (性能优化)
		D3D11_BUFFER_DESC vbDesc = {};
		vbDesc.ByteWidth = sizeof(SimpleVertex) * 6;
		vbDesc.Usage = D3D11_USAGE_DYNAMIC;
		vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_pDevice->CreateBuffer(&vbDesc, nullptr, &m_pVertexBuffer);

		return true;
	}

	void DrawRect(float x, float y, float w, float h, XMFLOAT4 color, float screenW, float screenH) {
		// 更新常量
		D3D11_MAPPED_SUBRESOURCE map;
		if (SUCCEEDED(m_pContext->Map(m_pConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			CB_ScreenSize* data = (CB_ScreenSize*)map.pData;
			data->width = screenW;
			data->height = screenH;
			m_pContext->Unmap(m_pConstantBuffer, 0);
		}

		SimpleVertex vertices[] = {
			{ {x, y}, color }, { {x + w, y}, color }, { {x, y + h}, color },
			{ {x + w, y}, color }, { {x + w, y + h}, color }, { {x, y + h}, color },
		};

		// 更新顶点 (不再创建新 Buffer)
		if (SUCCEEDED(m_pContext->Map(m_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			memcpy(map.pData, vertices, sizeof(vertices));
			m_pContext->Unmap(m_pVertexBuffer, 0);
		}

		// 渲染设置
		m_pContext->OMSetRenderTargets(1, &m_pRTV.p, nullptr);

		D3D11_VIEWPORT vp = { 0.0f, 0.0f, screenW, screenH, 0.0f, 1.0f };
		m_pContext->RSSetViewports(1, &vp);
		m_pContext->RSSetState(m_pRasterizerState);

		float blendFactor[] = { 0.f, 0.f, 0.f, 0.f };
		m_pContext->OMSetBlendState(m_pBlendState, blendFactor, 0xFFFFFFFF);

		m_pContext->IASetInputLayout(m_pLayout);
		m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		UINT stride = sizeof(SimpleVertex);
		UINT offset = 0;
		ID3D11Buffer* pVB = m_pVertexBuffer;
		m_pContext->IASetVertexBuffers(0, 1, &pVB, &stride, &offset);

		m_pContext->VSSetShader(m_pVS, nullptr, 0);
		m_pContext->VSSetConstantBuffers(0, 1, &m_pConstantBuffer.p);
		m_pContext->PSSetShader(m_pPS, nullptr, 0);

		m_pContext->Draw(6, 0);
	}

private:
	bool LoadEmbeddedShaders() {
		// --- 加载顶点着色器 ---
		// 使用 resource.h 里定义的 IDR_VS
		ShaderBlob vsBlob = LoadShaderFromResource(IDR_VS1);
		if (!vsBlob.data) {
			MessageBoxW(NULL, L"Failed to load Vertex Shader Resource!", L"Error", MB_OK);
			return false;
		}

		HRESULT hr = m_pDevice->CreateVertexShader(vsBlob.data, vsBlob.size, nullptr, &m_pVS);
		if (FAILED(hr)) return false;

		// --- 加载像素着色器 ---
		ShaderBlob psBlob = LoadShaderFromResource(IDR_PS1);
		if (!psBlob.data) {
			MessageBoxW(NULL, L"Failed to load Pixel Shader Resource!", L"Error", MB_OK);
			return false;
		}

		hr = m_pDevice->CreatePixelShader(psBlob.data, psBlob.size, nullptr, &m_pPS);
		if (FAILED(hr)) return false;

		// --- 创建 Input Layout ---
		// 需要用到 VS 的二进制数据
		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		hr = m_pDevice->CreateInputLayout(layout, 2, vsBlob.data, vsBlob.size, &m_pLayout);
		if (FAILED(hr)) return false;

		return true;
	}
};