#include "main.h"

#include "renderer.h"
#include <atomic>
#include <cmath>

WindowInfoClass windowInfo;
InkRenderer inkRenderer;

void UnionRectInPlace(RECT& target, const RECT& add);

namespace
{
	std::atomic<bool> g_clearCanvasRequested = false;
	std::atomic<int> g_brushShapeType = 0; // 0: 原来的画笔

	const char* GetDriverTypeName(D3D_DRIVER_TYPE driverType)
	{
		switch (driverType)
		{
		case D3D_DRIVER_TYPE_HARDWARE:
			return "Hardware";
		case D3D_DRIVER_TYPE_WARP:
			return "WARP";
		default:
			return "Unknown";
		}
	}

	const char* GetFeatureLevelName(D3D_FEATURE_LEVEL featureLevel)
	{
		switch (featureLevel)
		{
		case D3D_FEATURE_LEVEL_11_1:
			return "11_1";
		case D3D_FEATURE_LEVEL_11_0:
			return "11_0";
		case D3D_FEATURE_LEVEL_10_1:
			return "10_1";
		case D3D_FEATURE_LEVEL_10_0:
			return "10_0";
		case D3D_FEATURE_LEVEL_9_3:
			return "9_3";
		case D3D_FEATURE_LEVEL_9_2:
			return "9_2";
		case D3D_FEATURE_LEVEL_9_1:
			return "9_1";
		default:
			return "Unknown";
		}
	}

