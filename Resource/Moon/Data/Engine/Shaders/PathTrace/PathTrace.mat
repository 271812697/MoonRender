#shader vertex
#version 450 core

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoords;

out vec2 TexCoords;

void main()
{
	gl_Position = vec4(position.x, position.y, 0.0, 1.0);
	TexCoords = texCoords;
}
   



#shader fragment
#version 450 core

out vec4 color;
in vec2 TexCoords;
uniform bool isCameraMoving;
uniform vec3 randomVector;
uniform vec2 resolution;
uniform vec2 tileOffset;
uniform vec2 invNumTiles;

uniform sampler2D accumTexture;
uniform samplerBuffer BVH;
uniform isamplerBuffer vertexIndicesTex;
uniform isamplerBuffer meshTriangleInfoTex;
uniform usamplerBuffer triangleInfoTex;
uniform samplerBuffer verticesTex;
uniform samplerBuffer normalsTex;
uniform samplerBuffer triangleDomainColorTex;
uniform sampler2D materialsTex;
uniform sampler2D transformsTex;
uniform sampler2D lightsTex;
uniform sampler2DArray textureMapsArrayTex;

uniform sampler2D envMapTex;
uniform sampler2D envMapCDFTex;

uniform vec2 envMapRes;
uniform float envMapTotalSum;
uniform float envMapIntensity;
uniform float envMapRot;
uniform vec3 uniformLightCol;
uniform int numOfLights;
uniform int maxDepth;
uniform int topBVHIndex;
uniform int frameNum;
uniform float roughnessMollificationAmt;
#if defined(OPT_RR)
uniform int RR_DEPTH;
#endif

#define PI         3.14159265358979323
#define INV_PI     0.31830988618379067
#define TWO_PI     6.28318530717958648
#define INV_TWO_PI 0.15915494309189533
#define INV_4_PI   0.07957747154594766
#define EPS 0.0003
#define INF 1000000.0

#define QUAD_LIGHT 0
#define SPHERE_LIGHT 1
#define DISTANT_LIGHT 2

#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_BLEND 1
#define ALPHA_MODE_MASK 2

#define MEDIUM_NONE 0
#define MEDIUM_ABSORB 1
#define MEDIUM_SCATTER 2
#define MEDIUM_EMISSIVE 3

struct Ray
{
	vec3 origin;
	vec3 direction;
};

struct Medium
{
	int type;
	float density;
	vec3 color;
	float anisotropy;
};

struct Material
{
	vec3 baseColor;
	float opacity;
	int alphaMode;
	float alphaCutoff;
	vec3 emission;
	float anisotropic;
	float metallic;
	float roughness;
	float subsurface;
	float specularTint;
	float sheen;
	float sheenTint;
	float clearcoat;
	float clearcoatRoughness;
	float specTrans;
	float ior;
	float ax;
	float ay;
	Medium medium;
};

struct Camera
{
	vec3 up;
	vec3 right;
	vec3 forward;
	vec3 position;
	float fov;
	float focalDist;
	float aperture;
	float owidth;
	float oheight;
};

struct Light
{
	vec3 position;
	vec3 emission;
	vec3 u;
	vec3 v;
	float radius;
	float area;
	float type;
};

struct State
{
	int depth;
	float eta;
	float hitDist;

	vec3 fhp;
	vec3 normal;
	vec3 ffnormal;
	vec3 tangent;
	vec3 bitangent;

	bool isEmitter;

	vec2 texCoord;
	int matID;
	int domainId;
	vec3 domainTint;
	//uint tid;
	Material mat;
	Medium medium;
};

struct ScatterSampleRec
{
	vec3 L;
	vec3 f;
	float pdf;
};

struct LightSampleRec
{
	vec3 normal;
	vec3 emission;
	vec3 direction;
	float dist;
	float pdf;
};

uniform Camera camera;

//RNG from code by Moroz Mykhailo (https://www.shadertoy.com/view/wltcRS)

//internal RNG state 
uvec4 seed;
ivec2 pixel;

void InitRNG(vec2 p, int frame)
{
	pixel = ivec2(p);
	seed = uvec4(p, uint(frame), uint(p.x) + uint(p.y));
}

void pcg4d(inout uvec4 v)
{
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
	v = v ^ (v >> 16u);
	v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
}

float rand()
{
	pcg4d(seed); return float(seed.x) / float(0xffffffffu);
}

vec3 FaceForward(vec3 a, vec3 b)
{
	return dot(a, b) < 0.0 ? -b : b;
}

float Luminance(vec3 c)
{
	return 0.212671 * c.x + 0.715160 * c.y + 0.072169 * c.z;
}

float SphereIntersect(float rad, vec3 pos, Ray r)
{
	vec3 op = pos - r.origin;
	float eps = 0.001;
	float b = dot(op, r.direction);
	float det = b * b - dot(op, op) + rad * rad;
	if (det < 0.0)
		return INF;

	det = sqrt(det);
	float t1 = b - det;
	if (t1 > eps)
		return t1;

	float t2 = b + det;
	if (t2 > eps)
		return t2;

	return INF;
}
//pos the start of rectangle , plane that includes the the rectangle ,u v the two axis of rectangle and the lenght is inverse
float RectIntersect(in vec3 pos, in vec3 u, in vec3 v, in vec4 plane, in Ray r)
{
	vec3 n = vec3(plane);
	float dt = dot(r.direction, n);
	float t = (plane.w - dot(n, r.origin)) / dt;

	if (t > EPS)
	{
		vec3 p = r.origin + r.direction * t;
		vec3 vi = p - pos;
		float a1 = dot(u, vi);
		if (a1 >= 0.0 && a1 <= 1.0)
		{
			float a2 = dot(v, vi);
			if (a2 >= 0.0 && a2 <= 1.0)
				return t;
		}
	}

	return INF;
}

float AABBIntersect(vec3 minCorner, vec3 maxCorner, Ray r)
{
	vec3 invDir = 1.0 / r.direction;

	vec3 f = (maxCorner - r.origin) * invDir;
	vec3 n = (minCorner - r.origin) * invDir;

	vec3 tmax = max(f, n);
	vec3 tmin = min(f, n);

	float t1 = min(tmax.x, min(tmax.y, tmax.z));
	float t0 = max(tmin.x, max(tmin.y, tmin.z));

	return (t1 >= t0) ? (t0 > 0.f ? t0 : t1) : -1.0;
}
	// Watertight ray/triangle intersection (Woop et al. 2013).
	// Avoids cracks on shared edges and precision loss on sliver triangles.
	// Outputs hit distance t and barycentric coordinates (u, v).
	bool IntersectTriangle(Ray r, vec3 v0, vec3 v1, vec3 v2, out float t, out vec2 bary)
	{
		vec3 dir = r.direction;
		vec3 absDir = abs(dir);
		
		// Pick the dominant ray axis and permute so it maps to z
		int kz = (absDir.x >= absDir.y && absDir.x >= absDir.z) ? 0 : (absDir.y >= absDir.z ? 1 : 2);
		int kx = (kz + 1) % 3;
		int ky = (kx + 1) % 3;
		if (kx > ky)
		{
			int tmp = kx;
			kx = ky;
			ky = tmp;
		}
		
		vec3 d = vec3(dir[kx], dir[ky], dir[kz]);
		vec3 o = vec3(r.origin[kx], r.origin[ky], r.origin[kz]);
		vec3 a = vec3(v0[kx], v0[ky], v0[kz]);
		vec3 b = vec3(v1[kx], v1[ky], v1[kz]);
		vec3 c = vec3(v2[kx], v2[ky], v2[kz]);
		
		// Shear the triangle along the ray so the intersection becomes a 2D edge test
		float sx = -d.x / d.z;
		float sy = -d.y / d.z;
		float sz = 1.0 / d.z;
		
		vec2 a2 = a.xy + vec2(sx, sy) * a.z;
		vec2 b2 = b.xy + vec2(sx, sy) * b.z;
		vec2 c2 = c.xy + vec2(sx, sy) * c.z;
		vec2 o2 = o.xy + vec2(sx, sy) * o.z;
		
		vec2 e0 = b2 - a2;
		vec2 e1 = c2 - a2;
		vec2 t2 = o2 - a2;
		
		float det = e0.x * e1.y - e0.y * e1.x;
		if (det == 0.0)
		{
			return false;
		}
		
		float invDet = 1.0 / det;
		float u = (t2.x * e1.y - t2.y * e1.x) * invDet;
		float v = (e0.x * t2.y - e0.y * t2.x) * invDet;
		float w = 1.0 - u - v;
		
		if (u < 0.0 || v < 0.0 || w < 0.0)
		{
			return false;
		}
		
		t = (a.z - o.z + u * (b.z - a.z) + v * (c.z - a.z)) * sz;
		bary = vec2(u, v);
		return t > 0.0;
	}
