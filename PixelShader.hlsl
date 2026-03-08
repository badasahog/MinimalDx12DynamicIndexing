struct PSInput
{
    float4 Position : SV_POSITION;
    float2 Uv : TEXCOORD0;
};

struct MaterialConstants
{
    uint MaterialIndex;
};

ConstantBuffer<MaterialConstants> materialConstants : register(b0, space0);
Texture2D g_txDiffuse : register(t0);
Texture2D g_txMats[] : register(t1);
SamplerState g_sampler : register(s0);

float4 main(PSInput input) : SV_TARGET
{
    float3 Diffuse = g_txDiffuse.Sample(g_sampler, input.Uv).rgb;
    float3 Material = g_txMats[materialConstants.MaterialIndex].Sample(g_sampler, input.Uv).rgb;
    return float4(Diffuse * Material, 1.0f);
}
