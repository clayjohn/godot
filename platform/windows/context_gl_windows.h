/*************************************************************************/
/*  context_gl_windows.h                                                 */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#if defined(OPENGL_ENABLED) || defined(GLES_ENABLED)

// Author: Juan Linietsky <reduzio@gmail.com>, (C) 2008

#ifndef CONTEXT_GL_WIN_H
#define CONTEXT_GL_WIN_H

#include "temp_gl_defines.h"

#include "core/error/error_list.h"
#include "core/os/os.h"

#include "servers/display_server.h"
#include "core/templates/local_vector.h"

#include <windows.h>

typedef bool(APIENTRY *PFNWGLSWAPINTERVALEXTPROC)(int interval);
typedef int(APIENTRY *PFNWGLGETSWAPINTERVALEXTPROC)(void);

class ContextGL_Windows {


	struct GLWindow
	{
		GLWindow() {in_use = false;}
		
		bool in_use;
		
		// the external ID
		DisplayServer::WindowID window_id;
		int width;
		int height;
		HWND hWnd;
		int gldisplay_id;
	};
	
	struct GLDisplay
	{
		//GLDisplay() {}
		~GLDisplay();
		//GLManager_Windows_Private *context;
		//::Display *x11_display;
		//XVisualInfo x_vi;
		//XSetWindowAttributes x_swa;
		//unsigned long x_valuemask;
		HDC hDC;
		HGLRC hRC;
		unsigned int pixel_format;
		
	};
	
	LocalVector<GLWindow> _windows;
	LocalVector<GLDisplay> _displays;
	
	GLWindow * _current_window;
	
	void _internal_set_current_window(GLWindow * p_win);

	GLWindow &get_window(unsigned int id) {return _windows[id];}
	const GLWindow &get_window(unsigned int id) const {return _windows[id];}
	
	const GLDisplay &get_current_display() const {return _displays[_current_window->gldisplay_id];}
	const GLDisplay &get_display(unsigned int id) {return _displays[id];}
	

	HDC hDC;
	HGLRC hRC;
	unsigned int pixel_format;
	HWND hWnd;
	bool opengl_3_context;
	bool use_vsync;
	bool vsync_via_compositor;

	PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;
	PFNWGLGETSWAPINTERVALEXTPROC wglGetSwapIntervalEXT;

	static bool should_vsync_via_compositor();
private:
	//int _find_or_create_display(Display *p_x11_display);
	//Error _create_context(GLDisplay &gl_display);
public:
	Error window_create(DisplayServer::WindowID p_window_id, HWND p_window, HINSTANCE p_instance, int p_width, int p_height);
	void window_resize(DisplayServer::WindowID p_window_id, int p_width, int p_height);
	int window_get_width(DisplayServer::WindowID p_window = 0);
	int window_get_height(DisplayServer::WindowID p_window = 0);
	void window_destroy(DisplayServer::WindowID p_window_id);

	void make_window_current(DisplayServer::WindowID p_window_id);

	void release_current();

	void make_current();

	int get_window_width();
	int get_window_height();
	void swap_buffers();

	Error initialize();

	void set_use_vsync(bool p_use);
	bool is_using_vsync() const;

	ContextGL_Windows();
	~ContextGL_Windows();
};

#endif
#endif
