// ink.hlsli

// 1. 常量缓冲区 (b0)
cbuffer ScreenBuffer : register(b0)
{
    float screenWidth;
    float screenHeight;
    float globalShapeType;
    
    // 接收传来的环形缓冲偏移量
    uint globalBufferOffset;
    
    float4 globalColor;
};

// 2. 结构定义
struct InkStrokeSegment
{
    float4 startData;
    float4 endData;
    float4 cutNormals;
    uint flags;
    float3 padding;
};

// 3. 结构化缓冲区
StructuredBuffer<InkStrokeSegment> InkData : register(t0);

// 4. VS -> PS
struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 pixPos : TEXCOORD0;
    
    nointerpolation float4 color : COLOR;
    
    nointerpolation float2 p1 : VAL_START;
    nointerpolation float2 p2 : VAL_END;
    nointerpolation float r1 : VAL_RAD_START;
    nointerpolation float r2 : VAL_RAD_END;
    nointerpolation float2 startCutNormal : VAL_START_CUT;
    nointerpolation float2 endCutNormal : VAL_END_CUT;
    nointerpolation uint flags : VAL_FLAGS;
    nointerpolation float shapeType : VAL_TYPE;
};