	HRESULT CreateD3D11DeviceWithCompatibleFeatureLevels(
		D3D_DRIVER_TYPE driverType,
		UINT creationFlags,
		CComPtr<ID3D11Device>& device,
		D3D_FEATURE_LEVEL& actualFeatureLevel,
		CComPtr<ID3D11DeviceContext>& deviceContext)
	{
		static const D3D_FEATURE_LEVEL preferredFeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};
		static const D3D_FEATURE_LEVEL fallbackFeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_0,
		};

		device.Release();
		deviceContext.Release();

		HRESULT hr = D3D11CreateDevice(
			nullptr,
			driverType,
			nullptr,
			creationFlags,
			preferredFeatureLevels,
			ARRAYSIZE(preferredFeatureLevels),
			D3D11_SDK_VERSION,
			&device,
			&actualFeatureLevel,
			&deviceContext
		);
		if (hr == E_INVALIDARG)
		{
			device.Release();
			deviceContext.Release();

			hr = D3D11CreateDevice(
				nullptr,
				driverType,
				nullptr,
				creationFlags,
				fallbackFeatureLevels,
				ARRAYSIZE(fallbackFeatureLevels),
				D3D11_SDK_VERSION,
				&device,
				&actualFeatureLevel,
				&deviceContext
			);
		}

		return hr;
	}

	constexpr float kMinMergeDistancePixels = 0.75f;
	constexpr float kSegmentDirtyPaddingPixels = 3.0f;
	constexpr float kVectorEpsilon = 1e-4f;

	struct StrokeSample
	{
		float x = 0.0f;
		float y = 0.0f;
		float radius = 0.0f;
	};

	float LengthSq(float x, float y)
	{
		return x * x + y * y;
	}

	float DistanceSq(const StrokeSample& a, const StrokeSample& b)
	{
		return LengthSq(b.x - a.x, b.y - a.y);
	}

	XMFLOAT2 NormalizeOrZero(float x, float y)
	{
		float lenSq = LengthSq(x, y);
		if (lenSq <= kVectorEpsilon) return XMFLOAT2(0.0f, 0.0f);

		float invLen = 1.0f / sqrtf(lenSq);
		return XMFLOAT2(x * invLen, y * invLen);
	}

	float GetSampleMergeDistance(const StrokeSample& a, const StrokeSample& b)
	{
		return max(kMinMergeDistancePixels, 0.25f * max(a.radius, b.radius));
	}

	bool ShouldMergeSample(const StrokeSample& a, const StrokeSample& b)
	{
		float threshold = GetSampleMergeDistance(a, b);
		return DistanceSq(a, b) <= threshold * threshold;
	}

	bool TryBuildCutNormal(const StrokeSample& prev, const StrokeSample& current, const StrokeSample& next, XMFLOAT2& outNormal)
	{
		XMFLOAT2 prevDir = NormalizeOrZero(current.x - prev.x, current.y - prev.y);
		XMFLOAT2 nextDir = NormalizeOrZero(next.x - current.x, next.y - current.y);

		if (LengthSq(prevDir.x, prevDir.y) <= kVectorEpsilon || LengthSq(nextDir.x, nextDir.y) <= kVectorEpsilon)
		{
			return false;
		}

		XMFLOAT2 sum(prevDir.x + nextDir.x, prevDir.y + nextDir.y);
		if (LengthSq(sum.x, sum.y) <= 0.04f)
		{
			return false;
		}

		outNormal = NormalizeOrZero(sum.x, sum.y);
		return true;
	}

	void EmitStrokeSegment(
		const StrokeSample& start,
		const StrokeSample& end,
		bool hasStartCut,
		const XMFLOAT2& startCutNormal,
		bool hasEndCut,
		const XMFLOAT2& endCutNormal,
		vector<InkStrokeSegmentData>& outSegments,
		RECT& dirtyRect)
	{
		InkStrokeSegmentData segment{};
		segment.startData = XMFLOAT4(start.x, start.y, start.radius, 0.0f);
		segment.endData = XMFLOAT4(end.x, end.y, end.radius, 0.0f);
		segment.cutNormals = XMFLOAT4(
			hasStartCut ? startCutNormal.x : 0.0f,
			hasStartCut ? startCutNormal.y : 0.0f,
			hasEndCut ? endCutNormal.x : 0.0f,
			hasEndCut ? endCutNormal.y : 0.0f
		);
		segment.flags =
			(hasStartCut ? InkStrokeSegmentFlag_StartCut : 0u) |
			(hasEndCut ? InkStrokeSegmentFlag_EndCut : 0u);
		outSegments.push_back(segment);

		float maxRadius = max(start.radius, end.radius) + kSegmentDirtyPaddingPixels;
		RECT bounds(
			static_cast<LONG>(floorf(min(start.x, end.x) - maxRadius)),
			static_cast<LONG>(floorf(min(start.y, end.y) - maxRadius)),
			static_cast<LONG>(ceilf(max(start.x, end.x) + maxRadius)),
			static_cast<LONG>(ceilf(max(start.y, end.y) + maxRadius))
		);
		UnionRectInPlace(dirtyRect, bounds);
	}

	class IncrementalStrokeBuilder
	{
	public:
		void Begin(const StrokeSample& sample)
		{
			Reset();
			hasSeedSample = true;
			seedSample = sample;
			latestObservedSample = sample;
		}

		void PushSample(const StrokeSample& sample, vector<InkStrokeSegmentData>& outSegments, RECT& dirtyRect)
		{
			if (!hasSeedSample)
			{
				Begin(sample);
				return;
			}

			latestObservedSample = sample;

			if (!hasPendingSegment)
			{
				if (ShouldMergeSample(seedSample, sample))
				{
					return;
				}

				pendingStart = seedSample;
				pendingEnd = sample;
				pendingHasStartCut = false;
				pendingStartCutNormal = XMFLOAT2(0.0f, 0.0f);
				hasPendingSegment = true;
				return;
			}

			if (ShouldMergeSample(pendingEnd, sample))
			{
				pendingEnd = sample;
				return;
			}

			XMFLOAT2 cutNormal{};
			bool hasEndCut = TryBuildCutNormal(pendingStart, pendingEnd, sample, cutNormal);
			EmitStrokeSegment(
				pendingStart,
				pendingEnd,
				pendingHasStartCut,
				pendingStartCutNormal,
				hasEndCut,
				cutNormal,
				outSegments,
				dirtyRect
			);

			pendingStart = pendingEnd;
			pendingEnd = sample;
			pendingHasStartCut = hasEndCut;
			pendingStartCutNormal = cutNormal;
		}

		void Finish(vector<InkStrokeSegmentData>& outSegments, RECT& dirtyRect)
		{
			if (!hasSeedSample)
			{
				return;
			}

			if (!hasPendingSegment)
			{
				EmitStrokeSegment(
					latestObservedSample,
					latestObservedSample,
					false,
					XMFLOAT2(0.0f, 0.0f),
					false,
					XMFLOAT2(0.0f, 0.0f),
					outSegments,
					dirtyRect
				);
			}
			else
			{
				EmitStrokeSegment(
					pendingStart,
					pendingEnd,
					pendingHasStartCut,
					pendingStartCutNormal,
					false,
					XMFLOAT2(0.0f, 0.0f),
					outSegments,
					dirtyRect
				);
			}

			Reset();
		}

		void EmitPreview(const StrokeSample& liveSample, vector<InkStrokeSegmentData>& outSegments, RECT& dirtyRect) const
		{
			if (!hasSeedSample)
			{
				return;
			}

			if (hasPendingSegment)
			{
				EmitStrokeSegment(
					pendingStart,
					liveSample,
					pendingHasStartCut,
					pendingStartCutNormal,
					false,
					XMFLOAT2(0.0f, 0.0f),
					outSegments,
					dirtyRect
				);
				return;
			}

			if (ShouldMergeSample(seedSample, liveSample))
			{
				EmitStrokeSegment(
					liveSample,
					liveSample,
					false,
					XMFLOAT2(0.0f, 0.0f),
					false,
					XMFLOAT2(0.0f, 0.0f),
					outSegments,
					dirtyRect
				);
				return;
			}

			EmitStrokeSegment(
				seedSample,
				liveSample,
				false,
				XMFLOAT2(0.0f, 0.0f),
				false,
				XMFLOAT2(0.0f, 0.0f),
				outSegments,
				dirtyRect
			);
		}

	private:
		void Reset()
		{
			hasSeedSample = false;
			hasPendingSegment = false;
			pendingHasStartCut = false;
		}

		bool hasSeedSample = false;
		bool hasPendingSegment = false;
		bool pendingHasStartCut = false;
		StrokeSample seedSample{};
		StrokeSample latestObservedSample{};
		StrokeSample pendingStart{};
		StrokeSample pendingEnd{};
		XMFLOAT2 pendingStartCutNormal{};
	};
}

