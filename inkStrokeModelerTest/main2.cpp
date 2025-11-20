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
#include <d2d1_1.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")

using namespace std;
using namespace ink::stroke_model;

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

class WindowInfoClass
{
public:
	int w = 2000;
	int h = 1000;
}windowInfo;

CComPtr<ID2D1Factory1> d2dFactory1;
CComPtr<ID3D11Device> d3dDevice_HARDWARE;
CComPtr<ID2D1Device> d2dDevice_HARDWARE;

HWND windowHWND;
IMAGE drawpadImage;

int main()
{
	timeBeginPeriod(1); // 全局高精度计时器

	// 初始化工厂
	{
		ID2D1Factory1* tmpFactory = nullptr;
		D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), NULL, (IID_PPV_ARGS(&tmpFactory)));
		d2dFactory1.Attach(tmpFactory);
	}
	// 初始化 D3D 设备
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
			nullptr                     // 返回设备上下文 (我们不需要)
		);

		CComPtr<IDXGIDevice> dxgiDevice;
		d3dDevice_HARDWARE.QueryInterface(&dxgiDevice);

		d2dFactory1->CreateDevice(dxgiDevice, &d2dDevice_HARDWARE);
	}

	// 窗口创建
	{
		windowHWND = hiex::initgraph_win32(windowInfo.w, windowInfo.h);
	}

	// DC
	CComPtr<ID2D1DeviceContext> d2dDeviceContext;
	{
		d2dDevice_HARDWARE->CreateDeviceContext(
			D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
			&d2dDeviceContext
		);
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
	// D2D Bitmap
	{
		CComPtr<IDXGISurface> dxgiBackBuffer;
		swapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&dxgiBackBuffer);

		D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
		);

		CComPtr<ID2D1Bitmap1> d2dTargetBitmap;
		d2dDeviceContext->CreateBitmapFromDxgiSurface(
			dxgiBackBuffer,
			&bitmapProperties,
			&d2dTargetBitmap
		);

		d2dDeviceContext->SetTarget(d2dTargetBitmap);
	}

	// 简单的 DPI 初始化
	int dpiX;
	{
		HDC screen = GetDC(nullptr);
		dpiX = GetDeviceCaps(screen, LOGPIXELSX);
		ReleaseDC(nullptr, screen);
	}

	// 初始调测参数
	const bool debug = true;
	const float sampling_rate_hz = 60.0f; // Hz
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

	// 绘制初始化
	{
		// 图形抗锯齿
		d2dDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		// d2dDeviceContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE); // 文本抗锯齿

		d2dDeviceContext->BeginDraw();

		d2dDeviceContext->Clear(D2D1::ColorF(D2D1::ColorF::White));

		// 绘制一段墨迹（测试）
		// 样式为两端粗细不同的线段，起始端是凸出，而终点端是凹入的，结合切线计算和路径闭合
		// 需要注意的是：当前代码设计之初就没有考虑过内涵的情况，并且不应该会出现这种粗细变化case，需要在后面模拟压感阶段避免

		// 首先需要准备 (x1,y1,r1) (x2,y2,r2) 两个端点位置和半径

		float x1 = 800.0f;
		float y1 = 100.0f;
		float r1 = 25.0f;

		float x2 = 500.0f;
		float y2 = 500.0f;
		float r2 = 150.0f;

		{
			// 圆心间距离
			double dist = std::hypot(x2 - x1, y2 - y1);

			// 保证不是内涵的情况
			if (dist > abs(r1 - r2))
			{
				// 基准角度
				double base_angle = std::atan2(y2 - y1, x2 - x1);

				// 偏移角 alpha
				// cos(alpha) = (r1 - r2) / d
				// 这里的参数必须在 [-1, 1] 之间，前面的 if 检查保证了这一点
				double alpha = acos((r1 - r2) / dist);

				// 切线角度
				double angle1 = base_angle + alpha;
				double angle2 = base_angle - alpha;

				float tp1x = x1 + r1 * cos(angle1);
				float tp1y = y1 + r1 * sin(angle1);
				float tp2x = x1 + r1 * cos(angle2);
				float tp2y = y1 + r1 * sin(angle2);

				float ep1x = x2 + r2 * cos(angle1);
				float ep1y = y2 + r2 * sin(angle1);
				float ep2x = x2 + r2 * cos(angle2);
				float ep2y = y2 + r2 * sin(angle2);

				// 计算路径

				CComPtr<ID2D1PathGeometry> path;
				CComPtr<ID2D1GeometrySink> sink;
				d2dFactory1->CreatePathGeometry(&path);
				path->Open(&sink);

				sink->BeginFigure(D2D1::Point2F(tp1x, tp1y), D2D1_FIGURE_BEGIN_FILLED);

				// 起始端圆弧
				{
					// 计算绘制的为优弧/劣弧
					D2D1_ARC_SIZE arcSize = (r1 > r2) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

					// 推算绘制线顺/逆时针

					// 2. 判断方向 (SweepDirection)
					// 计算 向量Start(u) 和 向量End(v) 的叉积 (CP)
					// u = start - center, v = end - center
					float ux = tp1x - x1;
					float uy = tp1y - y1;
					float vx = tp2x - x1;
					float vy = tp2y - y1;

					// 叉积 CP = x1*y2 - x2*y1
					float cp = ux * vy - uy * vx;

					// 在 Direct2D 屏幕坐标系中 (Y轴向下)：
					// CP > 0 表示 从 Start 到 End 是 "顺时针" (Clockwise) 的最短路径
					// CP < 0 表示 从 Start 到 End 是 "逆时针" (Counter-Clockwise) 的最短路径

					D2D1_SWEEP_DIRECTION direction;
					if (arcSize == D2D1_ARC_SIZE_SMALL)
					{
						// 如果是小弧，直接走最短路径
						direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
							: D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
					}
					else
					{
						// 如果是大弧，我们要走长的那条路，所以取反
						direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
							: D2D1_SWEEP_DIRECTION_CLOCKWISE;
					}

					sink->AddArc(D2D1::ArcSegment(
						D2D1::Point2F(tp2x, tp2y),
						D2D1::SizeF(r1, r1),
						0.0f,
						direction,
						arcSize
					));
				}

				sink->AddLine(D2D1::Point2F(ep2x, ep2y));

				// 终点端圆弧
				{
					// 计算绘制的为优弧/劣弧 -> 我们需要取反（向内凹）
					D2D1_ARC_SIZE arcSize = (r2 > r1) ? D2D1_ARC_SIZE_SMALL : D2D1_ARC_SIZE_LARGE;

					// 推算绘制线顺/逆时针

					// 2. 判断方向 (SweepDirection)
					// 计算 向量Start(u) 和 向量End(v) 的叉积 (CP)
					// u = start - center, v = end - center
					float ux = ep2x - x2;
					float uy = ep2y - y2;
					float vx = ep1x - x2;
					float vy = ep1y - y2;

					// 叉积 CP = x1*y2 - x2*y1
					float cp = ux * vy - uy * vx;

					// 在 Direct2D 屏幕坐标系中 (Y轴向下)：
					// CP > 0 表示 从 Start 到 End 是 "顺时针" (Clockwise) 的最短路径
					// CP < 0 表示 从 Start 到 End 是 "逆时针" (Counter-Clockwise) 的最短路径
					// 我们需要取反

					D2D1_SWEEP_DIRECTION direction;
					if (arcSize == D2D1_ARC_SIZE_SMALL)
					{
						// 如果是小弧，直接走最短路径
						direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
							: D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
					}
					else
					{
						// 如果是大弧，我们要走长的那条路，所以取反
						direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
							: D2D1_SWEEP_DIRECTION_CLOCKWISE;
					}

					sink->AddArc(D2D1::ArcSegment(
						D2D1::Point2F(ep1x, ep1y),
						D2D1::SizeF(r2, r2),
						0.0f,
						direction,
						arcSize
					));
				}

				sink->AddLine(D2D1::Point2F(tp1x, tp1y));

				sink->EndFigure(D2D1_FIGURE_END_CLOSED);
				sink->Close();

				// 绘制

				CComPtr<ID2D1SolidColorBrush> strokeBrush;
				d2dDeviceContext->CreateSolidColorBrush(
					D2D1::ColorF(D2D1::ColorF::Red),
					&strokeBrush
				);

				d2dDeviceContext->FillGeometry(path, strokeBrush);
			}
		}

		d2dDeviceContext->EndDraw();
		swapChain->Present(0, 0); // 第一个参数为 1 则开启垂直同步
	}

	getmessage(EM_KEY);

