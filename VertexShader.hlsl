struct VSInput
{
    float3 Position : POSITION;
    float2 Uv : TEXCOORD0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 Uv : TEXCOORD0;
};

struct CbufferType
{
    float4x4 MVP;
};

ConstantBuffer<CbufferType> MaterialConstants : register(b0, space0);

PSInput main(VSInput Input)
{
    PSInput Result;
    
    Result.Position = mul(float4(Input.Position, 1.0f), MaterialConstants.MVP);
    Result.Uv = Input.Uv;
    
    return Result;
}