//The Normal Distrubtion function
float GTR1(float NDotH, float a)
{
	if (a >= 1.0)
		return INV_PI;
	float a2 = a * a;
	float t = 1.0 + (a2 - 1.0) * NDotH * NDotH;
	return (a2 - 1.0) / (PI * log(a2) * t);
}

float GTR2(float NDotH, float a)
{
	float a2 = a * a;
	float t = 1.0 + (a2 - 1.0) * NDotH * NDotH;
	return a2 / (PI * t * t);
}
//this is used to sample a vec3 in hemisphere with roughness importance 
//the roughness can decide the angle of cosTheta which limits the range of sample
//when roughness is equal to 1,cosTheta  is pi/2 ,and 0 -> 0
//this can be useful to query the direction of specular light 
vec3 SampleGTR1(float rgh, float r1, float r2)
{
	float a = max(0.001, rgh);
	float a2 = a * a;

	float phi = r1 * TWO_PI;

	float cosTheta = sqrt((1.0 - pow(a2, 1.0 - r2)) / (1.0 - a2));
	float sinTheta = clamp(sqrt(1.0 - (cosTheta * cosTheta)), 0.0, 1.0);
	float sinPhi = sin(phi);
	float cosPhi = cos(phi);

	return vec3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}

vec3 SampleGTR2(float rgh, float r1, float r2)
{
	float a = max(0.001, rgh);

	float phi = r1 * TWO_PI;

	float cosTheta = sqrt((1.0 - r2) / (1.0 + (a * a - 1.0) * r2));
	float sinTheta = clamp(sqrt(1.0 - (cosTheta * cosTheta)), 0.0, 1.0);
	float sinPhi = sin(phi);
	float cosPhi = cos(phi);

	return vec3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}
//Sample a H
vec3 SampleGGXVNDF(vec3 V, float ax, float ay, float r1, float r2)
{
	vec3 Vh = normalize(vec3(ax * V.x, ay * V.y, V.z));

	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1, 0, 0);
	vec3 T2 = cross(Vh, T1);

	float r = sqrt(r1);
	float phi = 2.0 * PI * r2;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);
	float s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

	vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

	return normalize(vec3(ax * Nh.x, ay * Nh.y, max(0.0, Nh.z)));
}
//Consideration of Anisotropic Normal Distribution Equation
float GTR2Aniso(float NDotH, float HDotX, float HDotY, float ax, float ay)
{
	float a = HDotX / ax;
	float b = HDotY / ay;
	float c = a * a + b * b + NDotH * NDotH;
	return 1.0 / (PI * ax * ay * c * c);
}

vec3 SampleGTR2Aniso(float ax, float ay, float r1, float r2)
{
	float phi = r1 * TWO_PI;

	float sinPhi = ay * sin(phi);
	float cosPhi = ax * cos(phi);
	float tanTheta = sqrt(r2 / (1 - r2));

	return vec3(tanTheta * cosPhi, tanTheta * sinPhi, 1.0);
}

float SmithG(float NDotV, float alphaG)
{
	float a = alphaG * alphaG;
	float b = NDotV * NDotV;
	return (2.0 * NDotV) / (NDotV + sqrt(a + b - a * b));
}

float SmithGAniso(float NDotV, float VDotX, float VDotY, float ax, float ay)
{
	float a = VDotX * ax;
	float b = VDotY * ay;
	float c = NDotV;
	return (2.0 * NDotV) / (NDotV + sqrt(a * a + b * b + c * c));
}

float SchlickWeight(float u)
{
	float m = clamp(1.0 - u, 0.0, 1.0);
	float m2 = m * m;
	return m2 * m2 * m;
}

float DielectricFresnel(float cosThetaI, float eta)
{
	float sinThetaTSq = eta * eta * (1.0f - cosThetaI * cosThetaI);

	// Total internal reflection
	if (sinThetaTSq > 1.0)
		return 1.0;

	float cosThetaT = sqrt(max(1.0 - sinThetaTSq, 0.0));

	float rs = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
	float rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

	return 0.5f * (rs * rs + rp * rp);
}

vec3 CosineSampleHemisphere(float r1, float r2)
{
	vec3 dir;
	float r = sqrt(r1);
	float phi = TWO_PI * r2;
	dir.x = r * cos(phi);
	dir.y = r * sin(phi);
	dir.z = sqrt(max(0.0, 1.0 - dir.x * dir.x - dir.y * dir.y));
	return dir;
}

vec3 UniformSampleHemisphere(float r1, float r2)
{
	float r = sqrt(max(0.0, 1.0 - r1 * r1));
	float phi = TWO_PI * r2;
	return vec3(r * cos(phi), r * sin(phi), r1);
}

vec3 UniformSampleSphere(float r1, float r2)
{
	float z = 1.0 - 2.0 * r1;
	float r = sqrt(max(0.0, 1.0 - z * z));
	float phi = TWO_PI * r2;
	return vec3(r * cos(phi), r * sin(phi), z);
}

float PowerHeuristic(float a, float b)
{
	float t = a * a;
	return t / (b * b + t);
}

void Onb(in vec3 N, inout vec3 T, inout vec3 B)
{
	vec3 up = abs(N.z) < 0.9999999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
	T = normalize(cross(up, N));
	B = cross(N, T);
}

void SampleSphereLight(in Light light, in vec3 scatterPos, inout LightSampleRec lightSample)
{
	float r1 = rand();
	float r2 = rand();

	vec3 sphereCentertoSurface = scatterPos - light.position;
	float distToSphereCenter = length(sphereCentertoSurface);
	vec3 sampledDir;

	// TODO: Fix this. Currently assumes the light will be hit only from the outside
	sphereCentertoSurface /= distToSphereCenter;
	sampledDir = UniformSampleHemisphere(r1, r2);
	vec3 T, B;
	Onb(sphereCentertoSurface, T, B);
	sampledDir = T * sampledDir.x + B * sampledDir.y + sphereCentertoSurface * sampledDir.z;

	vec3 lightSurfacePos = light.position + sampledDir * light.radius;

	lightSample.direction = lightSurfacePos - scatterPos;
	lightSample.dist = length(lightSample.direction);
	float distSq = lightSample.dist * lightSample.dist;

	lightSample.direction /= lightSample.dist;
	lightSample.normal = normalize(lightSurfacePos - light.position);
	lightSample.emission = light.emission * float(numOfLights);
	lightSample.pdf = distSq / (light.area * 0.5 * abs(dot(lightSample.normal, lightSample.direction)));
}

void SampleRectLight(in Light light, in vec3 scatterPos, inout LightSampleRec lightSample)
{
	float r1 = rand();
	float r2 = rand();

	vec3 lightSurfacePos = light.position + light.u * r1 + light.v * r2;
	lightSample.direction = lightSurfacePos - scatterPos;
	lightSample.dist = length(lightSample.direction);
	float distSq = lightSample.dist * lightSample.dist;
	lightSample.direction /= lightSample.dist;
	lightSample.normal = normalize(cross(light.u, light.v));
	lightSample.emission = light.emission * float(numOfLights);
	lightSample.pdf = distSq / (light.area * abs(dot(lightSample.normal, lightSample.direction)));
}

void SampleDistantLight(in Light light, in vec3 scatterPos, inout LightSampleRec lightSample)
{
	lightSample.direction = normalize(light.position - vec3(0.0));
	lightSample.normal = normalize(scatterPos - light.position);
	lightSample.emission = light.emission * float(numOfLights);
	lightSample.dist = INF;
	lightSample.pdf = 1.0;
}

void SampleOneLight(in Light light, in vec3 scatterPos, inout LightSampleRec lightSample)
{
	int type = int(light.type);

	if (type == QUAD_LIGHT)
		SampleRectLight(light, scatterPos, lightSample);
	else if (type == SPHERE_LIGHT)
		SampleSphereLight(light, scatterPos, lightSample);
	else
		SampleDistantLight(light, scatterPos, lightSample);
}