void HighPrecisionWait(double frameTimeSpentMs, double targetFPS)
{
	// 1. 计算目标帧时间 (毫秒)
	// 例如: 60FPS -> 16.666... ms
	double targetFrameTimeMs = 1000.0 / targetFPS;

	// 2. 计算还需要等待的时间 (毫秒)
	double waitTimeMs = targetFrameTimeMs - frameTimeSpentMs;

	// 如果已经超时（掉帧），直接返回，不等待
	if (waitTimeMs <= 0.0)
	{
		return;
	}

	// 获取高精度计时器的频率 (Ticks Per Second)
	static LARGE_INTEGER freq = { 0 };
	if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

	// 记录开始等待时刻的 QPC
	LARGE_INTEGER startCounter, currentCounter;
	QueryPerformanceCounter(&startCounter);

	// 将等待时间 (ms) 转换为 QPC 的 Ticks 单位
	// 公式: (ms * freq) / 1000
	long long waitTicks = (long long)((waitTimeMs * (double)freq.QuadPart) / 1000.0);
	long long targetEndTick = startCounter.QuadPart + waitTicks;

	// === 阶段一：Sleep (粗略等待) ===
	// 只有当剩余时间大于 2ms 时才启用 Sleep，留出 1.5ms 的安全余量给 Spin
	if (waitTimeMs > 2.0)
	{
		// 预留约 1.5ms 的时间给最后的忙等待，其余时间睡觉
		// 注意这里显式使用 std::milli
		double sleepMs = waitTimeMs - 1.5;
		std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleepMs));
	}

	// === 阶段二：Spin (高精度忙等待) ===
	// 死循环直到 QPC 达到目标 Tick
	do
	{
		QueryPerformanceCounter(&currentCounter);

		YieldProcessor();
	} while (currentCounter.QuadPart < targetEndTick);
}
void UnionRectInPlace(RECT& target, const RECT& add)
{
	// 新增矩形无效，直接返回
	if (add.left >= add.right || add.top >= add.bottom) return;
	// target 是空矩形，直接替换
	if (target.left >= target.right || target.top >= target.bottom)
	{
		target = add;
		return;
	}

	target.left = min(target.left, add.left);
	target.top = min(target.top, add.top);
	target.right = max(target.right, add.right);
	target.bottom = max(target.bottom, add.bottom);
}

