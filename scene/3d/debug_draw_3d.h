/**************************************************************************/
/*  debug_draw_3d.h                                                       */
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

#pragma once

#include "core/templates/local_vector.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/immediate_mesh.h"
#include "scene/resources/material.h"

class DebugDraw3D : public VisualInstance3D {
	GDCLASS(DebugDraw3D, VisualInstance3D);

	Ref<ImmediateMesh> line_container;
	Ref<ShaderMaterial> material;
	struct Line {
		Vector3 start;
		Vector3 end;
		Color color;
		float thickness;
	};

	LocalVector<Line> lines;
	LocalVector<Line> persistent_lines;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void debug_draw_line(const Vector3 &p_start, const Vector3 &p_end, const Color &p_color = Color(1, 0, 0), float p_thickness = 1.0f);
	void debug_draw_persistent_line(const Vector3 &p_start, const Vector3 &p_end, const Color &p_color = Color(1, 0, 0), float p_thickness = 1.0f);
	void debug_clear_persistent_lines();
	void update_mesh();

private:
	void _add_line_vertex(const Line &l);

	DebugDraw3D();
	~DebugDraw3D();
};