vec3 SampleHG(vec3 V, float g, float r1, float r2)
{
	float cosTheta;

	if (abs(g) < 0.001)
		cosTheta = 1 - 2 * r2;
	else
	{
		float sqrTerm = (1 - g * g) / (1 + g - 2 * g * r2);
		cosTheta = -(1 + g * g - sqrTerm * sqrTerm) / (2 * g);
	}

	float phi = r1 * TWO_PI;
	float sinTheta = clamp(sqrt(1.0 - (cosTheta * cosTheta)), 0.0, 1.0);
	float sinPhi = sin(phi);
	float cosPhi = cos(phi);

	vec3 v1, v2;
	Onb(V, v1, v2);

	return sinTheta * cosPhi * v1 + sinTheta * sinPhi * v2 + cosTheta * V;
}

float PhaseHG(float cosTheta, float g)
{
	float denom = 1 + g * g + 2 * g * cosTheta;
	return INV_4_PI * (1 - g * g) / (denom * sqrt(denom));
}

#ifdef OPT_ENVMAP
#ifndef OPT_UNIFORM_LIGHT
//find the position of first element whose value is greater than the value, then convert to uv
vec2 BinarySearch(float value)
{
	ivec2 envMapResInt = ivec2(envMapRes);
	int lower = 0;
	int upper = envMapResInt.y - 1;
	while (lower < upper)
	{
		int mid = (lower + upper) >> 1;
		if (value < texelFetch(envMapCDFTex, ivec2(envMapResInt.x - 1, mid), 0).r)
			upper = mid;
		else
			lower = mid + 1;
	}
	int y = clamp(lower, 0, envMapResInt.y - 1);

	lower = 0;
	upper = envMapResInt.x - 1;
	while (lower < upper)
	{
		int mid = (lower + upper) >> 1;
		if (value < texelFetch(envMapCDFTex, ivec2(mid, y), 0).r)
			upper = mid;
		else
			lower = mid + 1;
	}
	int x = clamp(lower, 0, envMapResInt.x - 1);
	return vec2(x, y) / envMapRes;
}

vec4 EvalEnvMap(Ray r)
{
	float theta = acos(clamp(r.direction.y, -1.0, 1.0));
	vec2 uv = vec2((PI + atan(r.direction.z, r.direction.x)) * INV_TWO_PI, theta * INV_PI) + vec2(envMapRot, 0.0);

	vec3 color = texture(envMapTex, uv).rgb;
	float pdf = Luminance(color) / envMapTotalSum;

	return vec4(color, (pdf * envMapRes.x * envMapRes.y) / (TWO_PI * PI * sin(theta)));
}

vec4 SampleEnvMap(inout vec3 color)
{
	vec2 uv = BinarySearch(rand() * envMapTotalSum);

	color = texture(envMapTex, uv).rgb;
	float pdf = Luminance(color) / envMapTotalSum;

	uv.x -= envMapRot;
	float phi = uv.x * TWO_PI;
	float theta = uv.y * PI;

	if (sin(theta) == 0.0)
		pdf = 0.0;

	return vec4(-sin(theta) * cos(phi), cos(theta), -sin(theta) * sin(phi), (pdf * envMapRes.x * envMapRes.y) / (TWO_PI * PI * sin(theta)));
}

#endif
#endif

//to query BVH for Ray r to konw whether if r hit the scene
bool AnyHit(Ray r, float maxDist)
{

#ifdef OPT_LIGHTS
	// Intersect Emitters
	for (int i = 0; i < numOfLights; i++)
	{
		// Fetch light Data
		vec3 position = texelFetch(lightsTex, ivec2(i * 5 + 0, 0), 0).xyz;
		vec3 emission = texelFetch(lightsTex, ivec2(i * 5 + 1, 0), 0).xyz;
		vec3 u = texelFetch(lightsTex, ivec2(i * 5 + 2, 0), 0).xyz;
		vec3 v = texelFetch(lightsTex, ivec2(i * 5 + 3, 0), 0).xyz;
		vec3 params = texelFetch(lightsTex, ivec2(i * 5 + 4, 0), 0).xyz;
		float radius = params.x;
		float area = params.y;
		float type = params.z;

		// Intersect rectangular area light
		if (type == QUAD_LIGHT)
		{
			vec3 normal = normalize(cross(u, v));
			vec4 plane = vec4(normal, dot(normal, position));
			u *= 1.0f / dot(u, u);
			v *= 1.0f / dot(v, v);

			float d = RectIntersect(position, u, v, plane, r);
			if (d > 0.0 && d < maxDist)
				return true;
		}

		// Intersect spherical area light
		if (type == SPHERE_LIGHT)
		{
			float d = SphereIntersect(radius, position, r);
			if (d > 0.0 && d < maxDist)
				return true;
		}
	}
#endif

	// Intersect BVH and tris
	int stack[64];
	int ptr = 0;
	stack[ptr++] = -1;

	int index = topBVHIndex;
	float leftHit = 0.0;
	float rightHit = 0.0;

#if defined(OPT_ALPHA_TEST) && !defined(OPT_MEDIUM)
	int currMatID = 0;
#endif
	bool BLAS = false;

	Ray rTrans;
	rTrans.origin = r.origin;
	rTrans.direction = r.direction;

	while (index != -1)
	{
		ivec3 LRLeaf = ivec3(texelFetch(BVH, index * 3 + 2).xyz);

		int leftIndex = int(LRLeaf.x);
		int rightIndex = int(LRLeaf.y);
		int leaf = int(LRLeaf.z);

		if (leaf > 0) // Leaf node of BLAS
		{
			for (int i = 0; i < rightIndex; i++) // Loop through tris
			{
				ivec3 vertIndices = ivec3(texelFetch(vertexIndicesTex, leftIndex + i).xyz);

				vec4 v0 = texelFetch(verticesTex, vertIndices.x);
				vec4 v1 = texelFetch(verticesTex, vertIndices.y);
				vec4 v2 = texelFetch(verticesTex, vertIndices.z);

				float hitT;
				vec2 hitBary;
				if (IntersectTriangle(rTrans, v0.xyz, v1.xyz, v2.xyz, hitT, hitBary) && hitT < maxDist)
				{
#if defined(OPT_ALPHA_TEST) && !defined(OPT_MEDIUM)
					vec2 t0 = vec2(v0.w, texelFetch(normalsTex, vertIndices.x).w);
					vec2 t1 = vec2(v1.w, texelFetch(normalsTex, vertIndices.y).w);
					vec2 t2 = vec2(v2.w, texelFetch(normalsTex, vertIndices.z).w);

					vec2 texCoord = t0 * (1.0 - hitBary.x - hitBary.y) + t1 * hitBary.x + t2 * hitBary.y;

					vec4 texIDs = texelFetch(materialsTex, ivec2(currMatID * 8 + 6, 0), 0);
					vec4 alphaParams = texelFetch(materialsTex, ivec2(currMatID * 8 + 7, 0), 0);

					float alpha = texture(textureMapsArrayTex, vec3(texCoord, texIDs.x)).a;

					float opacity = alphaParams.x;
					int alphaMode = int(alphaParams.y);
					float alphaCutoff = alphaParams.z;
					opacity *= alpha;

					// Ignore intersection and continue ray based on alpha test
					if (!((alphaMode == ALPHA_MODE_MASK && opacity < alphaCutoff) ||
						(alphaMode == ALPHA_MODE_BLEND && rand() > opacity)))
						return true;
#else
					return true;
#endif
				}

			}
		}
		else if (leaf < 0) // Leaf node of TLAS
		{
			vec4 r1 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 4, 0), 0).xyzw;
			vec4 r2 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 5, 0), 0).xyzw;
			vec4 r3 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 6, 0), 0).xyzw;
			vec4 r4 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 7, 0), 0).xyzw;

			mat4 invTransform = mat4(r1, r2, r3, r4);

			rTrans.origin = vec3(invTransform * vec4(r.origin, 1.0));
			rTrans.direction = vec3(invTransform * vec4(r.direction, 0.0));

			// Add a marker. We'll return to this spot after we've traversed the entire BLAS
			stack[ptr++] = -1;

			index = leftIndex;
			BLAS = true;
#if defined(OPT_ALPHA_TEST) && !defined(OPT_MEDIUM)
			currMatID = rightIndex;
