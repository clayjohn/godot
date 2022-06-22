#[compute]

#version 450

#VERSION_DEFINES

#define GROUP_SIZE 4

layout(local_size_x = GROUP_SIZE, local_size_y = GROUP_SIZE, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform samplerCube source_cube;

layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly imageCube dest_cubemap;

#include "cubemap_roughness_inc.glsl"

#define SAMPLE_COUNT 32

shared vec4 sample_directions[SAMPLE_COUNT];

void main() {
	uvec3 id = gl_GlobalInvocationID;
	id.z += params.face_id;

	vec2 uv = ((vec2(id.xy) * 2.0 + 1.0) / (params.face_size) - 1.0);
	vec3 N = texelCoordToVec(uv, id.z);

	if (params.use_direct_write) {
		imageStore(dest_cubemap, ivec3(id), vec4(texture(source_cube, N).rgb, 1.0));
	} else {
		float solid_angle_texel = 4.0 * M_PI / (6.0 * params.face_size * params.face_size);
		float roughness2 = params.roughness * params.roughness;
		float roughness4 = roughness2 * roughness2;
		vec3 UpVector = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
		mat3 T;
		T[0] = normalize(cross(UpVector, N));
		T[1] = cross(N, T[0]);
		T[2] = N;

		//Precompute sample directions instead of every pixel doing it
		// 4x4 block so each pixel is responsible for 2
		uint index = gl_LocalInvocationID.x * 2 + gl_LocalInvocationID.y * 8;

		vec2 xi = Hammersley(index, SAMPLE_COUNT);
		vec3 H = ImportanceSampleGGX(xi, roughness4);
		sample_directions[index].xyz = (2.0 * H.z * H - vec3(0.0, 0.0, 1.0));
		float D = DistributionGGX(H.z, roughness4);
		float pdf = D * H.z / (4.0 * H.z) + 0.0001;
		float solid_angle_sample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
		sample_directions[index].w = params.roughness == 0.0 ? 0.0 : 0.5 * log2(solid_angle_sample / solid_angle_texel);

		xi = Hammersley(index + 1, SAMPLE_COUNT);
		H = ImportanceSampleGGX(xi, roughness4);

		sample_directions[index + 1].xyz = (2.0 * H.z * H - vec3(0.0, 0.0, 1.0));
		D = DistributionGGX(H.z, roughness4);
		pdf = D * H.z / (4.0 * H.z) + 0.0001;
		solid_angle_sample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
		sample_directions[index].w = params.roughness == 0.0 ? 0.0 : 0.5 * log2(solid_angle_sample / solid_angle_texel);

		memoryBarrierShared();
		barrier();

		vec4 sum = vec4(0.0, 0.0, 0.0, 0.0);

		for (uint sampleNum = 0u; sampleNum < SAMPLE_COUNT; sampleNum++) {
			if (sample_directions[sampleNum].z > 0.0) {
				vec3 L = T * sample_directions[sampleNum].xyz;
				sum.rgb += textureLod(source_cube, L, sample_directions[sampleNum].w).rgb * sample_directions[sampleNum].z;
				sum.a += sample_directions[sampleNum].z;
			}
		}
		sum /= sum.a;

		imageStore(dest_cubemap, ivec3(id), vec4(sum.rgb, 1.0));
	}
}
