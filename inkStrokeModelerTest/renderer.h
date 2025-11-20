#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <atlbase.h>
#include <vector>
#include <algorithm>

// 调测 BUG
#include <dxgidebug.h>

#include "resource.h"

#include "main.h"

using namespace DirectX;

// 辅助加载函数 (保持不变)
struct ShaderBlob { const void* data; size_t size; };
inline ShaderBlob LoadShaderFromResource(int resourceID) {
	HMODULE hModule = ::GetModuleHandle(nullptr);
	HRSRC hRes = ::FindResource(hModule, MAKEINTRESOURCE(resourceID), L"SHADER");
	if (!hRes) return { nullptr, 0 };
	HGLOBAL hMem = ::LoadResource(hModule, hRes);
	if (!hMem) return { nullptr, 0 };
	return { ::LockResource(hMem), static_cast<size_t>(::SizeofResource(hModule, hRes)) };
}

// 包含绘制所需的全部逻辑数据，不仅仅是位置
struct InkVertex {
	XMFLOAT2 pos;       // POSITION
	XMFLOAT4 color;     // COLOR
	XMFLOAT2 p1;        // VAL_P1
	XMFLOAT2 p2;        // VAL_P2
	float    r1;        // VAL_R1
	float    r2;        // VAL_R2
	int      shapeType; // VAL_TYPE
};

struct CB_ScreenSize {
	float width;
	float height;
	float padding[2];
};

class InkRenderer {
public:
	CComPtr<ID3D11Device>           device;
	CComPtr<ID3D11DeviceContext>    context;
	CComPtr<ID3D11RenderTargetView> renderTargetView;
	CComPtr<ID3D11VertexShader>     vertexShader;
	CComPtr<ID3D11PixelShader>      pixelShader;
	CComPtr<ID3D11InputLayout>      inputLayout;
	CComPtr<ID3D11Buffer>           screenCB;
	CComPtr<ID3D11Buffer>           dynamicVB;
	CComPtr<ID3D11BlendState>       alphaBlendState;
	CComPtr<ID3D11RasterizerState>  rasterState;