#endif
			continue;
		}
		else
		{
			leftHit = AABBIntersect(texelFetch(BVH, leftIndex * 3 + 0).xyz, texelFetch(BVH, leftIndex * 3 + 1).xyz, rTrans);
			rightHit = AABBIntersect(texelFetch(BVH, rightIndex * 3 + 0).xyz, texelFetch(BVH, rightIndex * 3 + 1).xyz, rTrans);

			if (leftHit > 0.0 && rightHit > 0.0)
			{
				int deferred = -1;
				if (leftHit > rightHit)
				{
					index = rightIndex;
					deferred = leftIndex;
				}
				else
				{
					index = leftIndex;
					deferred = rightIndex;
				}

				stack[ptr++] = deferred;
				continue;
			}
			else if (leftHit > 0.)
			{
				index = leftIndex;
				continue;
			}
			else if (rightHit > 0.)
			{
				index = rightIndex;
				continue;
			}
		}
		index = stack[--ptr];

		// If we've traversed the entire BLAS then switch to back to TLAS and resume where we left off
		if (BLAS && index == -1)
		{
			BLAS = false;

			index = stack[--ptr];

			rTrans.origin = r.origin;
			rTrans.direction = r.direction;
		}
	}

	return false;
}

bool ClosestHit(Ray r, inout State state, inout LightSampleRec lightSample)
{
	float t = INF;
	float d;

#ifdef OPT_LIGHTS
	// Intersect Emitters
#ifdef OPT_HIDE_EMITTERS
	if (state.depth > 0)
#endif
		for (int i = 0; i < numOfLights; i++)
		{
			// Fetch light Data
			vec3 position = texelFetch(lightsTex, ivec2(i * 5 + 0, 0), 0).xyz;
			vec3 emission = texelFetch(lightsTex, ivec2(i * 5 + 1, 0), 0).xyz;
			vec3 u = texelFetch(lightsTex, ivec2(i * 5 + 2, 0), 0).xyz;
			vec3 v = texelFetch(lightsTex, ivec2(i * 5 + 3, 0), 0).xyz;
			vec3 params = texelFetch(lightsTex, ivec2(i * 5 + 4, 0), 0).xyz;
			float radius = params.x;
			float area = params.y;
			float type = params.z;

			if (type == QUAD_LIGHT)
			{
				vec3 normal = normalize(cross(u, v));
				if (dot(normal, r.direction) > 0.) // Hide backfacing quad light
					continue;
				vec4 plane = vec4(normal, dot(normal, position));
				u *= 1.0f / dot(u, u);
				v *= 1.0f / dot(v, v);

				d = RectIntersect(position, u, v, plane, r);
				if (d < 0.)
					d = INF;
				if (d < t)
				{
					t = d;
					float cosTheta = dot(-r.direction, normal);
					lightSample.pdf = (t * t) / (area * cosTheta);
					lightSample.emission = emission;
					state.isEmitter = true;
				}
			}

			if (type == SPHERE_LIGHT)
			{
				d = SphereIntersect(radius, position, r);
				if (d < 0.)
					d = INF;
				if (d < t)
				{
					t = d;
					vec3 hitPt = r.origin + t * r.direction;
					float cosTheta = dot(-r.direction, normalize(hitPt - position));
					// TODO: Fix this. Currently assumes the light will be hit only from the outside
					lightSample.pdf = (t * t) / (area * cosTheta * 0.5);
					lightSample.emission = emission;
					state.isEmitter = true;
				}
			}
		}
#endif

	// Intersect BVH and tris
	int stack[64];
	int ptr = 0;
	stack[ptr++] = -1;

	int index = topBVHIndex;
	float leftHit = 0.0;
	float rightHit = 0.0;

	int currMatID = 0;
	bool BLAS = false;

	ivec3 triID = ivec3(-1);
	state.domainTint = vec3(1.0);
	
	mat4 transMat;
	mat4 transform;
	vec3 bary;
	vec4 vert0, vert1, vert2;

	Ray rTrans;
	rTrans.origin = r.origin;
	rTrans.direction = r.direction;

	while (index != -1)
	{
		ivec3 LRLeaf = ivec3(texelFetch(BVH, index * 3 + 2).xyz);
		int leftIndex = int(LRLeaf.x);
		int rightIndex = int(LRLeaf.y);
		int leaf = int(LRLeaf.z);

		if (leaf > 0) // Leaf node of BLAS
		{

			for (int i = 0; i < rightIndex; i++) // Loop through tris
			{
				ivec3 vertIndices = ivec3(texelFetch(vertexIndicesTex, leftIndex + i).xyz);

				vec4 v0 = texelFetch(verticesTex, vertIndices.x);
				vec4 v1 = texelFetch(verticesTex, vertIndices.y);
				vec4 v2 = texelFetch(verticesTex, vertIndices.z);

				float hitT;
				vec2 hitBary;
				if (IntersectTriangle(rTrans, v0.xyz, v1.xyz, v2.xyz, hitT, hitBary) && hitT < t)
				{
					//state.tid=leftIndex + i;
					
					t = hitT;
					triID = vertIndices;
					state.matID = currMatID;
					state.domainTint = texelFetch(triangleDomainColorTex, leftIndex + i).rgb;
					
					bary = vec3(1.0 - hitBary.x - hitBary.y, hitBary.x, hitBary.y);
					vert0 = v0, vert1 = v1, vert2 = v2;
					transform = transMat;
				}
			}
		}
		else if (leaf < 0) // Leaf node of TLAS
		{

			vec4 r1 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 0, 0), 0).xyzw;
			vec4 r2 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 1, 0), 0).xyzw;
			vec4 r3 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 2, 0), 0).xyzw;
			vec4 r4 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 3, 0), 0).xyzw;
			vec4 r5 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 4, 0), 0).xyzw;
			vec4 r6 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 5, 0), 0).xyzw;
			vec4 r7 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 6, 0), 0).xyzw;
			vec4 r8 = texelFetch(transformsTex, ivec2((-leaf - 1) * 8 + 7, 0), 0).xyzw;

			transMat = mat4(r1, r2, r3, r4);
			mat4 invTransMat = mat4(r5, r6, r7, r8);

			rTrans.origin = vec3(invTransMat * vec4(r.origin, 1.0));
			rTrans.direction = vec3(invTransMat * vec4(r.direction, 0.0));

			// Add a marker. We'll return to this spot after we've traversed the entire BLAS
			stack[ptr++] = -1;
			index = leftIndex;
			BLAS = true;
			currMatID = rightIndex;
			continue;
		}
		else
		{

			leftHit = AABBIntersect(texelFetch(BVH, leftIndex * 3 + 0).xyz, texelFetch(BVH, leftIndex * 3 + 1).xyz, rTrans);
			rightHit = AABBIntersect(texelFetch(BVH, rightIndex * 3 + 0).xyz, texelFetch(BVH, rightIndex * 3 + 1).xyz, rTrans);

			if (leftHit > 0.0 && rightHit > 0.0)
			{
				int deferred = -1;
				if (leftHit > rightHit)
				{
					index = rightIndex;
					deferred = leftIndex;
				}
				else
				{
					index = leftIndex;
					deferred = rightIndex;
				}

				stack[ptr++] = deferred;
				continue;
			}
			else if (leftHit > 0.)
			{
				index = leftIndex;
				continue;
			}
			else if (rightHit > 0.)
			{
				index = rightIndex;
				continue;
			}
		}
		index = stack[--ptr];

		// If we've traversed the entire BLAS then switch to back to TLAS and resume where we left off
		if (BLAS && index == -1)
		{
			BLAS = false;

			index = stack[--ptr];

			rTrans.origin = r.origin;
			rTrans.direction = r.direction;
		}
	}

	// No intersections
	if (t == INF)
		return false;

	state.hitDist = t;
	state.fhp = r.origin + r.direction * t;

	// Ray hit a triangle and not a light source
	if (triID.x != -1)
	{
		state.isEmitter = false;
        //meshTriangleInfoTex
		// Normals
		vec4 n0 = texelFetch(normalsTex, triID.x);
		vec4 n1 = texelFetch(normalsTex, triID.y);
		vec4 n2 = texelFetch(normalsTex, triID.z);

		// Get texcoords from w coord of vertices and normals
		vec2 t0 = vec2(vert0.w, n0.w);
		vec2 t1 = vec2(vert1.w, n1.w);
		vec2 t2 = vec2(vert2.w, n2.w);

		// Interpolate texture coords and normals using barycentric coords
		state.texCoord = t0 * bary.x + t1 * bary.y + t2 * bary.z;
		vec3 normal = normalize(n0.xyz * bary.x + n1.xyz * bary.y + n2.xyz * bary.z);

		state.normal = normalize(transpose(inverse(mat3(transform))) * normal);
		state.ffnormal = dot(state.normal, r.direction) <= 0.0 ? state.normal : -state.normal;

		// Calculate tangent and bitangent
		vec3 deltaPos1 = vert1.xyz - vert0.xyz;
		vec3 deltaPos2 = vert2.xyz - vert0.xyz;

		vec2 deltaUV1 = t1 - t0;
		vec2 deltaUV2 = t2 - t0;

		float invdet = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);

		state.tangent = (deltaPos1 * deltaUV2.y - deltaPos2 * deltaUV1.y) * invdet;
		state.bitangent = (deltaPos2 * deltaUV1.x - deltaPos1 * deltaUV2.x) * invdet;

		state.tangent = normalize(mat3(transform) * state.tangent);
		state.bitangent = normalize(mat3(transform) * state.bitangent);
	}

	return true;
}


