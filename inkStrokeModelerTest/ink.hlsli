// ink.hlsli

cbuffer ScreenBuffer : register(b0)
{
    float screenWidth;
    float screenHeight;
    float2 padding;
};

// 顶点输入：包含了绘制形状所需的全部几何参数
struct VS_INPUT
{
    float2 pos : POSITION;
    float4 color : COLOR;
    
    float2 p1 : VAL_START; // 起点坐标
    float2 p2 : VAL_END; // 终点坐标
    float r1 : VAL_RAD_START; // 起点半径
    float r2 : VAL_RAD_END; // 终点半径
    int shapeType : VAL_TYPE;
};

// 像素输入：将几何参数透传给像素着色器进行数学计算
struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 pixPos : TEXCOORD0;
    float4 color : COLOR;
    
    nointerpolation float2 p1 : VAL_START;
    nointerpolation float2 p2 : VAL_END;
    nointerpolation float r1 : VAL_RAD_START;
    nointerpolation float r2 : VAL_RAD_END;
    nointerpolation int shapeType : VAL_TYPE;
};