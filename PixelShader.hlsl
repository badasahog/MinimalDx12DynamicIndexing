struct PSInput
{
    float4 Position : SV_POSITION;
    float2 Uv : TEXCOORD0;
};

struct MaterialConstants
{
    uint MaterialIndex;
};

ConstantBuffer<MaterialConstants> MaterialConstants : register(b0, space0);
Texture2D DiffuseTexture : register(t0);
Texture2D MaterialTextures[] : register(t1);
SamplerState Sampler : register(s0);

float4 main(PSInput Input) : SV_TARGET
{
    float3 Diffuse = DiffuseTexture.Sample(Sampler, Input.Uv).rgb;
    float3 Material = MaterialTextures[MaterialConstants.MaterialIndex].Sample(Sampler, Input.Uv).rgb;
    return float4(Diffuse * Material, 1.0f);
}