/* References:
* [1] [Physically Based Shading at Disney] https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
* [2] [Extending the Disney BRDF to a BSDF with Integrated Subsurface Scattering] https://blog.selfshadow.com/publications/s2015-shading-course/burley/s2015_pbs_disney_bsdf_notes.pdf
* [3] [The Disney BRDF Explorer] https://github.com/wdas/brdf/blob/main/src/brdfs/disney.brdf
* [4] [Miles Macklin's implementation] https://github.com/mmacklin/tinsel/blob/master/src/disney.h
* [5] [Simon Kallweit's project report] http://simon-kallweit.me/rendercompo2015/report/
* [6] [Microfacet Models for Refraction through Rough Surfaces] https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
* [7] [Sampling the GGX Distribution of Visible Normals] https://jcgt.org/published/0007/04/01/paper.pdf
* [8] [Pixar's Foundation for Materials] https://graphics.pixar.com/library/PxrMaterialsCourse2017/paper.pdf
* [9] [Mitsuba 3] https://github.com/mitsuba-renderer/mitsuba3
*/

vec3 DisneyEval(State state, vec3 V, vec3 N, vec3 L, out float pdf);

vec3 ToWorld(vec3 X, vec3 Y, vec3 Z, vec3 V)
{
	return V.x * X + V.y * Y + V.z * Z;
}

vec3 ToLocal(vec3 X, vec3 Y, vec3 Z, vec3 V)
{
	return vec3(dot(V, X), dot(V, Y), dot(V, Z));
}

void TintColors(Material mat, float eta, out float F0, out vec3 Csheen, out vec3 Cspec0)
{
	float lum = Luminance(mat.baseColor);
	vec3 ctint = lum > 0.0 ? mat.baseColor / lum : vec3(1.0);

	F0 = (1.0 - eta) / (1.0 + eta);
	F0 *= F0;

	Cspec0 = F0 * mix(vec3(1.0), ctint, mat.specularTint);
	Csheen = mix(vec3(1.0), ctint, mat.sheenTint);
}

vec3 EvalDisneyDiffuse(Material mat, vec3 Csheen, vec3 V, vec3 L, vec3 H, out float pdf)
{
	pdf = 0.0;
	if (L.z <= 0.0)
		return vec3(0.0);

	float LDotH = dot(L, H);

	float Rr = 2.0 * mat.roughness * LDotH * LDotH;

	// Diffuse
	float FL = SchlickWeight(L.z);
	float FV = SchlickWeight(V.z);
	float Fretro = Rr * (FL + FV + FL * FV * (Rr - 1.0));
	float Fd = (1.0 - 0.5 * FL) * (1.0 - 0.5 * FV);

	// Fake subsurface
	float Fss90 = 0.5 * Rr;
	float Fss = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
	float ss = 1.25 * (Fss * (1.0 / (L.z + V.z) - 0.5) + 0.5);

	// Sheen
	float FH = SchlickWeight(LDotH);
	vec3 Fsheen = FH * mat.sheen * Csheen;

	pdf = L.z * INV_PI;
	return INV_PI * mat.baseColor * mix(Fd + Fretro, ss, mat.subsurface) + Fsheen;
}

vec3 EvalMicrofacetReflection(Material mat, vec3 V, vec3 L, vec3 H, vec3 F, out float pdf)
{
	pdf = 0.0;
	if (L.z <= 0.0)
		return vec3(0.0);

	float D = GTR2Aniso(H.z, H.x, H.y, mat.ax, mat.ay);
	float G1 = SmithGAniso(abs(V.z), V.x, V.y, mat.ax, mat.ay);
	float G2 = G1 * SmithGAniso(abs(L.z), L.x, L.y, mat.ax, mat.ay);

	pdf = G1 * D / (4.0 * V.z);
	return F * D * G2 / (4.0 * L.z * V.z);
}

vec3 EvalMicrofacetRefraction(Material mat, float eta, vec3 V, vec3 L, vec3 H, vec3 F, out float pdf)
{
	pdf = 0.0;
	if (L.z >= 0.0)
		return vec3(0.0);

	float LDotH = dot(L, H);
	float VDotH = dot(V, H);

	float D = GTR2Aniso(H.z, H.x, H.y, mat.ax, mat.ay);
	float G1 = SmithGAniso(abs(V.z), V.x, V.y, mat.ax, mat.ay);
	float G2 = G1 * SmithGAniso(abs(L.z), L.x, L.y, mat.ax, mat.ay);
	float denom = LDotH + VDotH * eta;
	denom *= denom;
	float eta2 = eta * eta;
	float jacobian = abs(LDotH) / denom;

	pdf = G1 * max(0.0, VDotH) * D * jacobian / V.z;
	return pow(mat.baseColor, vec3(0.5)) * (1.0 - F) * D * G2 * abs(VDotH) * jacobian * eta2 / abs(L.z * V.z);
}

vec3 EvalClearcoat(Material mat, vec3 V, vec3 L, vec3 H, out float pdf)
{
	pdf = 0.0;
	if (L.z <= 0.0)
		return vec3(0.0);

	float VDotH = dot(V, H);

	float F = mix(0.04, 1.0, SchlickWeight(VDotH));
	float D = GTR1(H.z, mat.clearcoatRoughness);
	float G = SmithG(L.z, 0.25) * SmithG(V.z, 0.25);
	float jacobian = 1.0 / (4.0 * VDotH);

	pdf = D * H.z * jacobian;
	return vec3(F) * D * G;
}

