/**************************************************************************/
/*  triangle_mesh.cpp                                                     */
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

#include "triangle_mesh.h"

#include "core/math/math_funcs_binary.h"
#include "core/object/class_db.h"
#include "core/templates/hashfuncs.h"
#include "core/templates/local_vector.h"
#include "core/templates/sort_array.h"

// Mirrors HashMapComparatorDefault<Vector3>, which dispatches to
// Vector3::is_same(). Inlined here because Vector3::is_same() is out-of-line,
// and vertex deduplication calls it once per probe.
static _FORCE_INLINE_ bool _vertex_is_same(const Vector3 &p_a, const Vector3 &p_b) {
	return Math::is_same(p_a.x, p_b.x) && Math::is_same(p_a.y, p_b.y) && Math::is_same(p_a.z, p_b.z);
}

// Equivalent to hash_murmur3_one_real(), including its +/- 0.0 and NaN
// normalization, but inlined -- the real version lives in hashfuncs.cpp, which
// costs three out-of-line calls for every vertex hashed.
static _FORCE_INLINE_ uint32_t _hash_vertex_component(real_t p_in, uint32_t p_seed) {
#ifdef REAL_T_IS_DOUBLE
	union {
		double d;
		uint64_t i;
	} u;

	if (p_in == 0.0) {
		u.d = 0.0;
	} else if (Math::is_nan(p_in)) {
		u.d = Math::NaN;
	} else {
		u.d = p_in;
	}

	return hash_murmur3_one_64(u.i, p_seed);
#else
	union {
		float f;
		uint32_t i;
	} u;

	if (p_in == 0.0f) {
		u.f = 0.0;
	} else if (Math::is_nan(p_in)) {
		u.f = Math::NaN;
	} else {
		u.f = p_in;
	}

	return hash_murmur3_one_32(u.i, p_seed);
#endif
}

// Equivalent to Vector3::hash().
static _FORCE_INLINE_ uint32_t _hash_vertex(const Vector3 &p_vertex) {
	uint32_t h = _hash_vertex_component(p_vertex.x, HASH_MURMUR3_SEED);
	h = _hash_vertex_component(p_vertex.y, h);
	h = _hash_vertex_component(p_vertex.z, h);
	return hash_fmix32(h);
}

int TriangleMesh::_create_bvh(BVH *p_bvh, BVHLeaf *p_leaves, int p_from, int p_size, int p_depth, int &r_max_depth, int &r_max_alloc) {
	if (p_depth > r_max_depth) {
		r_max_depth = p_depth;
	}

	if (p_size == 1) {
		return p_leaves[p_from].index;
	} else if (p_size == 0) {
		return -1;
	}

	// Pick the split axis from the bounds of the leaf centers rather than from
	// the merged leaf AABBs. Both are a linear scan, but this one only touches
	// the compact center array, and it lets the node's own AABB be built from
	// its two children below instead of merging every leaf AABB again here.
	Vector3 center_min = p_leaves[p_from].center;
	Vector3 center_max = center_min;
	for (int i = 1; i < p_size; i++) {
		const Vector3 &center = p_leaves[p_from + i].center;
		center_min.x = MIN(center_min.x, center.x);
		center_min.y = MIN(center_min.y, center.y);
		center_min.z = MIN(center_min.z, center.z);
		center_max.x = MAX(center_max.x, center.x);
		center_max.y = MAX(center_max.y, center.y);
		center_max.z = MAX(center_max.z, center.z);
	}

	const Vector3 extents = center_max - center_min;
	int li = Vector3::AXIS_X;
	if (extents.y > extents.x) {
		li = (extents.z > extents.y) ? Vector3::AXIS_Z : Vector3::AXIS_Y;
	} else if (extents.z > extents.x) {
		li = Vector3::AXIS_Z;
	}

	switch (li) {
		case Vector3::AXIS_X: {
			SortArray<BVHLeaf, BVHCmpX> sort_x;
			sort_x.nth_element(0, p_size, p_size / 2, &p_leaves[p_from]);
		} break;
		case Vector3::AXIS_Y: {
			SortArray<BVHLeaf, BVHCmpY> sort_y;
			sort_y.nth_element(0, p_size, p_size / 2, &p_leaves[p_from]);
		} break;
		case Vector3::AXIS_Z: {
			SortArray<BVHLeaf, BVHCmpZ> sort_z;
			sort_z.nth_element(0, p_size, p_size / 2, &p_leaves[p_from]);
		} break;
	}

	// Both halves hold at least one leaf, so neither child can come back as -1.
	int left = _create_bvh(p_bvh, p_leaves, p_from, p_size / 2, p_depth + 1, r_max_depth, r_max_alloc);
	int right = _create_bvh(p_bvh, p_leaves, p_from + p_size / 2, p_size - p_size / 2, p_depth + 1, r_max_depth, r_max_alloc);

	int index = r_max_alloc++;
	BVH *_new = &p_bvh[index];
	_new->aabb = p_bvh[left].aabb.merge(p_bvh[right].aabb);
	_new->center = _new->aabb.get_center();
	_new->face_index = -1;
	_new->left = left;
	_new->right = right;

	return index;
}

