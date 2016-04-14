struct VSOutput {
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
};

cbuffer ConstantBuffer:register(b0)
{
	float4x4 mtx;
	float4x4 junk1;
	float4 junk2;
	int4 junk3;
	int4 junk4[7];
};

VSOutput vsmain(
	float4 pos : POSITION,
	float4 color : COLOR
	)
{
	VSOutput vsout = (VSOutput)0;
	vsout.Position = mul( mtx, pos );
	vsout.Color = color;
	return vsout;
}

float4 psmain( VSOutput vsout ) : SV_TARGET
{
	return vsout.Color;
}