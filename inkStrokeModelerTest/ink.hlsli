// ink.hlsli

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
};