void TriangleMesh::get_indices(Vector<int> *r_triangles_indices) const {
	if (!valid) {
		return;
	}

	const int triangles_num = triangles.size();

	// Parse vertices indices
	const Triangle *triangles_read = triangles.ptr();

	r_triangles_indices->resize(triangles_num * 3);
	int *r_indices_write = r_triangles_indices->ptrw();

	for (int i = 0; i < triangles_num; ++i) {
		r_indices_write[3 * i + 0] = triangles_read[i].indices[0];
		r_indices_write[3 * i + 1] = triangles_read[i].indices[1];
		r_indices_write[3 * i + 2] = triangles_read[i].indices[2];
	}
}

void TriangleMesh::create(const Vector<Vector3> &p_faces, const Vector<int32_t> &p_surface_indices) {
	valid = false;

	ERR_FAIL_COND(p_surface_indices.size() && p_surface_indices.size() != p_faces.size());

	int fc = p_faces.size();
	ERR_FAIL_COND(!fc || ((fc % 3) != 0));
	fc /= 3;
	triangles.resize(fc);

	// A binary tree over `fc` leaves needs `fc` leaf nodes plus `fc - 1`
	// internal nodes, so this is always enough.
	bvh.resize(fc * 2);
	BVH *bw = bvh.ptrw();

	LocalVector<BVHLeaf> leaves;
	leaves.resize_uninitialized(fc);
	BVHLeaf *lw = leaves.ptr();

	{
		//create faces and indices and base bvh
		//except for the dedup table for repeated vertices, everything
		//goes in-place.

		const Vector3 *r = p_faces.ptr();
		const int32_t *si = p_surface_indices.ptr();
		Triangle *w = triangles.ptrw();

		// Open addressed table of indices into `vertices`, with -1 marking a
		// free slot. Sized to a power of two so the bucket index is a mask
		// instead of a modulo, and large enough that it never has to grow and
		// that probe chains stay short.
		const uint32_t table_size = (uint32_t)Math::next_power_of_2((uint64_t)fc * 6);
		const uint32_t table_mask = table_size - 1;
		LocalVector<int32_t> table;
		table.resize_uninitialized(table_size);
		int32_t *tw = table.ptr();
		memset(tw, -1, table_size * sizeof(int32_t));

		// Worst case every vertex is unique. Truncated once the real count is known.
		vertices.resize(fc * 3);
		Vector3 *vw = vertices.ptrw();
		int vertex_count = 0;

		for (int i = 0; i < fc; i++) {
			Triangle &f = w[i];
			const Vector3 *v = &r[i * 3];

			for (int j = 0; j < 3; j++) {
				const Vector3 vs = v[j].snappedf(0.0001);

				int32_t vidx;
				uint32_t slot = _hash_vertex(vs) & table_mask;
				while (true) {
					const int32_t existing = tw[slot];
					if (existing < 0) {
						vidx = vertex_count++;
						vw[vidx] = vs;
						tw[slot] = vidx;
						break;
					}
					if (_vertex_is_same(vw[existing], vs)) {
						vidx = existing;
						break;
					}
					slot = (slot + 1) & table_mask;
				}

				f.indices[j] = vidx;
				if (j == 0) {
					bw[i].aabb = AABB(vs, Vector3());
				} else {
					bw[i].aabb.expand_to(vs);
				}
			}

			f.normal = Face3(r[i * 3 + 0], r[i * 3 + 1], r[i * 3 + 2]).get_plane().get_normal();
			f.surface_index = si ? si[i] : 0;

			bw[i].left = -1;
			bw[i].right = -1;
			bw[i].face_index = i;
			bw[i].center = bw[i].aabb.get_center();

			lw[i].center = bw[i].center;
			lw[i].index = i;
		}

		vertices.resize(vertex_count);
	}

	max_depth = 0;
	int max_alloc = fc;
	_create_bvh(bw, lw, 0, fc, 1, max_depth, max_alloc);

	bvh.resize(max_alloc); //resize back

	valid = true;
}

