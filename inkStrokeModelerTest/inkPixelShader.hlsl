// inkPixelShader.hlsl
#include "ink.hlsli"

float sdUnevenCapsule_Vertical(float2 p, float r1, float r2, float h)
{
    p.x = abs(p.x);
    float b = (r1 - r2) / h;
    float b2 = b * b;
    if (b2 > 1.0)
        return length(p) - max(r1, r2);
    float a = sqrt(max(0.0, 1.0 - b2));
    float k = dot(p, float2(-b, a));
    if (k < 0.0)
        return length(p) - r1;
    if (k > a * h)
        return length(p - float2(0.0, h)) - r2;
    return dot(p, float2(a, b)) - r1;
}

float GetInkDist_Convex(float2 p, float2 p1, float2 p2, float r1, float r2)
{
    float2 pa = p - p1;
    float2 ba = p2 - p1;
    float h = length(ba);
    if (h < 0.1)
        return length(pa) - r1;
    float2 yAxis = ba / h;
    float2 xAxis = float2(-yAxis.y, yAxis.x);
    float2 p_local = float2(dot(pa, xAxis), dot(pa, yAxis));
    return sdUnevenCapsule_Vertical(p_local, r1, r2, h);
}

float4 main(PS_INPUT input) : SV_Target
{
    int type = (int)(input.shapeType + 0.5);
    bool hasStartCut = (input.flags & 1u) != 0u;
    bool hasEndCut = (input.flags & 2u) != 0u;

    if (any(isnan(input.p1)) || any(isnan(input.p2)))
        discard;

    float d = 0.0;

    if (type == 0)
    {
        d = GetInkDist_Convex(input.pixPos, input.p1, input.p2, input.r1, input.r2);
    }

    if (hasStartCut && dot(input.pixPos - input.p1, input.startCutNormal) < 0.0)
        discard;

    if (hasEndCut && dot(input.pixPos - input.p2, input.endCutNormal) > 0.0)
        discard;

    float aaWidth = clamp(fwidth(d), 0.75, 1.25);
    float coverage = 1.0 - smoothstep(-aaWidth * 0.5, aaWidth * 0.5, d);
    float finalAlpha = input.color.a * coverage;

    if (finalAlpha <= 0.0)
        discard;

    return float4(input.color.rgb * finalAlpha, finalAlpha);
}