	// 初始化 (保持大部分逻辑不变，只修改 InputLayout 和 VB 大小)
	bool Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain)
	{
		device = inDevice; context = inContext;

		// ... (RTV 创建代码同原版，略) ...
		CComPtr<ID3D11Texture2D> backBuffer;
		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);

		ID3D11RenderTargetView* rtvs[] = { renderTargetView.p };
		context->OMSetRenderTargets(1, rtvs, nullptr);

		// 1. 创建常量缓冲
		D3D11_BUFFER_DESC cbDesc = { sizeof(CB_ScreenSize), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		device->CreateBuffer(&cbDesc, nullptr, &screenCB);

		// 2. 创建混合状态 (Premultiplied Alpha or Standard)
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; // or INV_SRC_ALPHA
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x0F;
		device->CreateBlendState(&blendDesc, &alphaBlendState);

		// 3. 创建动态顶点缓冲 (预留足够空间绘制一批线段，例如 100 个 quad)
		// 这里我们每次 DrawMap 一次，所以最小 6 个顶点即可
		D3D11_BUFFER_DESC vbDesc = { sizeof(InkVertex) * 6, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		device->CreateBuffer(&vbDesc, nullptr, &dynamicVB);

		// 4. 加载 Shader
		if (!LoadShaders()) return false;

		// 5. 创建光栅化状态
		{
			D3D11_RASTERIZER_DESC rasterDesc = {};
			rasterDesc.FillMode = D3D11_FILL_SOLID;

			// [关键修改] 必须设置为 D3D11_CULL_NONE，因为我们的 2D 投影翻转了 Y 轴，
			// 导致顺时针定义的三角形变成了逆时针，默认设置会把它们剔除掉。
			rasterDesc.CullMode = D3D11_CULL_NONE;

			rasterDesc.FrontCounterClockwise = FALSE;
			rasterDesc.DepthClipEnable = TRUE;
			// 如果你想启用多重采样抗锯齿(MSAA)，这里也要设为TRUE，但我们用的是Shader抗锯齿，所以无所谓
			rasterDesc.MultisampleEnable = TRUE;
			rasterDesc.AntialiasedLineEnable = TRUE;

			HRESULT hr = device->CreateRasterizerState(&rasterDesc, &rasterState);
			if (FAILED(hr)) return false;
		}

		return true;
	}

	void SetScreenSize(float w, float h)
	{
		// 1. 更新常量缓冲
		D3D11_MAPPED_SUBRESOURCE map;
		if (SUCCEEDED(context->Map(screenCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			CB_ScreenSize* data = (CB_ScreenSize*)map.pData;
			data->width = w; data->height = h;
			context->Unmap(screenCB, 0);
		}

		// 2. 设置视口 (Viewport)
		// 如果没有这一步，光栅化器不知道要把 NDC 坐标映射到屏幕的哪个区域
		D3D11_VIEWPORT vp;
		vp.Width = w;
		vp.Height = h;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		context->RSSetViewports(1, &vp);
	}

	// --- 核心绘制函数 ---
	void DrawStrokeSegment(float x1, float y1, float r1, float x2, float y2, float r2, XMFLOAT4 color)
	{
		// 1. 计算包围盒 (Bounding Box)
		// 为了确保包含整个形状和抗锯齿边缘，我们计算两个圆的包围盒
		float minX = min(x1 - r1, x2 - r2);
		float minY = min(y1 - r1, y2 - r2);
		float maxX = max(x1 + r1, x2 + r2);
		float maxY = max(y1 + r1, y2 + r2);

		// 额外扩充一点 padding (比如 2.0f) 以防 SDF 边缘被切断
		float padding = 2.0f;
		minX -= padding; minY -= padding;
		maxX += padding; maxY += padding;

		// 2. 填充顶点数据 (6个顶点组成的两个三角形 = 1个 Quad)
		InkVertex vertices[6];

		// 共享的几何属性
		auto SetV = [&](int i, float x, float y) {
			vertices[i].pos = XMFLOAT2(x, y);
			vertices[i].color = color;
			vertices[i].p1 = XMFLOAT2(x1, y1);
			vertices[i].p2 = XMFLOAT2(x2, y2);
			vertices[i].r1 = r1;
			vertices[i].r2 = r2;
			vertices[i].shapeType = 1; // 1 = InkStroke
			};

		// Triangle 1
		SetV(0, minX, minY); // Top-Left
		SetV(1, maxX, minY); // Top-Right
		SetV(2, minX, maxY); // Bottom-Left

		// Triangle 2
		SetV(3, minX, maxY); // Bottom-Left
		SetV(4, maxX, minY); // Top-Right
		SetV(5, maxX, maxY); // Bottom-Right

		// 3. 提交到 GPU
		D3D11_MAPPED_SUBRESOURCE map;
		if (SUCCEEDED(context->Map(dynamicVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			memcpy(map.pData, vertices, sizeof(vertices));
			context->Unmap(dynamicVB, 0);
		}

		// 4. 设置管线状态并绘制
		UINT stride = sizeof(InkVertex);
		UINT offset = 0;
		context->IASetInputLayout(inputLayout);
		context->IASetVertexBuffers(0, 1, &dynamicVB.p, &stride, &offset);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		context->VSSetShader(vertexShader, nullptr, 0);
		context->VSSetConstantBuffers(0, 1, &screenCB.p); // 绑定屏幕尺寸 CB

		context->PSSetShader(pixelShader, nullptr, 0);

		context->OMSetBlendState(alphaBlendState, nullptr, 0xFFFFFFFF);

		context->RSSetState(rasterState);
		context->Draw(6, 0);
	}

private:
	bool LoadShaders() {
		// 假设资源 ID 为 IDR_VS_INK 和 IDR_PS_INK
		// 为了演示，沿用你的 IDR_VS1 写法，但要注意 Shader 代码必须更新
		ShaderBlob vsBlob = LoadShaderFromResource(IDR_VS1);
		ShaderBlob psBlob = LoadShaderFromResource(IDR_PS1);

		if (!vsBlob.data || !psBlob.data) return false;

		device->CreateVertexShader(vsBlob.data, vsBlob.size, nullptr, &vertexShader);
		device->CreatePixelShader(psBlob.data, psBlob.size, nullptr, &pixelShader);

		// 更新 Input Layout 以匹配 InkVertex
		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION",      0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,                                D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",         0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },

			{ "VAL_START",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "VAL_END",       0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "VAL_RAD_START", 0, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "VAL_RAD_END",   0, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "VAL_TYPE",      0, DXGI_FORMAT_R32_SINT,           0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		HRESULT hr = device->CreateInputLayout(layout, _countof(layout), vsBlob.data, vsBlob.size, &inputLayout);
		if (FAILED(hr)) {
			// --- 调试代码开始 ---
	// 这段代码会把具体的错误原因输出到 Visual Studio 的 "输出" 窗口
			CComPtr<ID3D11InfoQueue> infoQueue;
			device->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&infoQueue);
			Testi(1);
			if (infoQueue) {
				Testi(2);
				UINT64 messageCount = infoQueue->GetNumStoredMessages();
				for (UINT64 i = 0; i < messageCount; i++) {
					Testi(3);
					SIZE_T messageLength = 0;
					infoQueue->GetMessage(i, nullptr, &messageLength);
					D3D11_MESSAGE* pMessage = (D3D11_MESSAGE*)malloc(messageLength);
					if (pMessage) {
						Testi(4);
						infoQueue->GetMessage(i, pMessage, &messageLength);
						OutputDebugStringA("D3D11 ERROR: ");
						OutputDebugStringA(pMessage->pDescription);
						OutputDebugStringA("\n");
						Testa(pMessage->pDescription);
						free(pMessage);
					}
				}
			}
			// --- 调试代码结束 ---

			MessageBox(NULL, L"CreateInputLayout Failed! Check Output Window for details.", L"Error", MB_OK);
			return false;
		}

		return true;
	}
};