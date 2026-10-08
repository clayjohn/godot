/**************************************************************************/
/*  image_compress_icbc.cpp                                               */
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

#include "image_compress_icbc.h"

#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/templates/local_vector.h"

#include <icbc.h>

// Level 0 only does a box fit. Higher levels add least squares and cluster fit,
// improving quality at a large cost in encoding speed.
static constexpr icbc::Quality ICBC_QUALITY = icbc::Quality_Fast;

// A single row of 4x4 blocks within a mipmap. Rows are encoded in parallel.
struct ICBCBlockRow {
	const uint8_t *src = nullptr;
	uint8_t *dst = nullptr;
	int src_w = 0;
	int src_h = 0;
	int blocks_x = 0;
	int block_y = 0;
};

static void _compress_block_row(void *p_rows, uint32_t p_index) {
	const ICBCBlockRow &row = static_cast<const ICBCBlockRow *>(p_rows)[p_index];

	// All texels are equally important and the image has no alpha to weight by.
	static const float input_weights[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	static const float color_weights[3] = { 1, 1, 1 };

	for (int bx = 0; bx < row.blocks_x; bx++) {
		uint8_t *dst_block = row.dst + bx * 8;

		const uint8_t *first_pixel = row.src + (row.block_y * 4 * row.src_w + bx * 4) * 4;
		bool is_solid = true;

		alignas(16) float input_colors[16 * 4];

		// Gather the block, clamping to the edge of the mip to pad partial blocks.
		for (int y = 0; y < 4; y++) {
			const int sy = MIN(row.block_y * 4 + y, row.src_h - 1);
			for (int x = 0; x < 4; x++) {
				const int sx = MIN(bx * 4 + x, row.src_w - 1);
				const uint8_t *src_pixel = row.src + (sy * row.src_w + sx) * 4;
				float *dst_pixel = input_colors + (y * 4 + x) * 4;

				is_solid = is_solid && src_pixel[0] == first_pixel[0] && src_pixel[1] == first_pixel[1] && src_pixel[2] == first_pixel[2];

				dst_pixel[0] = src_pixel[0] / 255.0f;
				dst_pixel[1] = src_pixel[1] / 255.0f;
				dst_pixel[2] = src_pixel[2] / 255.0f;
				dst_pixel[3] = 1.0f;
			}
		}

		if (is_solid) {
			// Solid blocks are encoded optimally with a table lookup, skipping the full encoder.
			icbc::compress_bc1_single_color(first_pixel[0], first_pixel[1], first_pixel[2], dst_block);
			continue;
		}

		// Allow the 3 color mode, but not using its black index. The software decoder (and
		// some hardware formats) treat that index as transparent, which would create holes.
		icbc::compress_bc1(ICBC_QUALITY, input_colors, input_weights, color_weights, true, false, dst_block);
	}
}

void _compress_icbc_bc1(Image *r_img) {
	uint64_t start_time = OS::get_singleton()->get_ticks_msec();

	// The image is already compressed, return.
	if (r_img->is_compressed()) {
		return;
	}

	// Convert to RGBA8 for compression.
	r_img->convert(Image::FORMAT_RGBA8);

	const Image::Format target_format = Image::FORMAT_DXT1;
	const bool has_mipmaps = r_img->has_mipmaps();

	// The first mipmap level of a compressed texture must be a multiple of 4.
	// See the comment in `_compress_etcpak()` for details.
	int width = (r_img->get_width() + 3) & ~0x03;
	int height = (r_img->get_height() + 3) & ~0x03;

	if (r_img->get_width() != width || r_img->get_height() != height) {
		// Align the image to 4x4 texels.
		r_img->resize(width, height, Image::INTERPOLATE_NEAREST);
	}

	// Create the buffer for compressed image data.
	Vector<uint8_t> dest_data;
	dest_data.resize(Image::get_image_data_size(width, height, target_format, has_mipmaps));
	uint8_t *dest_write = dest_data.ptrw();

	const uint8_t *src_read = r_img->get_data().ptr();

	const int mip_count = has_mipmaps ? Image::get_image_required_mipmaps(width, height, target_format) : 0;

	// Gather the block rows of every mipmap, so they can all be encoded in a single group task.
	LocalVector<ICBCBlockRow> rows;

	for (int i = 0; i < mip_count + 1; i++) {
		// Get write mip metrics for target image.
		int dest_mip_w, dest_mip_h;
		int64_t dest_mip_ofs = Image::get_image_mipmap_offset_and_dimensions(width, height, target_format, i, dest_mip_w, dest_mip_h);

		const int blocks_x = (dest_mip_w + 3) / 4;
		const int blocks_y = (dest_mip_h + 3) / 4;

		// Get mip data from source image for reading.
		int64_t src_mip_ofs, src_mip_size;
		int src_mip_w, src_mip_h;
		r_img->get_mipmap_offset_size_and_dimensions(i, src_mip_ofs, src_mip_size, src_mip_w, src_mip_h);

		for (int by = 0; by < blocks_y; by++) {
			ICBCBlockRow row;
			row.src = src_read + src_mip_ofs;
			row.dst = dest_write + dest_mip_ofs + by * blocks_x * 8;
			row.src_w = src_mip_w;
			row.src_h = src_mip_h;
			row.blocks_x = blocks_x;
			row.block_y = by;
			rows.push_back(row);
		}
	}

	WorkerThreadPool::GroupID group_task = WorkerThreadPool::get_singleton()->add_native_group_task(&_compress_block_row, rows.ptr(), rows.size(), -1, true, SNAME("ICBC Compress"));
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);

	// Replace original image with compressed one.
	r_img->set_data(width, height, has_mipmaps, target_format, dest_data);

	print_verbose(vformat("icbc: Encoding took %d ms.", OS::get_singleton()->get_ticks_msec() - start_time));
}