vec3 DisneySample(State state, vec3 V, vec3 N, out vec3 L, out float pdf)
{
	pdf = 0.0;

	float r1 = rand();
	float r2 = rand();

	// TODO: Tangent and bitangent should be calculated from mesh (provided, the mesh has proper uvs)
	vec3 T, B;
	Onb(N, T, B);

	// Transform to shading space to simplify operations (NDotL = L.z; NDotV = V.z; NDotH = H.z)
	V = ToLocal(T, B, N, V);

	// Tint colors
	vec3 Csheen, Cspec0;
	float F0;
	TintColors(state.mat, state.eta, F0, Csheen, Cspec0);

	// Model weights
	float dielectricWt = (1.0 - state.mat.metallic) * (1.0 - state.mat.specTrans);
	float metalWt = state.mat.metallic;
	float glassWt = (1.0 - state.mat.metallic) * state.mat.specTrans;

	// Lobe probabilities
	float schlickWt = SchlickWeight(V.z);

	float diffPr = dielectricWt * Luminance(state.mat.baseColor);
	float dielectricPr = dielectricWt * Luminance(mix(Cspec0, vec3(1.0), schlickWt));
	float metalPr = metalWt * Luminance(mix(state.mat.baseColor, vec3(1.0), schlickWt));
	float glassPr = glassWt;
	float clearCtPr = 0.25 * state.mat.clearcoat;

	// Normalize probabilities
	float invTotalWt = 1.0 / (diffPr + dielectricPr + metalPr + glassPr + clearCtPr);
	diffPr *= invTotalWt;
	dielectricPr *= invTotalWt;
	metalPr *= invTotalWt;
	glassPr *= invTotalWt;
	clearCtPr *= invTotalWt;

	// CDF of the sampling probabilities
	float cdf[5];
	cdf[0] = diffPr;
	cdf[1] = cdf[0] + dielectricPr;
	cdf[2] = cdf[1] + metalPr;
	cdf[3] = cdf[2] + glassPr;
	cdf[4] = cdf[3] + clearCtPr;

	// Sample a lobe based on its importance
	float r3 = rand();

	if (r3 < cdf[0]) // Diffuse
	{
		L = CosineSampleHemisphere(r1, r2);
	}
	else if (r3 < cdf[2]) // Dielectric + Metallic reflection
	{
		vec3 H = SampleGGXVNDF(V, state.mat.ax, state.mat.ay, r1, r2);

		if (H.z < 0.0)
			H = -H;

		L = normalize(reflect(-V, H));
	}
	else if (r3 < cdf[3]) // Glass
	{
		vec3 H = SampleGGXVNDF(V, state.mat.ax, state.mat.ay, r1, r2);
		float F = DielectricFresnel(abs(dot(V, H)), state.eta);

		if (H.z < 0.0)
			H = -H;

		// Rescale random number for reuse
		r3 = (r3 - cdf[2]) / (cdf[3] - cdf[2]);

		// Reflection
		if (r3 < F)
		{
			L = normalize(reflect(-V, H));
		}
		else // Transmission
		{
			L = normalize(refract(-V, H, state.eta));
		}
	}
	else // Clearcoat
	{
		vec3 H = SampleGTR1(state.mat.clearcoatRoughness, r1, r2);

		if (H.z < 0.0)
			H = -H;

		L = normalize(reflect(-V, H));
	}

	L = ToWorld(T, B, N, L);
	V = ToWorld(T, B, N, V);

	return DisneyEval(state, V, N, L, pdf);
}

vec3 DisneyEval(State state, vec3 V, vec3 N, vec3 L, out float pdf)
{
	pdf = 0.0;
	vec3 f = vec3(0.0);

	// TODO: Tangent and bitangent should be calculated from mesh (provided, the mesh has proper uvs)
	vec3 T, B;
	Onb(N, T, B);

	// Transform to shading space to simplify operations (NDotL = L.z; NDotV = V.z; NDotH = H.z)
	V = ToLocal(T, B, N, V);
	L = ToLocal(T, B, N, L);

	vec3 H;
	if (L.z > 0.0)
		H = normalize(L + V);
	else
		H = normalize(L + V * state.eta);

	if (H.z < 0.0)
		H = -H;

	// Tint colors
	vec3 Csheen, Cspec0;
	float F0;
	TintColors(state.mat, state.eta, F0, Csheen, Cspec0);

	// Model weights
	float dielectricWt = (1.0 - state.mat.metallic) * (1.0 - state.mat.specTrans);
	float metalWt = state.mat.metallic;
	float glassWt = (1.0 - state.mat.metallic) * state.mat.specTrans;

	// Lobe probabilities
	float schlickWt = SchlickWeight(V.z);

	float diffPr = dielectricWt * Luminance(state.mat.baseColor);
	float dielectricPr = dielectricWt * Luminance(mix(Cspec0, vec3(1.0), schlickWt));
	float metalPr = metalWt * Luminance(mix(state.mat.baseColor, vec3(1.0), schlickWt));
	float glassPr = glassWt;
	float clearCtPr = 0.25 * state.mat.clearcoat;

	// Normalize probabilities
	float invTotalWt = 1.0 / (diffPr + dielectricPr + metalPr + glassPr + clearCtPr);
	diffPr *= invTotalWt;
	dielectricPr *= invTotalWt;
	metalPr *= invTotalWt;
	glassPr *= invTotalWt;
	clearCtPr *= invTotalWt;

	bool reflect = L.z * V.z > 0;

	float tmpPdf = 0.0;
	float VDotH = abs(dot(V, H));

	// Diffuse
	if (diffPr > 0.0 && reflect)
	{
		f += EvalDisneyDiffuse(state.mat, Csheen, V, L, H, tmpPdf) * dielectricWt;
		pdf += tmpPdf * diffPr;
	}

	// Dielectric Reflection
	if (dielectricPr > 0.0 && reflect)
	{
		// Normalize for interpolating based on Cspec0
		float F = (DielectricFresnel(VDotH, 1.0 / state.mat.ior) - F0) / (1.0 - F0);

		f += EvalMicrofacetReflection(state.mat, V, L, H, mix(Cspec0, vec3(1.0), F), tmpPdf) * dielectricWt;
		pdf += tmpPdf * dielectricPr;
	}

	// Metallic Reflection
	if (metalPr > 0.0 && reflect)
	{
		// Tinted to base color
		vec3 F = mix(state.mat.baseColor, vec3(1.0), SchlickWeight(VDotH));

		f += EvalMicrofacetReflection(state.mat, V, L, H, F, tmpPdf) * metalWt;
		pdf += tmpPdf * metalPr;
	}

	// Glass/Specular BSDF
	if (glassPr > 0.0)
	{
		// Dielectric fresnel (achromatic)
		float F = DielectricFresnel(VDotH, state.eta);

		if (reflect)
		{
			f += EvalMicrofacetReflection(state.mat, V, L, H, vec3(F), tmpPdf) * glassWt;
			pdf += tmpPdf * glassPr * F;
		}
		else
		{
			f += EvalMicrofacetRefraction(state.mat, state.eta, V, L, H, vec3(F), tmpPdf) * glassWt;
			pdf += tmpPdf * glassPr * (1.0 - F);
		}
	}

	// Clearcoat
	if (clearCtPr > 0.0 && reflect)
	{
		f += EvalClearcoat(state.mat, V, L, H, tmpPdf) * 0.25 * state.mat.clearcoat;
		pdf += tmpPdf * clearCtPr;
	}

	return f * abs(L.z);
}

vec3 LambertSample(inout State state, vec3 V, vec3 N, inout vec3 L, inout float pdf)
{
	float r1 = rand();
	float r2 = rand();

	vec3 T, B;
	Onb(N, T, B);

	L = CosineSampleHemisphere(r1, r2);
	L = T * L.x + B * L.y + N * L.z;

	pdf = dot(N, L) * (1.0 / PI);

	return (1.0 / PI) * state.mat.baseColor * dot(N, L);
}

vec3 LambertEval(State state, vec3 V, vec3 N, vec3 L, inout float pdf)
{
	pdf = dot(N, L) * (1.0 / PI);

	return (1.0 / PI) * state.mat.baseColor * dot(N, L);
}


