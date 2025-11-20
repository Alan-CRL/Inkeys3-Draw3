// inkVertexShader.hlsl

#include "ink.hlsli"

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    
    // 1. 计算 Clip Space 坐标 (渲染管线需要)
    // 将 [0, W] -> [-1, 1], [0, H] -> [1, -1]
    float x = (input.pos.x / screenWidth) * 2.0 - 1.0;
    float y = -((input.pos.y / screenHeight) * 2.0 - 1.0);
    output.pos = float4(x, y, 0.0, 1.0);
    
    // 2. 传递原始像素坐标 (SDF计算需要)
    output.pixPos = input.pos;
    
    // 3. 透传参数
    output.color = input.color;
    output.p1 = input.p1;
    output.p2 = input.p2;
    output.r1 = input.r1;
    output.r2 = input.r2;
    output.shapeType = input.shapeType;
    
    return output;
}