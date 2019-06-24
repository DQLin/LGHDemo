//#define GENERATE_IR_GROUND_TRUTH

#ifdef GENERATE_IR_GROUND_TRUTH
Texture2D<float4> Analytic : register(t64);
#else
Texture2D<float3> Analytic : register(t64);
#endif

Texture2D<float3> SURatio : register(t65);
Texture2D<float> texShadow : register(t66);
SamplerState sampler0 : register(s0);
SamplerComparisonState shadowSampler : register(s1);

Texture2D<float4> texPosition		: register(t32);
Texture2D<float4> texNormal		    : register(t33);
Texture2D<float4> texAlbedo			: register(t34);
Texture2D<float4> texSpecular		: register(t35);

cbuffer PSConstants : register(b0)
{
	float4x4 WorldToShadow;
	float3 ViewerPos;
	float3 SunDirection;
	float3 SunColor;
	float4 ShadowTexelSize;
	int scrWidth;
	int scrHeight;
	int shadowRate;
	int debugMode;
	int directOnly;
}

// Apply fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, float3 lightDir, float3 halfVec)
{
	float fresnel = pow(1.0 - saturate(dot(lightDir, halfVec)), 5.0);
	specular = lerp(specular, 1, fresnel);
}

float GetShadow(float3 ShadowCoord)
{
#ifdef SINGLE_SAMPLE
	float result = texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy, ShadowCoord.z);
#else
	const float Dilation = 2.0;
	float d1 = Dilation * ShadowTexelSize.x * 0.125;
	float d2 = Dilation * ShadowTexelSize.x * 0.875;
	float d3 = Dilation * ShadowTexelSize.x * 0.625;
	float d4 = Dilation * ShadowTexelSize.x * 0.375;
	float result = (
		2.0 * texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy, ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d2, d1), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d1, -d2), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d2, -d1), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d1, d2), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d4, d3), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d3, -d4), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d4, -d3), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d3, d4), ShadowCoord.z)
		) / 10.0;
#endif
	return result * result;
}

float3 ApplyLightCommon(
	float3	diffuseColor,	// Diffuse albedo
	float3	specularColor,	// Specular albedo
	float	specularMask,	// Where is it shiny or dingy?
	float	gloss,			// Specular power
	float3	normal,			// World-space normal
	float3	viewDir,		// World-space vector from eye to point
	float3	lightDir,		// World-space vector from point to light
	float3	lightColor		// Radiance of directional light
)
{
	float3 halfVec = normalize(lightDir - viewDir);
	float nDotH = saturate(dot(halfVec, normal));

	FSchlick(specularColor, lightDir, halfVec);

	float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;

	float nDotL = saturate(dot(normal, lightDir));

	return nDotL * lightColor * diffuseColor + lightColor * specularFactor * specularColor;
}

float3 ApplyDirectionalLight(
	float3	diffuseColor,	// Diffuse albedo
	float3	specularColor,	// Specular albedo
	float	specularMask,	// Where is it shiny or dingy?
	float	gloss,			// Specular power
	float3	normal,			// World-space normal
	float3	viewDir,		// World-space vector from eye to point
	float3	lightDir,		// World-space vector from point to light
	float3	lightColor,		// Radiance of directional light
	float3	shadowCoord		// Shadow coordinate (Shadow map UV & light-relative Z)
)
{
	float shadow = GetShadow(shadowCoord);

	return shadow * ApplyLightCommon(
		diffuseColor,
		specularColor,
		specularMask,
		gloss,
		normal,
		viewDir,
		lightDir,
		lightColor
	);
}

float3 main(float4 screenPos : SV_POSITION) : SV_TARGET0
{
	int2 pos = int2(screenPos.xy);
	if (debugMode) return SURatio[pos];

	float2 uv = screenPos.xy / float2(scrWidth, scrHeight);
	
	float3 worldPosition = texPosition[pos].xyz;
	float3 viewDir = worldPosition - ViewerPos;
	float3 shadowCoord = mul(WorldToShadow, float4(worldPosition, 1.0)).xyz;
	float3 diffuseAlbedo = texAlbedo[pos].rgb;
	float3 specularAlbedo = 1;
	float3 specularMask = texSpecular[pos].g;
	float gloss = 128.0;
	float3 normal = texNormal[pos].xyz;
	float3 colorSum = 0;
	colorSum += ApplyDirectionalLight(diffuseAlbedo, specularAlbedo, specularMask, gloss, normal, viewDir, SunDirection, SunColor, shadowCoord);

#ifdef GENERATE_IR_GROUND_TRUTH
	if (!directOnly) colorSum += Analytic[pos].rgb * diffuseAlbedo;
#else
	float3 ss = shadowRate > 0 ? SURatio[pos] : 1.f;
	if (!directOnly) colorSum += Analytic[pos] * diffuseAlbedo * ss; 
#endif
	return colorSum;
}