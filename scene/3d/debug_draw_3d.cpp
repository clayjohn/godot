/**************************************************************************/
/*  debug_draw_3d.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "debug_draw_3d.h"

#include "servers/rendering/rendering_server.h"

void DebugDraw3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("debug_draw_line", "start", "end", "color", "thickness"), &DebugDraw3D::debug_draw_line, DEFVAL(Color(1, 0, 0)), DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("debug_draw_persistent_line", "start", "end", "color", "thickness"), &DebugDraw3D::debug_draw_persistent_line, DEFVAL(Color(1, 0, 0)), DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("debug_clear_persistent_lines"), &DebugDraw3D::debug_clear_persistent_lines);
	ClassDB::bind_method(D_METHOD("update_mesh"), &DebugDraw3D::update_mesh);
}

void DebugDraw3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			RenderingServer::get_singleton()->connect("frame_pre_draw", Callable(this, "update_mesh"));
			set_process(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (RenderingServer::get_singleton()->is_connected("frame_pre_draw", Callable(this, "update_mesh"))) {
				RenderingServer::get_singleton()->disconnect("frame_pre_draw", Callable(this, "update_mesh"));
			}
		} break;
	}
}

void DebugDraw3D::debug_draw_line(const Vector3 &p_start, const Vector3 &p_end, const Color &p_color, float p_thickness) {
	Line l;
	l.start = p_start;
	l.end = p_end;
	l.color = p_color;
	l.thickness = p_thickness;
	lines.push_back(l);
}

void DebugDraw3D::debug_draw_persistent_line(const Vector3 &p_start, const Vector3 &p_end, const Color &p_color, float p_thickness) {
	Line l;
	l.start = p_start;
	l.end = p_end;
	l.color = p_color;
	l.thickness = p_thickness;
	persistent_lines.push_back(l);
}

void DebugDraw3D::debug_clear_persistent_lines() {
	persistent_lines.clear();
}

void DebugDraw3D::_add_line_vertex(const Line &l) {
	line_container->surface_set_color(l.color);

	// We use TANGENT to store the "other" endpoint.
	// We use UV.x for thickness and UV.y for expansion direction.

	// Triangle 1
	// P1, +
	line_container->surface_set_tangent(Plane(l.end.x, l.end.y, l.end.z, 0.0));
	line_container->surface_set_uv(Vector2(l.thickness, 1.0));
	line_container->surface_add_vertex(l.start);

	// P1, -
	line_container->surface_set_tangent(Plane(l.end.x, l.end.y, l.end.z, 0.0));
	line_container->surface_set_uv(Vector2(l.thickness, -1.0));
	line_container->surface_add_vertex(l.start);

	// P2, +
	line_container->surface_set_tangent(Plane(l.start.x, l.start.y, l.start.z, 0.0));
	line_container->surface_set_uv(Vector2(l.thickness, 1.0));
	line_container->surface_add_vertex(l.end);

	// Triangle 2
	// P1, -
	line_container->surface_set_tangent(Plane(l.end.x, l.end.y, l.end.z, 0.0));
	line_container->surface_set_uv(Vector2(l.thickness, -1.0));
	line_container->surface_add_vertex(l.start);

	// P2, -
	line_container->surface_set_tangent(Plane(l.start.x, l.start.y, l.start.z, 0.0));
	line_container->surface_set_uv(Vector2(l.thickness, -1.0));
	line_container->surface_add_vertex(l.end);

	// P2, +
	line_container->surface_set_tangent(Plane(l.start.x, l.start.y, l.start.z, 0.0));
	line_container->surface_set_uv(Vector2(l.thickness, 1.0));
	line_container->surface_add_vertex(l.end);
}

void DebugDraw3D::update_mesh() {
	if (lines.is_empty() && persistent_lines.is_empty()) {
		return;
	}

	line_container->clear_surfaces();
	line_container->surface_begin(Mesh::PRIMITIVE_TRIANGLES, material);

	for (const Line &l : lines) {
		_add_line_vertex(l);
	}

	for (const Line &l : persistent_lines) {
		_add_line_vertex(l);
	}

	line_container->surface_end();
	lines.clear();
}

DebugDraw3D::DebugDraw3D() {
	line_container.instantiate();
	set_base(line_container->get_rid());
	set_instance_use_identity_transform(true);

	material.instantiate();

	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(
			"shader_type spatial;\n"
			"render_mode unshaded, cull_disabled, skip_vertex_transform;\n"
			"\n"
			"void vertex() {\n"
			"    vec4 p1_clip = PROJECTION_MATRIX * MODELVIEW_MATRIX * vec4(VERTEX, 1.0);\n"
			"    vec3 p2_world = TANGENT + MODELVIEW_MATRIX[3].xyz;\n"
			"    vec4 p2_clip = PROJECTION_MATRIX * VIEW_MATRIX * vec4(p2_world, 1.0);\n"
			"\n"
			"    vec2 p1_ndc = p1_clip.xy / p1_clip.w;\n"
			"    vec2 p2_ndc = p2_clip.xy / p2_clip.w;\n"
			"\n"
			"    vec2 dir = normalize(p2_ndc - p1_ndc);\n"
			"    vec2 perp = vec2(-dir.y, dir.x);\n"
			"\n"
			"    // Aspect ratio correction\n"
			"    perp.x /= VIEWPORT_SIZE.x / VIEWPORT_SIZE.y;\n"
			"\n"
			"    // UV.x is thickness in pixels, UV.y is direction\n"
			"    vec2 offset = perp * (UV.x / VIEWPORT_SIZE.y) * UV.y;\n"
			"\n"
			"    POSITION = p1_clip + vec4(offset * p1_clip.w, 0.0, 0.0);\n"
			"}\n"
			"\n"
			"void fragment() {\n"
			"    ALBEDO = COLOR.rgb;\n"
			"}\n");
	material->set_shader(shader);
}

DebugDraw3D::~DebugDraw3D() {
}