#ifdef AAA

	ExMessage m{};
	while (true)
	{
		hiex::getmessage_win32(&m, EM_MOUSE, windowHWND);

		if (m.message == WM_LBUTTONDOWN)
		{
			//params.prediction_params = kalman_predictor_params;
			params.prediction_params = StrokeEndPredictorParams();

			if (absl::Status status = modeler.Reset(params); !status.ok())
			{
				cout << "Error: " << status.message() << endl;
			}

			vector<Result> smoothed_stroke;
			vector<Result> predicted_stroke;
			size_t tot = 0;

			float xO = m.x;
			float yO = m.y;

			float xT = m.x;
			float yT = m.y;

			chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();

			Input input
			{
				.event_type = Input::EventType::kDown,
				.position = ink::stroke_model::Vec2(xO,yO),
				.time = Time(0.0)
			};
			modeler.Update(input, smoothed_stroke);

			double baseThickness = 5.0;
			double minThickness = baseThickness * 0.8; // 0.6/2.4 或 0.4/2.0
			double maxThickness = baseThickness * 1.4;
			double prevThickness = baseThickness;
			double smoothingFactor = 0.2;

			// 帧率保持
			chrono::high_resolution_clock::time_point rekon;
			while (1)
			{
				rekon = chrono::high_resolution_clock::now();
				SetImageColor(*hiex::GetWindowImage(windowHWND), RGBA(255, 255, 255, 255), false);
				SetImageColor(predictorImage, RGBA(0, 0, 0, 0), true);

				POINT pt;
				GetCursorPos(&pt);
				ScreenToClient(windowHWND, &pt);

				if (debug)
				{
					Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(RGBA(0, 0, 255, 50), true), 3.0F);
					pen.SetStartCap(Gdiplus::LineCapRound), pen.SetEndCap(Gdiplus::LineCapRound);

					graphics.DrawLine(&pen,
						xT,
						yT,
						static_cast<float>(pt.x),
						static_cast<float>(pt.y));

					xT = pt.x;
					yT = pt.y;
				}

				Input input
				{
					.event_type = Input::EventType::kMove,
					.position = ink::stroke_model::Vec2(pt.x, pt.y),
					.time = Time(chrono::duration<double>(chrono::high_resolution_clock::now() - start).count()) // 秒单位
				};

				modeler.Update(input, smoothed_stroke);
				modeler.Predict(predicted_stroke);
				if (!smoothed_stroke.empty() && (xO != smoothed_stroke.back().position.x || yO != smoothed_stroke.back().position.y))
				{
					// 用于粗细平滑
					float xI = xO;
					float yI = yO;

					for (size_t i = tot; i < smoothed_stroke.size() - 1; i++)
					{
						bool isStroke = false;
						if (smoothed_stroke.size() - tot <= strokes_num) isStroke = true;

						if (!isStroke) tot = i;

						/*graphics.DrawLine(&pen,
							smoothed_stroke[i].position.x,
							smoothed_stroke[i].position.y,
							smoothed_stroke[i + 1].position.x,
							smoothed_stroke[i + 1].position.y);*/

						auto rawSpeed = hypot(smoothed_stroke[i + 1].velocity.x, smoothed_stroke[i + 1].velocity.y);
						double ratio = clamp(static_cast<double>(rawSpeed / expected_speed), 0.0, 1.0);
						double targetThickness = minThickness + (1.0 - ratio) * (maxThickness - minThickness);
						double thickness = prevThickness;

						if (hypot(smoothed_stroke[i + 1].position.x - xI, smoothed_stroke[i + 1].position.y - yI) >= baseThickness)
						{
							thickness = std::lerp(prevThickness, targetThickness, smoothingFactor);
							xI = smoothed_stroke[i + 1].position.x;
							yI = smoothed_stroke[i + 1].position.y;
						}

						// cout << "= " << rawSpeed << ":" << ratio << ", " << thickness << endl;

						{
							float x1 = smoothed_stroke[i].position.x, y1 = smoothed_stroke[i].position.y;
							float x2 = smoothed_stroke[i + 1].position.x, y2 = smoothed_stroke[i + 1].position.y;
							float w1 = static_cast<float>(prevThickness), w2 = static_cast<float>(thickness);
							Gdiplus::Color color;

							if (debug)
							{
								if (!isStroke) color = hiex::ConvertToGdiplusColor(RGB(255, 0, 0), false);
								else color = hiex::ConvertToGdiplusColor(RGB(0, 0, 255), false);
							}
							else color = hiex::ConvertToGdiplusColor(RGB(255, 0, 0), false);

							// 方向向量
							float dx = x2 - x1;
							float dy = y2 - y1;
							float len = hypot(dx, dy);

							if (len < 1e-6f);
							else
							{
								// 单位法向量
								float nx = -dy / len;
								float ny = dx / len;

								// 半径
								float r1 = w1 / 2.0f;
								float r2 = w2 / 2.0f;

								// 主体梯形四个顶点
								Gdiplus::PointF p1(x1 + nx * r1, y1 + ny * r1); // 起点左
								Gdiplus::PointF p2(x2 + nx * r2, y2 + ny * r2); // 终点左
								Gdiplus::PointF p3(x2 - nx * r2, y2 - ny * r2); // 终点右
								Gdiplus::PointF p4(x1 - nx * r1, y1 - ny * r1); // 起点右

								Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);

								// 主体梯形
								Gdiplus::PointF trapezoid[] = { p1, p2, p3, p4 };
								path.AddPolygon(trapezoid, 4);

								// 起点凸圆帽
								{
									Gdiplus::RectF arcRect(x1 - r1, y1 - r1, w1, w1);
									// 弧的起始角度取决于方向
									float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
									path.AddArc(arcRect, angle + 90, 180); // 半圆
									path.CloseFigure();
								}

								// 终点凹圆帽
								{
									Gdiplus::RectF arcRect(x2 - r2, y2 - r2, w2, w2);
									float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
									path.AddArc(arcRect, angle - 90, 180); // 半圆
									path.CloseFigure();
								}

								// 使用颜色填充
								Gdiplus::SolidBrush brush(color);

								if (!isStroke) graphics.FillPath(&brush, &path);
								else
								{
									Gdiplus::Graphics graphics(GetImageHDC(&predictorImage));
									graphics.FillPath(&brush, &path);
								}
							}
						}

						prevThickness = thickness;
					}
				}
				if (!smoothed_stroke.empty())
				{
					xO = smoothed_stroke.back().position.x;
					yO = smoothed_stroke.back().position.y;

					BEGIN_TASK();

					hiex::TransparentImage(hiex::GetWindowImage(windowHWND), 0, 0, &drawpadImage);

					END_TASK();
				}

				if (!predicted_stroke.empty())
				{
					Gdiplus::Graphics graphics(GetImageHDC(&predictorImage));

					Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(RGB(0, 255, 0), false), prevThickness);
					if (debug) pen.SetColor(hiex::ConvertToGdiplusColor(RGB(0, 255, 0), false));
					else pen.SetColor(hiex::ConvertToGdiplusColor(RGB(255, 0, 0), false));

					pen.SetStartCap(Gdiplus::LineCapRound), pen.SetEndCap(Gdiplus::LineCapRound);

					graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
					graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

					graphics.DrawLine(&pen,
						xO,
						yO,
						predicted_stroke.front().position.x,
						predicted_stroke.front().position.y);
					for (size_t i = 0; i < predicted_stroke.size() - 1; i++)
					{
						graphics.DrawLine(&pen,
							predicted_stroke[i].position.x,
							predicted_stroke[i].position.y,
							predicted_stroke[i + 1].position.x,
							predicted_stroke[i + 1].position.y);
					}

					BEGIN_TASK();

					hiex::TransparentImage(hiex::GetWindowImage(windowHWND), 0, 0, &predictorImage);

					END_TASK();
				}
				REDRAW_WINDOW();

				if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) break;
				hiex::flushmessage_win32(EM_MOUSE, windowHWND);

				// 帧率锁
				{
					auto tmp = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - rekon).count();

					// 60Hz
					double delay = 1000.0 / static_cast<double>(sampling_rate_hz) - tmp;
					if (delay >= 0.0) this_thread::sleep_for(chrono::milliseconds(static_cast<long long>(delay)));

					cout << tot << " " << tmp << "ms " << static_cast<int>(1000.0 / tmp) << "fps" << endl;
				}
			}

			{
				POINT pt;
				GetCursorPos(&pt);
				ScreenToClient(windowHWND, &pt);

				Input input
				{
					.event_type = Input::EventType::kUp,
					.position = ink::stroke_model::Vec2(pt.x,pt.y),
					.time = Time(chrono::duration<double>(chrono::high_resolution_clock::now() - start).count())
				};
				modeler.Update(input, smoothed_stroke);

				if (!smoothed_stroke.empty() && (xO != smoothed_stroke.back().position.x || yO != smoothed_stroke.back().position.y))
				{
					// 用于粗细平滑
					float xI = xO;
					float yI = yO;

					for (size_t i = tot; i < smoothed_stroke.size() - 1; i++)
					{
						tot = smoothed_stroke.size() - 1;

						auto rawSpeed = hypot(smoothed_stroke[i + 1].velocity.x, smoothed_stroke[i + 1].velocity.y);
						double ratio = clamp(static_cast<double>(rawSpeed / expected_speed), 0.0, 1.0);
						double targetThickness = minThickness + (1.0 - ratio) * (maxThickness - minThickness);
						double thickness = prevThickness;

						if (hypot(smoothed_stroke[i + 1].position.x - xI, smoothed_stroke[i + 1].position.y - yI) >= baseThickness)
						{
							thickness = std::lerp(prevThickness, targetThickness, smoothingFactor);
							xI = smoothed_stroke[i + 1].position.x;
							yI = smoothed_stroke[i + 1].position.y;
						}

						// cout << "= " << rawSpeed << ":" << ratio << ", " << thickness << endl;

						{
							float x1 = smoothed_stroke[i].position.x, y1 = smoothed_stroke[i].position.y;
							float x2 = smoothed_stroke[i + 1].position.x, y2 = smoothed_stroke[i + 1].position.y;
							float w1 = static_cast<float>(prevThickness), w2 = static_cast<float>(thickness);
							Gdiplus::Color color = hiex::ConvertToGdiplusColor(RGB(255, 0, 0), false);

							// 方向向量
							float dx = x2 - x1;
							float dy = y2 - y1;
							float len = hypot(dx, dy);

							if (len < 1e-6f);
							else
							{
								// 单位法向量
								float nx = -dy / len;
								float ny = dx / len;

								// 半径
								float r1 = w1 / 2.0f;
								float r2 = w2 / 2.0f;

								// 主体梯形四个顶点
								Gdiplus::PointF p1(x1 + nx * r1, y1 + ny * r1); // 起点左
								Gdiplus::PointF p2(x2 + nx * r2, y2 + ny * r2); // 终点左
								Gdiplus::PointF p3(x2 - nx * r2, y2 - ny * r2); // 终点右
								Gdiplus::PointF p4(x1 - nx * r1, y1 - ny * r1); // 起点右

								Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);

								// 主体梯形
								Gdiplus::PointF trapezoid[] = { p1, p2, p3, p4 };
								path.AddPolygon(trapezoid, 4);

								// 起点凸圆帽
								{
									Gdiplus::RectF arcRect(x1 - r1, y1 - r1, w1, w1);
									// 弧的起始角度取决于方向
									float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
									path.AddArc(arcRect, angle + 90, 180); // 半圆
									path.CloseFigure();
								}

								// 终点凹圆帽
								{
									Gdiplus::RectF arcRect(x2 - r2, y2 - r2, w2, w2);
									float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
									path.AddArc(arcRect, angle - 90, 180); // 半圆
									path.CloseFigure();
								}

								// 使用颜色填充
								Gdiplus::SolidBrush brush(color);
								graphics.FillPath(&brush, &path);
							}
						}

						prevThickness = thickness;
					}
				}

				BEGIN_TASK();

				SetImageColor(*hiex::GetWindowImage(windowHWND), RGBA(255, 255, 255, 255), false);
				hiex::TransparentImage(hiex::GetWindowImage(windowHWND), 0, 0, &drawpadImage);

				END_TASK();
				REDRAW_WINDOW();
			}

			hiex::flushmessage_win32(EM_MOUSE, windowHWND);
		}
	}

