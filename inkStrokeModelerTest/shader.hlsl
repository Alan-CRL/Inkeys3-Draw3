// shader.hlsl

cbuffer ScreenBuffer : register(b0)
{
    float screenWidth;
    float screenHeight;
    float2 padding;
};

struct VS_INPUT
{
    float2 pos : POSITION;
    float4 color : COLOR;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

// --- Vertex Shader ---
PS_INPUT VS(VS_INPUT input)
{
    PS_INPUT output;

    // 1. 计算屏幕 NDC 坐标 (用于定位)
    float x = (input.pos.x / screenWidth) * 2.0 - 1.0;
    float y = -((input.pos.y / screenHeight) * 2.0 - 1.0);
    output.pos = float4(x, y, 0.0, 1.0);
    
    output.color = input.color;

    // 2. 关键技巧：我们需要知道当前像素在“矩形内部”的什么位置
    // 我们利用 input.pos (像素坐标) 很难算，
    // 但是我们可以利用 GPU 的插值特性。
    // 这里是一个简化的 Hack：
    // 我们假设 C++ 传进来的 6 个点构成的矩形，
    // 我们希望知道每个像素距离这个矩形中心的距离。
    // 但最简单的画圆方法是：把每个顶点附带一个 UV 坐标 (0,0) 到 (1,1)。
    // 可是我们 C++ 里没有传 UV。
    
    // 既然现在还没改 C++ 结构，我们暂时无法精确知道圆心在哪。
    // 为了演示，我们先做一步“临时的” C++ 修改，或者
    // 直接在 Pixel Shader 里通过屏幕坐标硬算（但这只能画一个固定的圆）。
    
    // 为了让你立刻看到效果，请回到 VS 里，
    // 我们稍微改一下 Vertex Shader 的逻辑，让它把 "本身坐标" 传给 PS
    output.uv = input.pos;

    return output;
}

// --- Pixel Shader ---
float4 PS(PS_INPUT input) : SV_Target
{
    // 这里的 input.uv 现在是屏幕上的像素坐标 (比如 x=150, y=150)
    // 这里的 input.color 是绿色
    
    // 让我们在 Shader 里定义圆心和半径
    // (注意：正常做法是 C++ 传进来，但为了测试 Shader 威力，我们写死)
    float2 center = float2(200.0, 150.0); // 对应你 C++ DrawRect 的中心 (100+200/2, 100+100/2)
    float radius = 50.0; // 半径
    
    // 计算当前像素到圆心的距离
    float dist = distance(input.uv, center);
    
    // --- 核心：SDF 距离场 ---
    // 如果 距离 < 半径，就是 1.0 (显示颜色)
    // 如果 距离 > 半径，就是 0.0 (透明)
    
    // 1. 硬边缘 (有锯齿)
    // if (dist > radius) discard; 
    // return input.color;

    // 2. 抗锯齿 (完美光滑)
    // smoothstep(下限, 上限, 值)
    // 我们让边缘在 1.0 个像素范围内平滑过渡
    float alpha = 1.0 - smoothstep(radius - 1.0, radius, dist);

    // 如果完全透明，直接丢弃性能更好
    if (alpha <= 0.0)
        discard;

    return float4(input.color.rgb, input.color.a * alpha);
}