#include "ink.hlsli"

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    float x = (input.pos.x / screenWidth) * 2.0 - 1.0;
    float y = -((input.pos.y / screenHeight) * 2.0 - 1.0);
    
    output.pos = float4(x, y, 0.0, 1.0);
    output.color = input.color;
    
    return output;
}