#endif

	return 0;
}

// 废弃代码
/*
		// 调测表示起点终点位置
		{
			CComPtr<ID2D1SolidColorBrush> strokeBrush;
			d2dDeviceContext->CreateSolidColorBrush(
				D2D1::ColorF(D2D1::ColorF::Black),
				&strokeBrush
			);

			{
				D2D1_ELLIPSE circle = D2D1::Ellipse(
					D2D1::Point2F(x1, y1),
					r1,
					r1
				);

				d2dDeviceContext->DrawEllipse(
					&circle,
					strokeBrush,
					5.0f
				);
			}
			{
				D2D1_ELLIPSE circle = D2D1::Ellipse(
					D2D1::Point2F(x2, y2),
					r2,
					r2
				);

				d2dDeviceContext->DrawEllipse(
					&circle,
					strokeBrush,
					5.0f
				);
			}
		}
*/
/*
		{
			D2D1_POINT_2F p0 = D2D1::Point2F(100.0f, 100.0f);
			D2D1_POINT_2F p1 = D2D1::Point2F(500.0f, 500.0f);

			const float startWidth = 50.0f;
			const float endWidth = 300.0f;

			// 2. 方向和法线
			D2D1_POINT_2F dir = { p1.x - p0.x, p1.y - p0.y };
			float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);

			dir.x /= len;
			dir.y /= len;

			// 法线（垂直方向）
			D2D1_POINT_2F n = { -dir.y, dir.x };

			float r0 = startWidth * 0.5f;
			float r1 = endWidth * 0.5f;

			// 3. 四个边缘点（构成一个不等宽的四边形）
			D2D1_POINT_2F p0L = { p0.x + n.x * r0, p0.y + n.y * r0 };
			D2D1_POINT_2F p0R = { p0.x - n.x * r0, p0.y - n.y * r0 };
			D2D1_POINT_2F p1L = { p1.x + n.x * r1, p1.y + n.y * r1 };
			D2D1_POINT_2F p1R = { p1.x - n.x * r1, p1.y - n.y * r1 };

			// 4. 几何：中间是四边形，两头各加一个圆弧
			CComPtr<ID2D1PathGeometry> path;
			CComPtr<ID2D1GeometrySink> sink;
			d2dFactory1->CreatePathGeometry(&path);
			path->Open(&sink);

			// 起点圆头：从 p0R -> p0L 的半圆
			sink->BeginFigure(p0R, D2D1_FIGURE_BEGIN_FILLED);
			// 半圆可以用两个贝塞尔曲线近似，这里用简化方案：直接用圆弧
			sink->AddArc(D2D1::ArcSegment(
				p0L,
				D2D1::SizeF(r0, r0),
				0.0f,
				D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
				D2D1_ARC_SIZE_SMALL
			));

			// 连接到终点左侧
			sink->AddLine(p1L);

			// 终点圆头：从 p1L -> p1R 的半圆
			sink->AddArc(D2D1::ArcSegment(
				p1R,
				D2D1::SizeF(r1, r1),
				0.0f,
				D2D1_SWEEP_DIRECTION_CLOCKWISE,
				D2D1_ARC_SIZE_SMALL
			));

			// 回到起点右侧，闭合
			sink->AddLine(p0R);

			sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			sink->Close();

			// 5. 画出来（填充几何）
			CComPtr<ID2D1SolidColorBrush> lineBrush;
			d2dDeviceContext->CreateSolidColorBrush(
				D2D1::ColorF(D2D1::ColorF::Red),
				&lineBrush
			);

			d2dDeviceContext->FillGeometry(path, lineBrush);
		}
*/
/*
	drawpadImage = CreateImageColor(2000, 1000, RGBA(0, 0, 0, 0), true);
	IMAGE predictorImage(2000, 1000);

	setbkcolor(RGB(255, 255, 255));
	cleardevice();

	hiex::Gdiplus_Try_Starup();
	Gdiplus::Graphics graphics(GetImageHDC(&drawpadImage));
	graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

	{
		{
			float x1 = 100.0f, y1 = 100.0f;
			float x2 = 500.0f, y2 = 500.0f;
			float w1 = 50.0f, w2 = 10.0f;
			Gdiplus::Color color = hiex::ConvertToGdiplusColor(RGB(255, 0, 0), false);

			// 方向向量
							float dx = x2 - x1;
							float dy = y2 - y1;
							float len = hypot(dx, dy);

							if (len < 1e-6f);
							else
							{
								// 单位法向量
								float nx = -dy / len;
								float ny = dx / len;

								// 半径
								float r1 = w1 / 2.0f;
								float r2 = w2 / 2.0f;

								// 主体梯形四个顶点
								Gdiplus::PointF p1(x1 + nx * r1, y1 + ny * r1); // 起点左
								Gdiplus::PointF p2(x2 + nx * r2, y2 + ny * r2); // 终点左
								Gdiplus::PointF p3(x2 - nx * r2, y2 - ny * r2); // 终点右
								Gdiplus::PointF p4(x1 - nx * r1, y1 - ny * r1); // 起点右

								Gdiplus::GraphicsPath mainPath(Gdiplus::FillModeWinding);
								Gdiplus::PointF trapezoid[] = { p1, p2, p3, p4 };
								mainPath.AddPolygon(trapezoid, 4);

								// 起点凸帽
								{
									Gdiplus::RectF arcRect(x1 - r1, y1 - r1, w1, w1);
									float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
									mainPath.AddArc(arcRect, angle + 90, 180);
									mainPath.CloseFigure();
								}

								// 构造成 Region
								Gdiplus::Region region(&mainPath);

								// 终点凹帽（挖洞）
								{
									Gdiplus::GraphicsPath hole;
									Gdiplus::RectF arcRect(x2 - r2, y2 - r2, w2, w2);
									float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
									hole.AddArc(arcRect, angle - 90, 360); // 顺着线段方向的半圆
									hole.AddArc(arcRect, angle + 90, -180); // 回到起点闭合
									hole.CloseFigure();

									//region.Exclude(&hole); // 从主体中挖掉这个半圆
								}

								Gdiplus::SolidBrush brush(color);
								graphics.FillRegion(&brush, &region);
							}
		}

		BEGIN_TASK();

		hiex::TransparentImage(hiex::GetWindowImage(windowHWND), 0, 0, &drawpadImage);

		END_TASK();
		REDRAW_WINDOW();
	}
*/

