// inkVertexShader.hlsl
#include "ink.hlsli"

static const float2 kQuadUVs[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

PS_INPUT main(uint id : SV_VertexID)
{
    PS_INPUT output;
    
    // 1. 计算相对索引
    uint segmentIndex = id / 6;
    uint vertexIndex = id % 6;
    
    // 2. 【关键】计算绝对索引：加上全局偏移量
    uint realIndex = globalBufferOffset + segmentIndex;
    
    // 3. Vertex Pulling
    InkStrokeSegment data = InkData[realIndex];

    float2 p1 = data.startData.xy;
    float2 p2 = data.endData.xy;
    float r1 = data.startData.z;
    float r2 = data.endData.z;
    
    float2 templatePos = kQuadUVs[vertexIndex];

    // --- OBB 计算 ---
    float2 dir = p2 - p1;
    float len = length(dir);
    
    float2 tangent = (len > 0.001) ? (dir / len) : float2(1.0, 0.0);
    float2 normal = float2(-tangent.y, tangent.x);
    
    float paddingVal = 2.0;
    float maxR = max(r1, r2);
    
    float localX = lerp(-r1 - paddingVal, len + r2 + paddingVal, templatePos.x);
    float localY = lerp(-maxR - paddingVal, maxR + paddingVal, templatePos.y);
    
    float2 worldPos = p1 + tangent * localX + normal * localY;
    
    // --- 坐标系转换 ---
    float x = (worldPos.x / screenWidth) * 2.0 - 1.0;
    float y = -((worldPos.y / screenHeight) * 2.0 - 1.0);
    
    output.pos = float4(x, y, 0.0, 1.0);
    output.pixPos = worldPos;
    output.color = globalColor;
    output.shapeType = globalShapeType;
    output.p1 = p1;
    output.p2 = p2;
    output.r1 = r1;
    output.r2 = r2;
    output.startCutNormal = data.cutNormals.xy;
    output.endCutNormal = data.cutNormals.zw;
    output.flags = data.flags;
    
    return output;
}
