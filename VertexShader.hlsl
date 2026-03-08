struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct CbufferType
{
    float4x4 g_mWorldViewProj;
};

ConstantBuffer<CbufferType> materialConstants : register(b0, space0);

PSInput main(VSInput input)
{
    PSInput result;
    
    result.position = mul(float4(input.position, 1.0f), materialConstants.g_mWorldViewProj);
    result.uv = input.uv;
    
    return result;
}