// 翻译示例
/*
		// 传入笔触建模器的输入数据。
		struct Input {
		  // 输入所代表的事件类型。
		  // "kDown" 事件表示笔触的开始，"kUp" 事件表示笔触的结束，
		  // 而介于两者之间的所有事件均为 "kMove" 事件。
		  enum class EventType { kDown, kMove, kUp };
		  EventType event_type;

		  // 输入的位置。
		  Vec2 position{0};

		  // 输入发生的时间。
		  Time time{0};

		  // 施加在触控笔上的压力值。
		  // 预期值范围为 [0, 1]。负值表示压力未知。
		  float pressure = -1;

		  // 触控笔与设备屏幕平面之间的夹角。
		  // 预期值范围为 [0, π/2]。值为 0 表示触控笔垂直于屏幕，
		  // 而值为 π/2 表示其与屏幕齐平。负值表示倾斜角未知。
		  float tilt = -1;

		  // 触控笔在屏幕上的投影与 X 轴正方向之间的夹角，按逆时针方向测量。
		  // 预期值范围为 [0, 2π)。负值表示方位角未知。
		  float orientation = -1;

		  friend bool operator==(const Input&, const Input&) = default;
		};

		// 由笔触建模器生成的建模结果。
		struct Result {
		  // 笔尖的位置、速度和加速度。
		  Vec2 position{0, 0};
		  Vec2 velocity{0, 0};
		  Vec2 acceleration{0, 0};

		  // 此结果数据发生的时间。
		  Time time{0};

		  // 触控笔的压力、倾斜角和方位角。
		  // 更多信息请参阅 Input 结构体中的相应字段。
		  float pressure = -1;
		  float tilt = -1;
		  float orientation = -1;

		  friend bool operator==(const Result&, const Result&) = default;
		};

		// 该结构体表明应使用“笔划结束”预测策略，
		// 该策略将预测建模为：最后一次观测到的输入即为笔划的终点。
		// 它实际上没有任何可调参数；它使用与整体模型相同的
		// PositionModelerParams 和 SamplingParams。请注意，此“预测”
		// 实际上并不会对未来进行实质性预测，它仅用于非常快速地“追赶”上
		// 原始输入的位置。
		struct StrokeEndPredictorParams {};

		// 该结构体表明应使用基于卡尔曼滤波器的预测策略，
		// 并提供了用于调整该策略的参数。
		//
		// 与“笔划结束”预测器不同，该策略除了执行“追赶”步骤外，
		// 还能预测出笔划超出最后一个输入位置的延伸部分。
		struct KalmanPredictorParams {
		  // 笔划本身固有噪声的方差。
		  double process_noise = -1;

		  // 由笔划测量误差所引起的噪声的方差。
		  double measurement_noise = -1;

		  // 在卡尔曼预测器被认为足够稳定以进行预测之前，所需接收的最小输入数。
		  int min_stable_iteration = 4;

		  // 卡尔曼滤波器假定输入是按统一的时间步长接收的，但情况并非总是如此。
		  // 我们会保留最近的输入时间戳，用于计算由此产生的校正值。
		  // 此参数决定了要保存的时间戳的最大数量。
		  int max_time_samples = 20;

		  // “追赶”部分预测所允许的最小速度，该速度用于弥合最后一次的结果
		  // （即最后修正过的位置）与最后一次的输入之间的距离。
		  //
		  // 一个好的初始值建议设为比预期输入速度小 3 个数量级。
		  float min_catchup_velocity = -1;

		  // 这些权重应用于三次预测多项式中的加速度 (x²) 和加加速度/冲击 (x³) 项。
		  // 它们的值越接近零，预测就越趋于线性。
		  float acceleration_weight = .5;
		  float jerk_weight = .1;

		  // 此值是对预测器的一个提示，指示了预测中超出最后一个输入位置的
		  // 延伸部分所期望的持续时间。根据预测器的置信度，
		  // 该部分预测的实际持续时间可能会小于此值，但绝不会超过它。
		  Duration prediction_interval{-1};

		  // 卡尔曼预测器使用多种启发式方法来评估预测的置信度。
		  // 每种启发式方法都会产生一个介于 0 和 1 之间的置信度值，
		  // 然后我们将它们的乘积作为总置信度。
		  // 以下参数可用于调整这些启发式方法。
		  struct ConfidenceParams {
			// 第一种启发式方法会随着我们接收到更多样本（即输入点）而简单地增加置信度。
			// 它在没有样本时评估为 0，在达到 desired_number_of_samples 时评估为 1。
			int desired_number_of_samples = 20;

			// 第二种启发式方法基于最后一个样本与当前估计值之间的距离。
			// 如果距离为 0，则评估为 1；如果距离大于或等于 max_estimation_distance，
			// 则评估为 0。
			//
			// 一个好的初始值建议设为 measurement_noise 的 1.5 倍。
			float max_estimation_distance = -1;

			// 第三种启发式方法基于预测的速度，该速度通过测量从预测起点到
			// （假设其延伸了完整的 prediction_interval）投影终点的距离来近似计算。
			// 它在速度为 min_travel_speed 时评估为 0，在 max_travel_speed 时评估为 1。
			//
			// 建议的初始值可分别设为预期输入速度的 5% 和 25%。
			float min_travel_speed = -1;
			float max_travel_speed = -1;

			// 第四种启发式方法基于预测的线性度，该线性度通过比较预测的终点与
			// 线性预测的终点（同样，延伸完整的 prediction_interval）来近似计算。
			// 它在距离为零时评估为 1，在距离达到 max_linear_deviation 时评估为
			// baseline_linearity_confidence。
			//
			// 一个好的初始值建议设为 measurement_noise 的 10 倍。
			float max_linear_deviation = -1;
			float baseline_linearity_confidence = .4;
		  };
		  ConfidenceParams confidence_params;
		};

		// 用于表明不应使用任何预测策略的类型。
		// 尝试将此设置与预测功能结合使用会导致错误。
		struct DisabledPredictorParams {};
		*///////////////////////////////////////////