bool TriangleMesh::intersect_segment(const Vector3 &p_begin, const Vector3 &p_end, Vector3 &r_point, Vector3 &r_normal, int32_t *r_surf_index, int32_t *r_face_index) const {
	if (!valid) {
		return false;
	}

	uint32_t *stack = (uint32_t *)alloca(sizeof(int) * max_depth);

	enum {
		TEST_AABB_BIT = 0,
		VISIT_LEFT_BIT = 1,
		VISIT_RIGHT_BIT = 2,
		VISIT_DONE_BIT = 3,
		VISITED_BIT_SHIFT = 29,
		NODE_IDX_MASK = (1 << VISITED_BIT_SHIFT) - 1,
		VISITED_BIT_MASK = ~NODE_IDX_MASK,

	};

	Vector3 n = (p_end - p_begin).normalized();
	real_t d = 1e10;
	bool inters = false;

	int level = 0;

	const Triangle *triangleptr = triangles.ptr();
	const Vector3 *vertexptr = vertices.ptr();
	const BVH *bvhptr = bvh.ptr();

	int pos = bvh.size() - 1;

	stack[0] = pos;
	while (true) {
		uint32_t node = stack[level] & NODE_IDX_MASK;
		const BVH &b = bvhptr[node];
		bool done = false;

		switch (stack[level] >> VISITED_BIT_SHIFT) {
			case TEST_AABB_BIT: {
				if (!b.aabb.intersects_segment(p_begin, p_end)) {
					stack[level] = (VISIT_DONE_BIT << VISITED_BIT_SHIFT) | node;
				} else {
					if (b.face_index >= 0) {
						const Triangle &s = triangleptr[b.face_index];
						Face3 f3(vertexptr[s.indices[0]], vertexptr[s.indices[1]], vertexptr[s.indices[2]]);

						Vector3 res;

						if (f3.intersects_segment(p_begin, p_end, &res)) {
							real_t nd = n.dot(res);
							if (nd < d) {
								d = nd;
								r_point = res;
								r_normal = f3.get_plane().get_normal();
								if (r_surf_index) {
									*r_surf_index = s.surface_index;
								}
								if (r_face_index) {
									*r_face_index = b.face_index;
								}
								inters = true;
							}
						}

						stack[level] = (VISIT_DONE_BIT << VISITED_BIT_SHIFT) | node;

					} else {
						stack[level] = (VISIT_LEFT_BIT << VISITED_BIT_SHIFT) | node;
					}
				}
				continue;
			}
			case VISIT_LEFT_BIT: {
				stack[level] = (VISIT_RIGHT_BIT << VISITED_BIT_SHIFT) | node;
				level++;
				stack[level] = b.left | TEST_AABB_BIT;
				continue;
			}
			case VISIT_RIGHT_BIT: {
				stack[level] = (VISIT_DONE_BIT << VISITED_BIT_SHIFT) | node;
				level++;
				stack[level] = b.right | TEST_AABB_BIT;
				continue;
			}
			case VISIT_DONE_BIT: {
				if (level == 0) {
					done = true;
					break;
				} else {
					level--;
				}
				continue;
			}
		}

		if (done) {
			break;
		}
	}

	if (inters) {
		if (n.dot(r_normal) > 0) {
			r_normal = -r_normal;
		}
	}

	return inters;
}