void GetMaterial(inout State state, in Ray r)
{
	int index = state.matID * 8;
	Material mat;
	Medium medium;

	vec4 param1 = texelFetch(materialsTex, ivec2(index + 0, 0), 0);
	vec4 param2 = texelFetch(materialsTex, ivec2(index + 1, 0), 0);
	vec4 param3 = texelFetch(materialsTex, ivec2(index + 2, 0), 0);
	vec4 param4 = texelFetch(materialsTex, ivec2(index + 3, 0), 0);
	vec4 param5 = texelFetch(materialsTex, ivec2(index + 4, 0), 0);
	vec4 param6 = texelFetch(materialsTex, ivec2(index + 5, 0), 0);
	vec4 param7 = texelFetch(materialsTex, ivec2(index + 6, 0), 0);
	vec4 param8 = texelFetch(materialsTex, ivec2(index + 7, 0), 0);

	mat.baseColor = param1.rgb;
	mat.anisotropic = param1.w;
	mat.emission = param2.rgb;
	mat.metallic = param3.x;
	mat.roughness = max(param3.y, 0.001);
	mat.subsurface = param3.z;
	mat.specularTint = param3.w;

	mat.sheen = param4.x;
	mat.sheenTint = param4.y;
	mat.clearcoat = param4.z;
	mat.clearcoatRoughness = mix(0.1, 0.001, param4.w); // Remapping from gloss to roughness

	mat.specTrans = param5.x;
	mat.ior = param5.y;
	mat.medium.type = int(param5.z);
	mat.medium.density = param5.w;

	mat.medium.color = param6.rgb;
	mat.medium.anisotropy = clamp(param6.w, -0.9, 0.9);

	ivec4 texIDs = ivec4(param7);

	mat.opacity = param8.x;
	mat.alphaMode = int(param8.y);
	mat.alphaCutoff = param8.z;

	// Base Color Map
	if (texIDs.x >= 0)
	{
		vec4 col = texture(textureMapsArrayTex, vec3(state.texCoord, texIDs.x));
		mat.baseColor.rgb *= pow(col.rgb, vec3(2.2));
		mat.opacity *= col.a;
	}

	// Metallic Roughness Map
	if (texIDs.y >= 0)
	{
		vec2 matRgh = texture(textureMapsArrayTex, vec3(state.texCoord, texIDs.y)).bg;
		mat.metallic = matRgh.x;
		mat.roughness = max(matRgh.y * matRgh.y, 0.001);
	}

	// Normal Map
	if (texIDs.z >= 0)
	{
		vec3 texNormal = texture(textureMapsArrayTex, vec3(state.texCoord, texIDs.z)).rgb;

#ifdef OPT_OPENGL_NORMALMAP
		texNormal.y = 1.0 - texNormal.y;
#endif
		texNormal = normalize(texNormal * 2.0 - 1.0);

		vec3 origNormal = state.normal;
		state.normal = normalize(state.tangent * texNormal.x + state.bitangent * texNormal.y + state.normal * texNormal.z);
		state.ffnormal = dot(origNormal, r.direction) <= 0.0 ? state.normal : -state.normal;
	}

#ifdef OPT_ROUGHNESS_MOLLIFICATION
	if (state.depth > 0)
		mat.roughness = max(mix(0.0, state.mat.roughness, roughnessMollificationAmt), mat.roughness);
#endif

	// Emission Map
	//if (texIDs.w >= 0)
	//	mat.emission = pow(texture(textureMapsArrayTex, vec3(state.texCoord, texIDs.w)).rgb, vec3(2.2));

	float aspect = sqrt(1.0 - mat.anisotropic * 0.9);
	mat.baseColor.rgb *= state.domainTint;
	mat.ax = max(0.001, mat.roughness / aspect);
	mat.ay = max(0.001, mat.roughness * aspect);

	state.mat = mat;
	state.eta = dot(r.direction, state.normal) < 0.0 ? (1.0 / mat.ior) : mat.ior;
}

// TODO: Recheck all of this
#if defined(OPT_MEDIUM) && defined(OPT_VOL_MIS)
vec3 EvalTransmittance(Ray r)
{
	LightSampleRec lightSample;
	State state;
	vec3 transmittance = vec3(1.0);

	for (int depth = 0; depth < maxDepth; depth++)
	{
		bool hit = ClosestHit(r, state, lightSample);

		// If no hit (environment map) or if ray hit a light source then return transmittance
		if (!hit || state.isEmitter)
			break;

		// TODO: Get only parameters that are needed to calculate transmittance
		GetMaterial(state, r);

		bool alphatest = (state.mat.alphaMode == ALPHA_MODE_MASK && state.mat.opacity < state.mat.alphaCutoff) || (state.mat.alphaMode == ALPHA_MODE_BLEND && rand() > state.mat.opacity);
		bool refractive = (1.0 - state.mat.metallic) * state.mat.specTrans > 0.0;

		// Refraction is ignored (Not physically correct but helps with sampling lights from inside refractive objects)
		if (hit && !(alphatest || refractive))
			return vec3(0.0);

		// Evaluate transmittance
		if (dot(r.direction, state.normal) > 0 && state.mat.medium.type != MEDIUM_NONE)
		{
			vec3 color = state.mat.medium.type == MEDIUM_ABSORB ? vec3(1.0) - state.mat.medium.color : vec3(1.0);
			transmittance *= exp(-color * state.mat.medium.density * state.hitDist);
		}

		// Move ray origin to hit point
		r.origin = state.fhp + r.direction * max(EPS, state.hitDist * 1e-4);
	}

	return transmittance;
}
#endif

vec3 DirectLight(in Ray r, in State state, bool isSurface)
{
	vec3 Ld = vec3(0.0);
	vec3 Li = vec3(0.0);
	vec3 scatterPos = state.fhp + state.ffnormal * max(EPS, state.hitDist * 1e-4);

	ScatterSampleRec scatterSample;

	// Environment Light
#ifdef OPT_ENVMAP
#ifndef OPT_UNIFORM_LIGHT
	{
		vec3 color;
		vec4 dirPdf = SampleEnvMap(Li);
		vec3 lightDir = dirPdf.xyz;
		float lightPdf = dirPdf.w;

		Ray shadowRay = Ray(scatterPos, lightDir);

#if defined(OPT_MEDIUM) && defined(OPT_VOL_MIS)
		// If there are volumes in the scene then evaluate transmittance rather than a binary anyhit test
		Li *= EvalTransmittance(shadowRay);

		if (isSurface)
			scatterSample.f = DisneyEval(state, -r.direction, state.ffnormal, lightDir, scatterSample.pdf);
		else
		{
			float p = PhaseHG(dot(-r.direction, lightDir), state.medium.anisotropy);
			scatterSample.f = vec3(p);
			scatterSample.pdf = p;
		}

		if (scatterSample.pdf > 0.0)
		{
			float misWeight = PowerHeuristic(lightPdf, scatterSample.pdf);
			if (misWeight > 0.0)
				Ld += misWeight * Li * scatterSample.f * envMapIntensity / lightPdf;
		}
#else
		// If there are no volumes in the scene then use a simple binary hit test
		bool inShadow = AnyHit(shadowRay, INF - EPS);

		if (!inShadow)
		{
			scatterSample.f = DisneyEval(state, -r.direction, state.ffnormal, lightDir, scatterSample.pdf);

			if (scatterSample.pdf > 0.0)
			{
				float misWeight = PowerHeuristic(lightPdf, scatterSample.pdf);
				if (misWeight > 0.0)
					Ld += misWeight * Li * scatterSample.f * envMapIntensity / lightPdf;
			}
		}
#endif
	}
#endif
#endif

	// Analytic Lights
#ifdef OPT_LIGHTS
	{
		LightSampleRec lightSample;
		Light light;

		//Pick a light to sample
		int index = int(rand() * float(numOfLights)) * 5;

		// Fetch light Data
		vec3 position = texelFetch(lightsTex, ivec2(index + 0, 0), 0).xyz;
		vec3 emission = texelFetch(lightsTex, ivec2(index + 1, 0), 0).xyz;
		vec3 u = texelFetch(lightsTex, ivec2(index + 2, 0), 0).xyz; // u vector for rect
		vec3 v = texelFetch(lightsTex, ivec2(index + 3, 0), 0).xyz; // v vector for rect
		vec3 params = texelFetch(lightsTex, ivec2(index + 4, 0), 0).xyz;
		float radius = params.x;
		float area = params.y;
		float type = params.z; // 0->Rect, 1->Sphere, 2->Distant

		light = Light(position, emission, u, v, radius, area, type);
		SampleOneLight(light, scatterPos, lightSample);
		Li = lightSample.emission;

		if (dot(lightSample.direction, lightSample.normal) < 0.0) // Required for quad lights with single sided emission
		{
			Ray shadowRay = Ray(scatterPos, lightSample.direction);

			// If there are volumes in the scene then evaluate transmittance rather than a binary anyhit test
#if defined(OPT_MEDIUM) && defined(OPT_VOL_MIS)
			Li *= EvalTransmittance(shadowRay);

			if (isSurface)
				scatterSample.f = DisneyEval(state, -r.direction, state.ffnormal, lightSample.direction, scatterSample.pdf);
			else
			{
				float p = PhaseHG(dot(-r.direction, lightSample.direction), state.medium.anisotropy);
				scatterSample.f = vec3(p);
				scatterSample.pdf = p;
			}

			float misWeight = 1.0;
			if (light.area > 0.0) // No MIS for distant light
				misWeight = PowerHeuristic(lightSample.pdf, scatterSample.pdf);

			if (scatterSample.pdf > 0.0)
				Ld += misWeight * scatterSample.f * Li / lightSample.pdf;
#else
			// If there are no volumes in the scene then use a simple binary hit test
			bool inShadow = AnyHit(shadowRay, lightSample.dist - EPS);

			if (!inShadow)
			{
				scatterSample.f = DisneyEval(state, -r.direction, state.ffnormal, lightSample.direction, scatterSample.pdf);

				float misWeight = 1.0;
				if (light.area > 0.0) // No MIS for distant light
					misWeight = PowerHeuristic(lightSample.pdf, scatterSample.pdf);

				if (scatterSample.pdf > 0.0)
					Ld += misWeight * Li * scatterSample.f / lightSample.pdf;
			}
#endif
		}
	}
#endif

	return Ld;
}

