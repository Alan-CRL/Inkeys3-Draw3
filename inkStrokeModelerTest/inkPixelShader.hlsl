// inkPixelShader.hlsl

#include "ink.hlsli"

// --- SDF 辅助函数 ---

// 计算点 p 到线段 ab 的距离平方
float dot2(in float2 v)
{
    return dot(v, v);
}
float cro(in float2 a, in float2 b)
{
    return a.x * b.y - a.y * b.x;
}

// SDF (连接两个不同半径圆的凸包)
// 返回值：负数表示在内部，正数表示在外部
float sdUnevenCapsule(float2 p, float2 pa, float2 pb, float r1, float r2)
{
    float2 p_rel = p - pa;
    float2 pb_rel = pb - pa;
    float h = dot(pb_rel, pb_rel);
    float q = dot(p_rel, pb_rel);
    
    float2 b = float2(r1 - r2, 0.0);
    float2 c = float2(sqrt(h - b.x * b.x), b.x);
    
    float k = cro(p_rel, pb_rel);
    float m = dot(c, p_rel);
    float n = dot(c, pb_rel);
    
    if (n < 0.0)
        return length(p_rel) - r1; // 起点圆帽
    if (n > c.x * sqrt(h))
        return length(p - pb) - r2; // 终点圆帽 (我们会切掉这部分)
    
    return max(abs(k) - c.x * r1, -m) / c.x; // 梯形部分
    
    // 简单且鲁棒的实现方式：
    /*
    p  -= pa;
    pb -= pa;
    float h = dot(pb,pb);
    float q = dot(p,pb);
    float2 b = float2(r1-r2,0.0);
    float2 c = float2(sqrt(h-b.x*b.x),b.x);
    float k = cro(p,pb);
    float t = dot(p,c) - b.x*q/h;
    return (dot(p,c)>b.x*q/h) ? 
           length(p-pb)-r2 : 
           ((q<0.0)? length(p)-r1 : -t/c.x); // 这里的逻辑有简化，下面在主函数里用组合逻辑更稳
    */
}

// 更加稳定的 SDF 组合逻辑
float GetDist(float2 p, float2 p1, float2 p2, float r1, float r2)
{
    // 1. 计算不对称胶囊体 (P1凸, P2凸, 侧面切线)
    // 这里我们手动构建：
    float2 ba = p2 - p1;
    float l2 = dot(ba, ba);
    float l = sqrt(l2);
    
    // 预防重叠导致除零
    if (l < 0.001)
        return length(p - p1) - r1;

    // 归一化方向
    float2 d = ba / l;
    float2 n = float2(-d.y, d.x); // 法线
    
    // 计算切线偏移角 alpha 的正弦/余弦
    // sin(alpha) = (r1 - r2) / l
    float sinAlpha = (r1 - r2) / l;
    // 钳制一下防止 NaN
    sinAlpha = clamp(sinAlpha, -0.999, 0.999);
    float cosAlpha = sqrt(1.0 - sinAlpha * sinAlpha);
    
    // 投影点到 P1 的局部坐标
    float2 v = p - p1;
    float projDist = dot(v, d); // 沿连线方向距离
    float perpDist = dot(v, n); // 垂直连线方向距离
    
    // --- 胶囊体 SDF (Convex Hull) ---
    // 这里的简化逻辑：计算点到两条切线的距离
    // 切线法向量 T1 = (sin, cos), T2 = (sin, -cos) 在局部坐标系(d, n)下
    // 但为了简单，我们直接使用 iq 的 sdUnevenCapsule 逻辑
    
    float2 p_curr = p - p1;
    float2 pb = p2 - p1;
    float h = dot(pb, pb);
    float q = dot(p_curr, pb);
    float2 b_vec = float2(r1 - r2, 0.0);
    float2 c_vec = float2(sqrt(h - b_vec.x * b_vec.x), b_vec.x);
    float k = cro(p_curr, pb);
    float t = dot(p_curr, c_vec) - b_vec.x * q / h;
    
    // 计算 P1 处的凸圆 SDF
    float d_p1 = length(p - p1) - r1;
    
    // 计算切线部分的 SDF (无限长锥体)
    float d_cone = -t / c_vec.x; // 负号是因为方向定义问题，需要调试确定符号
    // 修正 d_cone 计算: dot(p, N) - r. 
    // 我们只关心是在两条切线中间。
    // 正确的 sdUnevenCapsule 主体部分：
    float d_hull = (dot(p_curr, c_vec) > b_vec.x * q / h) ?
                   length(p - p2) - r2 : // P2 端 (凸)
                   ((q < 0.0) ? length(p - p1) - r1 : // P1 端 (凸)
                   abs(k) / l - (r1 * (l - q) + r2 * q) / l); // 线性插值半径近似 (不完全准确但够用)
                   
    // 使用最准确的 sdUnevenCapsule
    float dist = 0.0;
    {
        float2 pa = p1;
        float2 pb = p2;
        float ra = r1;
        float rb = r2;
        p_curr = p - pa;
        float2 ba_v = pb - pa;
        float h_v = dot(ba_v, ba_v);
        float q_v = dot(p_curr, ba_v);
        float2 b_v = float2(ra - rb, 0.0);
        float2 c_v = float2(sqrt(h_v - b_v.x * b_v.x), b_v.x);
        float k_v = cro(p_curr, ba_v);
        float t_v = dot(p_curr, c_v) - b_v.x * q_v / h_v;
        
        if (dot(p_curr, c_v) > b_v.x * q_v / h_v)
            dist = length(p - pb) - rb;
        else if (q_v < 0.0)
            dist = length(p - pa) - ra;
        else
            dist = -t_v / c_v.x;
    }
    
    // --- 关键：实现终点凹入 (Concave End) ---
    // 逻辑：图形 = (胶囊体) 减去 (P2 圆的内部)
    // Boolean Subtraction: max(d_shape, -d_hole)
    // P2 圆 SDF: d_hole = length(p - p2) - r2
    // -d_hole = r2 - length(p - p2)
    
    // 我们希望在 P2 处，只有当点在 r2 半径 *外部* 时才保留。
    // 也就是说，如果点太靠近 P2 (距离 < r2)，则 SDF 应该变大(正数，外部)。
    
    float d_bite = r2 - length(p - p2);
    
    // 最终 SDF = max(凸包距离, 咬痕距离)
    return max(dist, d_bite);
}

float4 main(PS_INPUT input) : SV_Target
{
    // 根据 shapeType 选择绘制逻辑
    float d = 0.0;
    
    if (input.shapeType == 1) // 1: Ink Stroke (凸起 -> 凹陷)
    {
        d = GetDist(input.pixPos, input.p1, input.p2, input.r1, input.r2);
    }
    else if (input.shapeType == 0) // 0: Line (测试用，普通线段)
    {
        // 简单胶囊体
        float2 pa = input.pixPos - input.p1;
        float2 ba = input.p2 - input.p1;
        float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
        d = length(pa - ba * h) - input.r1; // 假设两端半径相同
    }
    else
    {
        // 预留其他图形
        return float4(0, 0, 0, 0);
    }

    // --- 抗锯齿渲染 ---
    // d < 0 在内部, d > 0 在外部
    // 使用 fwidth 自动计算当前像素的导数，实现完美的 1px 抗锯齿
    float aa = fwidth(d);
    float alpha = 1.0 - smoothstep(-aa, aa, d);

    // 性能优化：如果完全透明，discard (可选)
    if (alpha <= 0.0)
        discard;

    return float4(input.color.rgb, input.color.a * alpha);
}