bool TriangleMesh::intersect_ray(const Vector3 &p_begin, const Vector3 &p_dir, Vector3 &r_point, Vector3 &r_normal, int32_t *r_surf_index, int32_t *r_face_index) const {
	if (!valid) {
		return false;
	}

	uint32_t *stack = (uint32_t *)alloca(sizeof(int) * max_depth);

	enum {
		TEST_AABB_BIT = 0,
		VISIT_LEFT_BIT = 1,
		VISIT_RIGHT_BIT = 2,
		VISIT_DONE_BIT = 3,
		VISITED_BIT_SHIFT = 29,
		NODE_IDX_MASK = (1 << VISITED_BIT_SHIFT) - 1,
		VISITED_BIT_MASK = ~NODE_IDX_MASK,

	};

	Vector3 n = p_dir;
	real_t d = 1e20;
	bool inters = false;

	int level = 0;

	const Triangle *triangleptr = triangles.ptr();
	const Vector3 *vertexptr = vertices.ptr();
	const BVH *bvhptr = bvh.ptr();

	int pos = bvh.size() - 1;

	stack[0] = pos;
	while (true) {
		uint32_t node = stack[level] & NODE_IDX_MASK;
		const BVH &b = bvhptr[node];
		bool done = false;

		switch (stack[level] >> VISITED_BIT_SHIFT) {
			case TEST_AABB_BIT: {
				if (!b.aabb.intersects_ray(p_begin, p_dir)) {
					stack[level] = (VISIT_DONE_BIT << VISITED_BIT_SHIFT) | node;
				} else {
					if (b.face_index >= 0) {
						const Triangle &s = triangleptr[b.face_index];
						Face3 f3(vertexptr[s.indices[0]], vertexptr[s.indices[1]], vertexptr[s.indices[2]]);

						Vector3 res;

						if (f3.intersects_ray(p_begin, p_dir, &res)) {
							real_t nd = n.dot(res);
							if (nd < d) {
								d = nd;
								r_point = res;
								r_normal = f3.get_plane().get_normal();
								if (r_surf_index) {
									*r_surf_index = s.surface_index;
								}
								if (r_face_index) {
									*r_face_index = b.face_index;
								}
								inters = true;
							}
						}

						stack[level] = (VISIT_DONE_BIT << VISITED_BIT_SHIFT) | node;

					} else {
						stack[level] = (VISIT_LEFT_BIT << VISITED_BIT_SHIFT) | node;
					}
				}
				continue;
			}
			case VISIT_LEFT_BIT: {
				stack[level] = (VISIT_RIGHT_BIT << VISITED_BIT_SHIFT) | node;
				level++;
				stack[level] = b.left | TEST_AABB_BIT;
				continue;
			}
			case VISIT_RIGHT_BIT: {
				stack[level] = (VISIT_DONE_BIT << VISITED_BIT_SHIFT) | node;
				level++;
				stack[level] = b.right | TEST_AABB_BIT;
				continue;
			}
			case VISIT_DONE_BIT: {
				if (level == 0) {
					done = true;
					break;
				} else {
					level--;
				}
				continue;
			}
		}

		if (done) {
			break;
		}
	}

	if (inters) {
		if (n.dot(r_normal) > 0) {
			r_normal = -r_normal;
		}
	}

	return inters;
}

bool TriangleMesh::inside_convex_shape(const Plane *p_planes, int p_plane_count, const Vector3 *p_points, int p_point_count, Vector3 p_scale) const {
	if (!valid) {
		return false;
	}

	uint32_t *stack = (uint32_t *)alloca(sizeof(int) * max_depth);

	enum {
		TEST_AABB_BIT = 0,
		VISIT_LEFT_BIT = 1,
		VISIT_RIGHT_BIT = 2,
		VISIT_DONE_BIT = 3,
		VISITED_BIT_SHIFT = 29,
		NODE_IDX_MASK = (1 << VISITED_BIT_SHIFT) - 1,
		VISITED_BIT_MASK = ~NODE_IDX_MASK,

	};

	int level = 0;

	const Triangle *triangleptr = triangles.ptr();
	const Vector3 *vertexptr = vertices.ptr();
	const BVH *bvhptr = bvh.ptr();

	Transform3D scale(Basis().scaled(p_scale));

	int pos = bvh.size() - 1;

	stack[0] = pos;
	while (true) {
		uint32_t node = stack[level] & NODE_IDX_MASK;
		const BVH &b = bvhptr[node];
		bool done = false;

		switch (stack[level] >> VISITED_BIT_SHIFT) {
			case TEST_AABB_BIT: {
				bool intersects = scale.xform(b.aabb).intersects_convex_shape(p_planes, p_plane_count, p_points, p_point_count);
				if (!intersects) {
					return false;
				}

				bool inside = scale.xform(b.aabb).inside_convex_shape(p_planes, p_plane_count);
				if (inside) {
					stack[level] = (VISIT_DONE_BIT << VISITED_BIT_SHIFT) | node;

				} else {
					if (b.face_index >= 0) {
						const Triangle &s = triangleptr[b.face_index];
						for (int j = 0; j < 3; ++j) {
							Vector3 point = scale.xform(vertexptr[s.indices[j]]);
							for (int i = 0; i < p_plane_count; i++) {
								const Plane &p = p_planes[i];
								if (p.is_point_over(point)) {
									return false;
								}
							}
						}

						stack[level] = (VISIT_DONE_BIT << VISITED_BIT_SHIFT) | node;

					} else {
						stack[level] = (VISIT_LEFT_BIT << VISITED_BIT_SHIFT) | node;
					}
				}
				continue;
			}
			case VISIT_LEFT_BIT: {
				stack[level] = (VISIT_RIGHT_BIT << VISITED_BIT_SHIFT) | node;
				level++;
				stack[level] = b.left | TEST_AABB_BIT;
				continue;
			}
			case VISIT_RIGHT_BIT: {
				stack[level] = (VISIT_DONE_BIT << VISITED_BIT_SHIFT) | node;
				level++;
				stack[level] = b.right | TEST_AABB_BIT;
				continue;
			}
			case VISIT_DONE_BIT: {
				if (level == 0) {
					done = true;
					break;
				} else {
					level--;
				}
				continue;
			}
		}

		if (done) {
			break;
		}
	}

	return true;
}