vec4 PathTrace(Ray r)
{
	vec3 radiance = vec3(0.0);
	vec3 throughput = vec3(1.0);

	State state;
	LightSampleRec lightSample;
	ScatterSampleRec scatterSample;

	// FIXME: alpha from material opacity/medium density
	float alpha = 1.0;

	// For medium tracking
	bool inMedium = false;
	bool mediumSampled = false;
	bool surfaceScatter = false;

	for (state.depth = 0;; state.depth++)
	{
		bool hit = ClosestHit(r, state, lightSample);
		//don't hit the scene,compute the contribution of Environment Light
		if (!hit)
		{
#if defined(OPT_BACKGROUND) || defined(OPT_TRANSPARENT_BACKGROUND)
			if (state.depth == 0)
				alpha = 0.0;
#endif

#ifdef OPT_HIDE_EMITTERS
			if (state.depth > 0)
#endif
			{
#ifdef OPT_UNIFORM_LIGHT
				radiance += uniformLightCol * throughput;
#else
#ifdef OPT_ENVMAP
				vec4 envMapColPdf = EvalEnvMap(r);

				float misWeight = 1.0;

				// Gather radiance from envmap and use scatterSample.pdf from previous bounce for MIS
				if (state.depth > 0)
					misWeight = PowerHeuristic(scatterSample.pdf, envMapColPdf.w);

#if defined(OPT_MEDIUM) && !defined(OPT_VOL_MIS)
				if (!surfaceScatter)
					misWeight = 1.0f;
#endif

				if (misWeight > 0)
					radiance += misWeight * envMapColPdf.rgb * throughput * envMapIntensity;
#endif
#endif
			}
			break;
		}
		//in this case， hit the scene
		GetMaterial(state, r);

		// Gather radiance from emissive objects. Emission from meshes is not importance sampled
		radiance += state.mat.emission * throughput;

		//in this case,hit the light,then evaluate the result of this path and return      
#ifdef OPT_LIGHTS

		// Gather radiance from light and use scatterSample.pdf from previous bounce for MIS
		if (state.isEmitter)
		{
			float misWeight = 1.0;

			if (state.depth > 0)
				misWeight = PowerHeuristic(scatterSample.pdf, lightSample.pdf);

#if defined(OPT_MEDIUM) && !defined(OPT_VOL_MIS)
			if (!surfaceScatter)
				misWeight = 1.0f;
#endif

			radiance += misWeight * lightSample.emission * throughput;

			break;
		}
#endif

		// Stop tracing ray if maximum depth was reached
		if (state.depth == maxDepth)
			break;

#ifdef OPT_MEDIUM

		mediumSampled = false;
		surfaceScatter = false;

		// Handle absorption/emission/scattering from medium
		// TODO: Handle light sources placed inside medium
		if (inMedium)
		{
			if (state.medium.type == MEDIUM_ABSORB)
			{
				throughput *= exp(-(1.0 - state.medium.color) * state.hitDist * state.medium.density);
			}
			else if (state.medium.type == MEDIUM_EMISSIVE)
			{
				radiance += state.medium.color * state.hitDist * state.medium.density * throughput;
			}
			else
			{
				// Sample a distance in the medium
				float scatterDist = min(-log(rand()) / state.medium.density, state.hitDist);
				mediumSampled = scatterDist < state.hitDist;

				if (mediumSampled)
				{
					throughput *= state.medium.color;

					// Move ray origin to scattering position
					r.origin += r.direction * scatterDist;
					state.fhp = r.origin;

					// Transmittance Evaluation
					radiance += DirectLight(r, state, false) * throughput;

					// Pick a new direction based on the phase function
					vec3 scatterDir = SampleHG(-r.direction, state.medium.anisotropy, rand(), rand());
					scatterSample.pdf = PhaseHG(dot(-r.direction, scatterDir), state.medium.anisotropy);
					r.direction = scatterDir;
				}
			}
		}

		// If medium was not sampled then proceed with surface BSDF evaluation
		if (!mediumSampled)
		{
#endif
			// when ALPHA_TEST is Enabled，the ray of path should keep going and -- depth
#ifdef OPT_ALPHA_TEST

			// Ignore intersection and continue ray based on alpha test
			if ((state.mat.alphaMode == ALPHA_MODE_MASK && state.mat.opacity < state.mat.alphaCutoff) ||
				(state.mat.alphaMode == ALPHA_MODE_BLEND && rand() > state.mat.opacity))
			{
				scatterSample.L = r.direction;
				state.depth--;
			}
			else
#endif
			{
				surfaceScatter = true;

				// Next event estimation
				radiance += DirectLight(r, state, true) * throughput;

				// Sample BSDF for color and outgoing direction
				scatterSample.f = DisneySample(state, -r.direction, state.ffnormal, scatterSample.L, scatterSample.pdf);
				if (scatterSample.pdf > 0.0)
					throughput *= scatterSample.f / scatterSample.pdf;
				else
					break;
			}

			// Move ray origin to hit point and set direction for next bounce
			r.direction = scatterSample.L;
			r.origin = state.fhp + r.direction * max(EPS, state.hitDist * 1e-4);

#ifdef OPT_MEDIUM

			// Note: Nesting of volumes isn't supported due to lack of a volume stack for performance reasons
			// Ray is in medium only if it is entering a surface containing a medium
			if (dot(r.direction, state.normal) < 0 && state.mat.medium.type != MEDIUM_NONE)
			{
				inMedium = true;
				// Get medium params from the intersected object
				state.medium = state.mat.medium;
			}
			// FIXME: Objects clipping or inside a medium were shaded incorrectly as inMedium would be set to false.
			// This hack works for now but needs some rethinking
			else if (state.mat.medium.type != MEDIUM_NONE)
				inMedium = false;
		}
#endif

#ifdef OPT_RR
		// Russian roulette
		if (state.depth >= RR_DEPTH)
		{
			float q = min(max(throughput.x, max(throughput.y, throughput.z)) + 0.001, 0.95);
			if (rand() > q)
				break;
			throughput /= q;
		}
#endif

	}

	return vec4(radiance, alpha);
}

void main(void)
{
	InitRNG(gl_FragCoord.xy, frameNum);
	float r1 = 2.0 * rand();
	float r2 = 2.0 * rand();
	vec2 coordsTile = mix(tileOffset, tileOffset + invNumTiles, TexCoords);
	vec2 jitter;
	jitter.x = r1 < 1.0 ? sqrt(r1) - 1.0 : 1.0 - sqrt(2.0 - r1);
	jitter.y = r2 < 1.0 ? sqrt(r2) - 1.0 : 1.0 - sqrt(2.0 - r2);

	jitter /= (resolution * 0.5);
	vec2 d = (coordsTile * 2.0 - 1.0) + jitter;
	float scale = tan(camera.fov * 0.5);
#if defined(PERSPECTIVE_CAMERA)
    d.y *=  scale;
	d.x *= resolution.x  /resolution.y *scale;
	vec3 rayDir = normalize(d.x * camera.right + d.y * camera.up + camera.forward);
	vec3 focalPoint = camera.focalDist * rayDir;
#else
    vec3 offsetVec =d.x * camera.right*camera.owidth*0.5 + d.y * camera.up*camera.oheight*0.5 ;
#endif

	float cam_r1 = rand() * TWO_PI;
	float cam_r2 = rand() * camera.aperture;
	vec3 randomAperturePos = (cos(cam_r1) * camera.right + sin(cam_r1) * camera.up) * sqrt(cam_r2);
#if defined(PERSPECTIVE_CAMERA)
	vec3 finalRayDir = normalize(focalPoint - randomAperturePos);
	Ray ray = Ray(camera.position + randomAperturePos, finalRayDir);
#else
    vec3 finalRayDir = normalize(camera.forward);
	Ray ray = Ray(camera.position + randomAperturePos+offsetVec, finalRayDir);
#endif

	vec4 accumColor = texture(accumTexture, coordsTile);

	vec4 pixelColor = PathTrace(ray);

	color = pixelColor + accumColor;
}