LRESULT CALLBACK Draw3WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		switch (wParam)
		{
		case '0':
		case VK_NUMPAD0:
			g_clearCanvasRequested.store(true, std::memory_order_relaxed);
			return 0;

		case '1':
		case VK_NUMPAD1:
			g_brushShapeType.store(0, std::memory_order_relaxed);
			return 0;
		}
		break;
	}

	return HIWINDOW_DEFAULT_PROC;
}

int main()
{
	timeBeginPeriod(1); // 全局高精度计时器

	// 窗口创建
	{
		windowHWND = hiex::initgraph_win32(windowInfo.w, windowInfo.h, EW_SHOWCONSOLE, _T(""), Draw3WndProc);
	}

	// 初始化 D3D 设备
	CComPtr<ID3D11DeviceContext> d3dDeviceContext; // DC
	{
		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D3D_FEATURE_LEVEL actualFeatureLevel = D3D_FEATURE_LEVEL_11_0;
		D3D_DRIVER_TYPE activeDriverType = D3D_DRIVER_TYPE_UNKNOWN;
		HRESULT hr = S_OK;

		hr = CreateD3D11DeviceWithCompatibleFeatureLevels(
			D3D_DRIVER_TYPE_HARDWARE,
			creationFlags,
			d3dDevice_HARDWARE,
			actualFeatureLevel,
			d3dDeviceContext
		);
		if (FAILED(hr))
		{
			cout << "Hardware device initialization failed. Falling back to WARP." << endl;

			hr = CreateD3D11DeviceWithCompatibleFeatureLevels(
				D3D_DRIVER_TYPE_WARP,
				creationFlags,
				d3dDevice_HARDWARE,
				actualFeatureLevel,
				d3dDeviceContext
			);

			if (FAILED(hr))
			{
				cout << "Failed to initialize a D3D11 device with both Hardware and WARP." << endl;
				return -1;
			}

			activeDriverType = D3D_DRIVER_TYPE_WARP;
		}
		else
		{
			activeDriverType = D3D_DRIVER_TYPE_HARDWARE;
		}

		cout << "Current D3D device: " << GetDriverTypeName(activeDriverType) << endl;
		cout << "D3D feature level: " << GetFeatureLevelName(actualFeatureLevel) << endl;

		hr = d3dDevice_HARDWARE->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice1));
		if (FAILED(hr))
		{
			cout << "Failed to query IDXGIDevice1 from the D3D11 device." << endl;
			return -1;
		}
	}

	// 从 windows8 开始可以考虑 SwapChain2 的 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT 更适合墨迹输入

	// 常规场景下的墨迹输入应使用 dxgiDevice1::SetMaximumFrameLatency(1) 来确保有一帧的间隙 CPU 处理时间留给 GPU 并行渲染来提高性能
	dxgiDevice1->SetMaximumFrameLatency(1);

	// 后续性能选项卡中可以提供一个 GPU 高优先级 的选项
	// dxgiDevice1->SetGPUThreadPriority(2);

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
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.Flags = 0;

		CComPtr<IDXGIAdapter> dxgiAdapter;
		dxgiDevice1->GetAdapter(&dxgiAdapter);

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
		//DXGI_RGBA color = { 1.0f, 1.0f, 1.0f, 1.0f };
		//swapChain->SetBackgroundColor(&color);
	}

	// 交换链应该保证指定脏区，而不是全部重绘
	// 后续修改，非 flip_discard

	inkRenderer.Init(d3dDevice_HARDWARE, d3dDeviceContext, swapChain);
	inkRenderer.SetScreenSize((float)windowInfo.w, (float)windowInfo.h);

	// 每帧绘制前应该
	/*
			inkRenderer.SetOMTarget();
			float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);
	*/

	// 简单的 DPI 初始化
	int dpiX;
	{
		HDC screen = GetDC(nullptr);
		dpiX = GetDeviceCaps(screen, LOGPIXELSX);
		ReleaseDC(nullptr, screen);
	}
	// 初始调测参数
	const bool debug = true;
	const float sampling_rate_hz = 120.0f; // Hz
	const float expected_speed = 500.0f * (static_cast<float>(dpiX) / 96.0f); // DPI 期望速度
	const float limited_speed = expected_speed * 3.0f; // 最高允许速度
	const int strokes_num = static_cast<int>(sampling_rate_hz / 6.0f); // 笔锋点个数
	// 模型初始化
	KalmanPredictorParams kalman_predictor_params;
	{
		kalman_predictor_params.process_noise = 0.05;
		kalman_predictor_params.measurement_noise = 0.01;
		kalman_predictor_params.min_stable_iteration = 4;
		kalman_predictor_params.max_time_samples = 20;
		kalman_predictor_params.min_catchup_velocity = expected_speed / 1000.0f;
		kalman_predictor_params.acceleration_weight = 0.5f;
		kalman_predictor_params.jerk_weight = 0.1f;
		kalman_predictor_params.prediction_interval = Duration(0.2);
		kalman_predictor_params.confidence_params = {
			.desired_number_of_samples = 10,
			.max_estimation_distance = 1.5f * static_cast<float>(kalman_predictor_params.measurement_noise),
			.min_travel_speed = 0.05f * expected_speed,
			.max_travel_speed = 0.25f * expected_speed,
			.max_linear_deviation = 10.0f * static_cast<float>(kalman_predictor_params.measurement_noise),
			.baseline_linearity_confidence = 0.4f
		};
	}
	StrokeModelParams params{
		.wobble_smoother_params{
			.is_enabled = false,
			.timeout = Duration(2.5 / sampling_rate_hz),
			.speed_floor = 0.02f * expected_speed,
			.speed_ceiling = 0.03f * expected_speed
		},
		.position_modeler_params{
			.spring_mass_constant = 11.f / 32400,
			.drag_constant = 72.f
		},
		.sampling_params{
			.min_output_rate = 3.0f * sampling_rate_hz,
			.end_of_stroke_stopping_distance = .001,
			.end_of_stroke_max_iterations = 20,
			.max_outputs_per_call = 2000
		},
	};
	StrokeModeler modeler;

	auto clearCanvas = [&swapChain]()
		{
			// For transparent targets, clear to premultiplied transparent black or draw the real background first.
			const XMFLOAT4 clearColor(1.0f, 1.0f, 1.0f, 1.0f);
			inkRenderer.ClearRTV(inkRenderer.offScreenTexture1RTV, clearColor);
			inkRenderer.ClearRTV(inkRenderer.renderTargetView, clearColor);
			swapChain->Present(0, 0);
		};

	clearCanvas();

	ExMessage m{};
	while (true)
	{
		if (g_clearCanvasRequested.exchange(false, std::memory_order_relaxed))
		{
			clearCanvas();
		}

		if (!hiex::peekmessage_win32(&m, EM_MOUSE, true, windowHWND))
		{
			Sleep(1);
			continue;
		}

		if (m.message == WM_LBUTTONDOWN || m.message == WM_RBUTTONDOWN)
		{
			bool eraser = (m.message == WM_RBUTTONDOWN) ? true : false;
			eraser = false;

			// 检查设备是否丢失，并重建
			// TODO

			RECT current = RECT(0, 0, 0, 0);
			bool isFirstFrame = true;

			params.prediction_params = kalman_predictor_params;
			//params.prediction_params = StrokeEndPredictorParams();

			if (absl::Status status = modeler.Reset(params); !status.ok())
			{
				cout << "Error: " << status.message() << endl;
			}

			vector<Result> smoothed_stroke;
			vector<Result> predicted_stroke;
			IncrementalStrokeBuilder strokeBuilder;
			size_t processedSmoothedStrokeCount = 0;

			chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();

			Input input
			{
				.event_type = Input::EventType::kDown,
				.position = ink::stroke_model::Vec2(m.x, m.y),
				.time = Time(0.0)
			};
			modeler.Update(input, smoothed_stroke);

			double baseThickness = 5.0;
			if (eraser) baseThickness = 50.0;

			double minThickness = baseThickness * 0.8; // 0.6/2.4 或 0.4/2.0
			double maxThickness = baseThickness * 1.4;
			double prevThickness = baseThickness;
			double smoothingFactor = 0.2;
			float thicknessAnchorX = static_cast<float>(m.x);
			float thicknessAnchorY = static_cast<float>(m.y);

			strokeBuilder.Begin(StrokeSample{
				static_cast<float>(m.x),
				static_cast<float>(m.y),
				static_cast<float>(baseThickness * 0.5)
			});

			auto appendSmoothedStroke = [&](vector<InkStrokeSegmentData>& drawSegments, RECT& dirtyRect)
				{
					for (; processedSmoothedStrokeCount < smoothed_stroke.size(); ++processedSmoothedStrokeCount)
					{
						const Result& result = smoothed_stroke[processedSmoothedStrokeCount];

						auto rawSpeed = hypot(result.velocity.x, result.velocity.y);
						double ratio = clamp(static_cast<double>(rawSpeed / expected_speed), 0.0, 1.0);
						double targetThickness = minThickness + (1.0 - ratio) * (maxThickness - minThickness);
						double thickness = prevThickness;

						if (hypot(result.position.x - thicknessAnchorX, result.position.y - thicknessAnchorY) >= baseThickness)
						{
							thickness = std::lerp(prevThickness, targetThickness, smoothingFactor);
							thicknessAnchorX = result.position.x;
							thicknessAnchorY = result.position.y;
						}

						StrokeSample sample{
							result.position.x,
							result.position.y,
							static_cast<float>(prevThickness * 0.5)
						};
						strokeBuilder.PushSample(sample, drawSegments, dirtyRect);
						prevThickness = thickness;
					}
				};

			vector<InkStrokeSegmentData> initialSegments;
			RECT initialDirty = RECT(0, 0, 0, 0);
			appendSmoothedStroke(initialSegments, initialDirty);
			RECT previousPreviewDirty = RECT(0, 0, 0, 0);

			auto presentDirtyRect = [&](RECT& dirtyRect, const vector<InkStrokeSegmentData>& previewSegments, const RECT& previewDirty)
				{
					UnionRectInPlace(dirtyRect, previousPreviewDirty);
					UnionRectInPlace(dirtyRect, previewDirty);

					dirtyRect.left = max(0L, dirtyRect.left);
					dirtyRect.top = max(0L, dirtyRect.top);
					dirtyRect.right = min((long)windowInfo.w, dirtyRect.right);
					dirtyRect.bottom = min((long)windowInfo.h, dirtyRect.bottom);

					if (dirtyRect.right <= dirtyRect.left || dirtyRect.bottom <= dirtyRect.top)
					{
						dirtyRect = RECT(0, 0, 0, 0);
						return false;
					}

					inkRenderer.SetOMTarget(inkRenderer.renderTargetView);

					if (!isFirstFrame)
					{
						inkRenderer.CopyResource(inkRenderer.screenTexture, inkRenderer.offScreenTexture1, dirtyRect);
					}
					else
					{
						inkRenderer.context->CopyResource(inkRenderer.screenTexture, inkRenderer.offScreenTexture1);
					}

					if (!previewSegments.empty())
					{
						inkRenderer.SetOMTarget(inkRenderer.renderTargetView);
						inkRenderer.DrawStroke(
							previewSegments,
							XMFLOAT4(1.0f, 0.0f, 0.0f, 0.30f),
							static_cast<float>(g_brushShapeType.load(std::memory_order_relaxed)),
							eraser
						);
					}

					if (!isFirstFrame)
					{
						DXGI_PRESENT_PARAMETERS parameters = {};
						parameters.DirtyRectsCount = 1;
						parameters.pDirtyRects = &dirtyRect;
						parameters.pScrollRect = nullptr;
						parameters.pScrollOffset = nullptr;

						swapChain->Present1(0, 0, &parameters);
					}
					else
					{
						swapChain->Present(0, 0);
					}

					isFirstFrame = false;
					previousPreviewDirty = previewDirty;
					return true;
				};

			// 帧率保持
			chrono::high_resolution_clock::time_point rekon;
			while (1)
			{
				if (g_clearCanvasRequested.exchange(false, std::memory_order_relaxed))
				{
					clearCanvas();
					previousPreviewDirty = RECT(0, 0, 0, 0);
				}

				rekon = chrono::high_resolution_clock::now();
				current = RECT(0, 0, 0, 0);

				inkRenderer.SetOMTarget(inkRenderer.offScreenTexture1RTV);

				POINT pt;
				GetCursorPos(&pt);
				ScreenToClient(windowHWND, &pt);

				Input input
				{
					.event_type = Input::EventType::kMove,
					.position = ink::stroke_model::Vec2(pt.x, pt.y),
					.time = Time(chrono::duration<double>(chrono::high_resolution_clock::now() - start).count()) // 秒单位
				};
				vector<InkStrokeSegmentData> drawSegments;

				modeler.Update(input, smoothed_stroke);
				modeler.Predict(predicted_stroke);
				appendSmoothedStroke(drawSegments, current);
				if (!drawSegments.empty())
				{
					inkRenderer.DrawStroke(
						drawSegments,
						XMFLOAT4(1.0f, 0.0f, 0.0f, 0.30f),
						static_cast<float>(g_brushShapeType.load(std::memory_order_relaxed)),
						eraser
					);
				}

				if (!predicted_stroke.empty())
				{
					// TODO
				}

				vector<InkStrokeSegmentData> previewSegments;
				RECT previewDirty = RECT(0, 0, 0, 0);
				strokeBuilder.EmitPreview(
					StrokeSample{
						static_cast<float>(pt.x),
						static_cast<float>(pt.y),
						static_cast<float>(prevThickness * 0.5)
					},
					previewSegments,
					previewDirty
				);

				presentDirtyRect(current, previewSegments, previewDirty);

				if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) break;
				hiex::flushmessage_win32(EM_MOUSE, windowHWND);

				// 帧率锁
				{
					double costMs = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - rekon).count();

					// 直接传入 ms，无需转换
					HighPrecisionWait(costMs, sampling_rate_hz);

					// 计算总帧时间用于显示实际 FPS
					double totalMs = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - rekon).count();

					// 防止除以0
					int logicFPS = (costMs > 0.001) ? static_cast<int>(1000.0 / costMs) : 9999;
					int actualFPS = (totalMs > 0.001) ? static_cast<int>(1000.0 / totalMs) : 9999;

					cout << processedSmoothedStrokeCount
						<< " logic: " << logicFPS << " FPS (" << costMs << "ms)"
						<< " real: " << actualFPS << " FPS"
						<< endl;
				}
			}

			current = RECT(0, 0, 0, 0);
			inkRenderer.SetOMTarget(inkRenderer.offScreenTexture1RTV);

			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(windowHWND, &pt);

			Input upInput
			{
				.event_type = Input::EventType::kUp,
				.position = ink::stroke_model::Vec2(pt.x, pt.y),
				.time = Time(chrono::duration<double>(chrono::high_resolution_clock::now() - start).count())
			};
			modeler.Update(upInput, smoothed_stroke);

			vector<InkStrokeSegmentData> finalSegments;
			appendSmoothedStroke(finalSegments, current);
			strokeBuilder.Finish(finalSegments, current);

			if (!finalSegments.empty())
			{
				inkRenderer.DrawStroke(
					finalSegments,
					XMFLOAT4(1.0f, 0.0f, 0.0f, 0.30f),
					static_cast<float>(g_brushShapeType.load(std::memory_order_relaxed)),
					eraser
				);
			}
			presentDirtyRect(current, {}, RECT(0, 0, 0, 0));

			hiex::flushmessage_win32(EM_MOUSE, windowHWND);
		}
	}

	getmessage(EM_KEY);
	return 0;
}