bool TriangleMesh::is_valid() const {
	return valid;
}

Vector<Face3> TriangleMesh::get_faces() const {
	if (!valid) {
		return Vector<Face3>();
	}

	Vector<Face3> faces;
	int ts = triangles.size();
	faces.resize(triangles.size());

	Face3 *w = faces.ptrw();
	const Triangle *r = triangles.ptr();
	const Vector3 *rv = vertices.ptr();

	for (int i = 0; i < ts; i++) {
		for (int j = 0; j < 3; j++) {
			w[i].vertex[j] = rv[r[i].indices[j]];
		}
	}

	return faces;
}

bool TriangleMesh::create_from_faces(const Vector<Vector3> &p_faces) {
	create(p_faces);
	return is_valid();
}

Dictionary TriangleMesh::intersect_segment_scriptwrap(const Vector3 &p_begin, const Vector3 &p_end) const {
	if (!valid) {
		return Dictionary();
	}

	Vector3 r_point;
	Vector3 r_normal;
	int32_t r_face_index = -1;

	bool intersected = intersect_segment(p_begin, p_end, r_point, r_normal, nullptr, &r_face_index);
	if (!intersected) {
		return Dictionary();
	}

	Dictionary result;
	result["position"] = r_point;
	result["normal"] = r_normal;
	result["face_index"] = r_face_index;

	return result;
}

Dictionary TriangleMesh::intersect_ray_scriptwrap(const Vector3 &p_begin, const Vector3 &p_dir) const {
	if (!valid) {
		return Dictionary();
	}

	Vector3 r_point;
	Vector3 r_normal;
	int32_t r_face_index = -1;

	bool intersected = intersect_ray(p_begin, p_dir, r_point, r_normal, nullptr, &r_face_index);
	if (!intersected) {
		return Dictionary();
	}

	Dictionary result;
	result["position"] = r_point;
	result["normal"] = r_normal;
	result["face_index"] = r_face_index;

	return result;
}

Vector<Vector3> TriangleMesh::get_faces_scriptwrap() const {
	if (!valid) {
		return Vector<Vector3>();
	}

	Vector<Vector3> faces;
	int ts = triangles.size();
	faces.resize(triangles.size() * 3);

	Vector3 *w = faces.ptrw();
	const Triangle *r = triangles.ptr();
	const Vector3 *rv = vertices.ptr();

	for (int i = 0; i < ts; i++) {
		for (int j = 0; j < 3; j++) {
			w[i * 3 + j] = rv[r[i].indices[j]];
		}
	}

	return faces;
}

void TriangleMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create_from_faces", "faces"), &TriangleMesh::create_from_faces);
	ClassDB::bind_method(D_METHOD("get_faces"), &TriangleMesh::get_faces_scriptwrap);

	ClassDB::bind_method(D_METHOD("intersect_segment", "begin", "end"), &TriangleMesh::intersect_segment_scriptwrap);
	ClassDB::bind_method(D_METHOD("intersect_ray", "begin", "dir"), &TriangleMesh::intersect_ray_scriptwrap);
}

TriangleMesh::TriangleMesh() {
	valid = false;
	max_depth = 0;
}
