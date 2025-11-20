// InkShader.hlsl

// 常量缓冲区：CPU 每帧更新一次，告诉 GPU 屏幕有多大
cbuffer ScreenBuffer : register(b0)
{
    float screenWidth;
    float screenHeight;
    float2 padding; // 必须凑齐 16 字节对齐
};

// 输入数据结构：C++ 传进来的单个顶点格式
struct VS_INPUT
{
    float2 pos : POSITION; // 像素坐标 (x, y)
    float4 color : COLOR; // 颜色 (r, g, b, a)
};

// 传递数据结构：VS 传给 PS 的数据
struct PS_INPUT
{
    float4 pos : SV_POSITION; // 裁剪空间坐标
    float4 color : COLOR; // 颜色
};

// --- 顶点着色器 (Vertex Shader) ---
PS_INPUT VS(VS_INPUT input)
{
    PS_INPUT output;

    // 坐标转换核心公式：
    // 1. input.pos.x / screenWidth  -> 得到 0.0 ~ 1.0
    // 2. * 2.0 - 1.0                -> 得到 -1.0 ~ 1.0
    // 3. Y 轴需要取反，因为 Direct3D 的 Y 轴正方向是向上的，而屏幕坐标是向下的。

    float x = (input.pos.x / screenWidth) * 2.0 - 1.0;
    float y = -((input.pos.y / screenHeight) * 2.0 - 1.0);

    output.pos = float4(x, y, 0.0, 1.0);
    output.color = input.color; // 颜色透传

    return output;
}

// --- 像素着色器 (Pixel Shader) ---
float4 PS(PS_INPUT input) : SV_Target
{
    // 目前仅仅是输出插值后的颜色
    // 之后我们会在这个函数里写圆角、抗锯齿的数学公式
    return input.color;
}