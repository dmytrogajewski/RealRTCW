/*
 * Screen-Space Global Illumination (SSGI)
 *
 * Approximates indirect lighting by sampling nearby pixels in screen space
 * and using their color as bounce light based on depth and normal estimation.
 */

uniform sampler2D u_ScreenDepthMap;  // Linearized depth
uniform sampler2D u_ScreenImageMap;  // Scene color

uniform vec4 u_ViewInfo; // zfar / znear, zfar, 1/width, 1/height

varying vec2 var_ScreenTex;

#define PI 3.14159265358979323846

// Random number generator
float random(const vec2 p)
{
	const vec2 r = vec2(
		23.1406926327792690,  // e^pi (Gelfond's constant)
		2.6651441426902251);  // 2^sqrt(2) (Gelfond-Schneider constant)
	return fract(sin(dot(p, r)) * 43758.5453);
}

// Random rotation matrix for sample jittering
mat2 randomRotation(const vec2 p)
{
	float r = random(p) * 2.0 * PI;
	float sinr = sin(r);
	float cosr = cos(r);
	return mat2(cosr, sinr, -sinr, cosr);
}

// Get linear depth from depth texture
float getLinearDepth(sampler2D depthMap, const vec2 tex, const float zFarDivZNear)
{
	float sampleZDivW = texture2D(depthMap, tex).r;
	return 1.0 / mix(zFarDivZNear, 1.0, sampleZDivW);
}

// Reconstruct view-space normal from depth buffer using derivatives
vec3 reconstructNormal(const vec2 tex, const float depth, const float zFarDivZNear, const vec2 texelSize)
{
	float depthLeft  = getLinearDepth(u_ScreenDepthMap, tex - vec2(texelSize.x, 0.0), zFarDivZNear);
	float depthRight = getLinearDepth(u_ScreenDepthMap, tex + vec2(texelSize.x, 0.0), zFarDivZNear);
	float depthUp    = getLinearDepth(u_ScreenDepthMap, tex + vec2(0.0, texelSize.y), zFarDivZNear);
	float depthDown  = getLinearDepth(u_ScreenDepthMap, tex - vec2(0.0, texelSize.y), zFarDivZNear);

	// Use central differences for better accuracy
	vec3 normal;
	normal.x = (depthLeft - depthRight) * 0.5;
	normal.y = (depthDown - depthUp) * 0.5;
	normal.z = texelSize.x * 2.0; // Scale based on texel size

	return normalize(normal);
}

// Sample indirect illumination
vec3 sampleIndirectLight(
	sampler2D depthMap,
	sampler2D colorMap,
	const vec2 tex,
	const float zFarDivZNear,
	const float zFar,
	const vec2 texelSize
)
{
	// Poisson disc samples for better distribution
	vec2 poissonDisc[16];
	poissonDisc[0]  = vec2(-0.94201624, -0.39906216);
	poissonDisc[1]  = vec2(0.94558609, -0.76890725);
	poissonDisc[2]  = vec2(-0.094184101, -0.92938870);
	poissonDisc[3]  = vec2(0.34495938, 0.29387760);
	poissonDisc[4]  = vec2(-0.91588581, 0.45771432);
	poissonDisc[5]  = vec2(-0.81544232, -0.87912464);
	poissonDisc[6]  = vec2(-0.38277543, 0.27676845);
	poissonDisc[7]  = vec2(0.97484398, 0.75648379);
	poissonDisc[8]  = vec2(0.44323325, -0.97511554);
	poissonDisc[9]  = vec2(0.53742981, -0.47373420);
	poissonDisc[10] = vec2(-0.26496911, -0.41893023);
	poissonDisc[11] = vec2(0.79197514, 0.19090188);
	poissonDisc[12] = vec2(-0.24188840, 0.99706507);
	poissonDisc[13] = vec2(-0.81409955, 0.91437590);
	poissonDisc[14] = vec2(0.19984126, 0.78641367);
	poissonDisc[15] = vec2(0.14383161, -0.14100790);

	vec3 indirectLight = vec3(0.0);
	float totalWeight = 0.0;

	float centerDepth = getLinearDepth(depthMap, tex, zFarDivZNear);
	vec3 centerNormal = reconstructNormal(tex, centerDepth, zFarDivZNear, texelSize);

	// Scale sample radius based on depth (closer = smaller radius in screen space)
	float scaleZ = zFarDivZNear * centerDepth;
	vec2 sampleRadius = texelSize * 128.0 / scaleZ;

	// Clamp radius to reasonable bounds
	sampleRadius = clamp(sampleRadius, texelSize * 4.0, texelSize * 64.0);

	// Random rotation for this pixel
	mat2 rmat = randomRotation(tex * 1000.0);

	float invZFar = 1.0 / zFar;

	// Number of samples (can be adjusted via uniform)
	const int NUM_SAMPLES = 8;

	for (int i = 0; i < NUM_SAMPLES; i++)
	{
		// Rotate and scale sample offset
		vec2 offset = rmat * poissonDisc[i] * sampleRadius;
		vec2 sampleCoord = tex + offset;

		// Skip samples outside screen bounds
		if (sampleCoord.x < 0.0 || sampleCoord.x > 1.0 ||
		    sampleCoord.y < 0.0 || sampleCoord.y > 1.0)
			continue;

		float sampleDepth = getLinearDepth(depthMap, sampleCoord, zFarDivZNear);
		float depthDiff = centerDepth - sampleDepth;

		// Check if sample is in front (potential occluder/light source)
		// and within reasonable depth range
		float depthThreshold = 50.0 * invZFar;

		if (abs(depthDiff) < depthThreshold)
		{
			// Sample scene color at this location
			vec3 sampleColor = texture2D(colorMap, sampleCoord).rgb;

			// Weight by distance (closer samples contribute more)
			float distWeight = 1.0 - length(poissonDisc[i]) * 0.5;

			// Weight by depth similarity (similar depth = more likely same surface)
			float depthWeight = 1.0 - abs(depthDiff) / depthThreshold;
			depthWeight = depthWeight * depthWeight; // Square for smoother falloff

			// Estimate visibility: sample is behind us relative to surface normal
			vec3 sampleDir = vec3(offset / sampleRadius, depthDiff * zFar);
			sampleDir = normalize(sampleDir);

			// Cosine weighting - light coming from directions facing the surface
			float normalWeight = max(0.0, -dot(centerNormal, sampleDir));

			// Combined weight
			float weight = distWeight * depthWeight * (0.3 + 0.7 * normalWeight);

			indirectLight += sampleColor * weight;
			totalWeight += weight;
		}
	}

	if (totalWeight > 0.0)
	{
		indirectLight /= totalWeight;
	}

	return indirectLight;
}

void main()
{
	vec2 texelSize = u_ViewInfo.wz;
	float zFarDivZNear = u_ViewInfo.x;
	float zFar = u_ViewInfo.y;

	// Sample indirect lighting
	vec3 indirectLight = sampleIndirectLight(
		u_ScreenDepthMap,
		u_ScreenImageMap,
		var_ScreenTex,
		zFarDivZNear,
		zFar,
		texelSize
	);

	// Output indirect light contribution
	// Will be additively blended with the scene
	gl_FragColor = vec4(indirectLight * 0.35, 1.0);
}
