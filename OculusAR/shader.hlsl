cbuffer Constants {
	float4x4 mvp;
};

Texture2D ObjTexture;
SamplerState ObjSamplerState;

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION; 
	float4 TexCoord : TEXCOORD;
};
VS_OUTPUT VS(float4 Pos : POSITION, float4 TexCoord : TEXCOORD) {
	VS_OUTPUT output = (VS_OUTPUT)0;
	output.Pos = mul(mvp, Pos);
	output.TexCoord = TexCoord;
	return output;
};

float4 PS(VS_OUTPUT input) : SV_Target
{
	return ObjTexture.Sample(ObjSamplerState, input.TexCoord); 
};