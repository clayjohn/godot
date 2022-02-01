/* clang-format off */
#[vertex]

#version 450

#VERSION_DEFINES

layout(location = 0) out vec2 uv_interp;
/* clang-format on */

layout(push_constant, binding = 1, std430) uniform Params {
	ivec2 source_size;
	ivec2 dest_size;
	int scale;
	int offset;
	bool first_frame;
	int pad;
	mat4 reprojection;
}
params;

void main() {
	vec2 base_arr[4] = vec2[](vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0));
	uv_interp = base_arr[gl_VertexIndex];

	gl_Position = vec4(uv_interp * 2.0 - 1.0, 0.0, 1.0);
}

/* clang-format off */
#[fragment]

#version 450

#VERSION_DEFINES


layout(location = 0) in vec2 uv_interp;
/* clang-format on */

layout(set = 0, binding = 0) uniform sampler2D source_sky;
layout(set = 0, binding = 1) uniform sampler2D source_history;

layout(push_constant, binding = 1, std430) uniform Params {
	ivec2 source_size;
	ivec2 dest_size;
	int scale;
	int offset;
	bool first_frame;
	int pad;
	mat4 reprojection;
}
params;

layout(location = 0) out highp vec4 out_color;

int check_pos(ivec2 x, int size) {
	return int((x.x % size) * size + (x.y % size));
}

void main() {
	ivec2 src_pos = ivec2(uv_interp * params.source_size);
	ivec2 dest_pos = ivec2(uv_interp * params.source_size * float(params.scale));

	if ((check_pos(dest_pos, params.scale) != params.offset) && (!params.first_frame)) {
		// Reprojection approach inspired by: http://john-chapman-graphics.blogspot.com/2013/01/what-is-motion-blur-motion-pictures-are.html
		vec4 current = vec4(uv_interp * 2.0 - 1.0, -1.0, 1.0); // We project the current pixel onto a curved plane 1 unit away from the camera
		vec4 previous = params.reprojection * current;
		previous.xyz /= previous.w;
		previous.xy = previous.xy * 0.5 + 0.5;
		if (previous.x <= 0.0 || previous.y <= 0.0 || previous.x >= 1.0 || previous.y >= 1.0) {
			// out of bounds, revert to low res sample
			out_color = textureLod(source_sky, uv_interp, 0.0);

		} else {
			// reprojected position is within the frame, so reproject
			out_color = textureLod(source_history, previous.xy, 0.0);
		}
		//out_color = vec4((previous.xy - uv_interp) * 10.0, 0.0, 1.0);
	} else {
		out_color = textureLod(source_sky, uv_interp, 0.0);
	}
}
