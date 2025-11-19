#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <atlbase.h> // for CComPtr
#include <string>
#include <vector>

// 引入数学库命名空间
using namespace DirectX;

// 对应 HLSL 中的 cbuffer ScreenBuffer
struct CB_ScreenSize {
	float width;
	float height;
	float padding[2];
};

// 对应 HLSL 中的 VS_INPUT
struct SimpleVertex {
	XMFLOAT2 pos;
	XMFLOAT4 color;
};

class InkRenderer {
public:
	// D3D 核心对象引用
	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pContext;
	CComPtr<ID3D11RenderTargetView> m_pRTV; // 必须：用于把画面画到 SwapChain 上

	// 管线状态对象
	CComPtr<ID3D11VertexShader> m_pVS;
	CComPtr<ID3D11PixelShader> m_pPS;
	CComPtr<ID3D11InputLayout> m_pLayout;
	CComPtr<ID3D11Buffer> m_pConstantBuffer;
	CComPtr<ID3D11BlendState> m_pBlendState;

	// 新增：用于关闭剔除
	CComPtr<ID3D11RasterizerState> m_pRasterizerState;

	// 初始化函数
	// 注意：需要传入 swapChain，因为我们需要创建 RenderTargetView
	bool Init(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain1* swapChain) {
		m_pDevice = device;
		m_pContext = context;

		// 1. 创建 RenderTargetView (这是你原代码缺少的关键一步)
		// 作用：让 D3D 知道要把像素画在 SwapChain 的哪个 Buffer 上
		CComPtr<ID3D11Texture2D> backBuffer;
		HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		if (FAILED(hr)) return false;

		hr = m_pDevice->CreateRenderTargetView(backBuffer, nullptr, &m_pRTV);
		if (FAILED(hr)) return false;

		// 2. 编译 Shader
		if (!CompileShaders()) return false;

		// 3. 创建常量缓冲区
		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.ByteWidth = sizeof(CB_ScreenSize);
		cbDesc.Usage = D3D11_USAGE_DYNAMIC; // CPU 经常更新 (窗口大小变化)
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_pDevice->CreateBuffer(&cbDesc, nullptr, &m_pConstantBuffer);

		// 4. 创建混合状态 (支持透明)
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

		// ---------------------------------------------------------
		// 5. 创建光栅化状态 (Rasterizer State) - <--- 新增部分
		// ---------------------------------------------------------
		D3D11_RASTERIZER_DESC rasterDesc = {};
		rasterDesc.FillMode = D3D11_FILL_SOLID; // 实体填充
		rasterDesc.CullMode = D3D11_CULL_NONE;  // 【关键】关闭剔除！双面都画
		rasterDesc.FrontCounterClockwise = FALSE;
		rasterDesc.DepthClipEnable = TRUE;

		hr = m_pDevice->CreateRasterizerState(&rasterDesc, &m_pRasterizerState);
		if (FAILED(hr)) return false;

		return true;
	}

	// 绘制矩形测试函数
	void DrawRect(float x, float y, float w, float h, XMFLOAT4 color, float screenW, float screenH) {
		// A. 更新常量缓冲区 (屏幕尺寸)
		D3D11_MAPPED_SUBRESOURCE map;
		if (SUCCEEDED(m_pContext->Map(m_pConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			CB_ScreenSize* data = (CB_ScreenSize*)map.pData;
			data->width = screenW;
			data->height = screenH;
			m_pContext->Unmap(m_pConstantBuffer, 0);
		}

		// B. 准备临时的顶点数据 (6个点组成两个三角形)
		SimpleVertex vertices[] = {
			// Triangle 1
			{ {x, y}, color },
			{ {x + w, y}, color },
			{ {x, y + h}, color },
			// Triangle 2
			{ {x + w, y}, color },
			{ {x + w, y + h}, color },
			{ {x, y + h}, color },
		};

		// 创建临时 Vertex Buffer
		D3D11_BUFFER_DESC vbDesc = {};
		vbDesc.ByteWidth = sizeof(SimpleVertex) * 6;
		vbDesc.Usage = D3D11_USAGE_DEFAULT;
		vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA initData = { vertices, 0, 0 };
		CComPtr<ID3D11Buffer> vertexBuffer;
		m_pDevice->CreateBuffer(&vbDesc, &initData, &vertexBuffer);

		// C. 设置管线状态 (这就好比画画前要拿起笔、蘸颜料)

		// 1. 绑定 Render Target (画布)
		m_pContext->OMSetRenderTargets(1, &m_pRTV.p, nullptr);

		// 2. 设置视口 (告诉 GPU 画满全屏)
		D3D11_VIEWPORT vp;
		vp.Width = screenW;
		vp.Height = screenH;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		m_pContext->RSSetViewports(1, &vp);

		// 3. 设置光栅化状态 (关闭剔除，确保双面可见)
		m_pContext->RSSetState(m_pRasterizerState);

		// 4. 设置混合状态 (开启透明混合)
		float blendFactor[] = { 0.f, 0.f, 0.f, 0.f };
		m_pContext->OMSetBlendState(m_pBlendState, blendFactor, 0xFFFFFFFF);

		// 5. 设置输入布局 (告诉 GPU 传进去的数据结构长啥样)
		m_pContext->IASetInputLayout(m_pLayout);

		// 6. 设置图元类型 (告诉 GPU 这些点是三角形)
		m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// 7. 绑定顶点缓冲区 (把数据喂给 GPU)
		UINT stride = sizeof(SimpleVertex);
		UINT offset = 0;
		ID3D11Buffer* pVB = vertexBuffer;
		m_pContext->IASetVertexBuffers(0, 1, &pVB, &stride, &offset);

		// 8. 绑定着色器 (VS 和 PS)
		m_pContext->VSSetShader(m_pVS, nullptr, 0);
		m_pContext->VSSetConstantBuffers(0, 1, &m_pConstantBuffer.p); // 别忘了把屏幕尺寸传给 VS

		m_pContext->PSSetShader(m_pPS, nullptr, 0);

		// D. 绘制
		m_pContext->Draw(6, 0);
	}

private:
	bool CompileShaders() {
		CComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
		DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG; // 调试模式建议开启

		// 编译 Vertex Shader
		// 注意：这里的文件路径 "InkShader.hlsl" 需要是相对于 exe 运行目录的路径 -> 后续改为资源中实现
		HRESULT hr = D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "VS", "vs_5_0", flags, 0, &vsBlob, &errorBlob);
		if (FAILED(hr)) {
			if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			return false;
		}
		m_pDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_pVS);

		// 编译 Pixel Shader
		hr = D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "PS", "ps_5_0", flags, 0, &psBlob, &errorBlob);
		if (FAILED(hr)) {
			if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			return false;
		}
		m_pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pPS);

		// 创建 Input Layout
		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		m_pDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_pLayout);

		return true;
	}
};