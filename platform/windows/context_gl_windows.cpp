/*************************************************************************/
/*  context_gl_windows.cpp                                               */
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

#include "temp_gl_defines.h"

#if defined(OPENGL_ENABLED) || defined(GLES_ENABLED)

// Author: Juan Linietsky <reduzio@gmail.com>, (C) 2008

#include "context_gl_windows.h"

#include <dwmapi.h>

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

#if defined(__GNUC__)
// Workaround GCC warning from -Wcast-function-type.
#define wglGetProcAddress (void *)wglGetProcAddress
#endif

typedef HGLRC(APIENTRY *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int *);

ContextGL_Windows::GLDisplay::~GLDisplay()
{
	//if (context)
	{
		//release_current();
		//glXDestroyContext(x11_display, context->glx_context);
		//memdelete(context);
		//context = nullptr;
	}
}

void ContextGL_Windows::release_current() {
	const GLDisplay &disp = get_current_display();
	wglMakeCurrent(disp.hDC, nullptr);
}

void ContextGL_Windows::make_current() {
	const GLDisplay &disp = get_current_display();
	wglMakeCurrent(disp.hDC, disp.hRC);
}

void ContextGL_Windows::make_window_current(DisplayServer::WindowID p_window_id)
{
	if (p_window_id == -1)
		return;
	
	GLWindow &win = _windows[p_window_id];
	if (!win.in_use)
		return;
	
	// noop
	if (&win == _current_window)
		return;
	
	const GLDisplay &disp = get_display(win.gldisplay_id);
	
	wglMakeCurrent(disp.hDC, disp.hRC);
	
	_internal_set_current_window(&win);
}

void ContextGL_Windows::_internal_set_current_window(GLWindow * p_win)
{
	_current_window = p_win;
	
	// quick access to x info	
	hWnd = _current_window->hWnd;
	const GLDisplay &disp = get_current_display();
	hDC = disp.hDC;
	hRC = disp.hRC;
	pixel_format = disp.pixel_format;
}

Error ContextGL_Windows::window_create(DisplayServer::WindowID p_window_id, HWND p_window, HINSTANCE p_instance, int p_width, int p_height)
{

		static PIXELFORMATDESCRIPTOR pfd = {
		sizeof(PIXELFORMATDESCRIPTOR), // Size Of This Pixel Format Descriptor
		1,
		PFD_DRAW_TO_WINDOW | // Format Must Support Window
				PFD_SUPPORT_OPENGL | // Format Must Support OpenGL
				PFD_DOUBLEBUFFER,
		(BYTE)PFD_TYPE_RGBA,
		(BYTE)(OS::get_singleton()->is_layered_allowed() ? 32 : 24),
		(BYTE)0, (BYTE)0, (BYTE)0, (BYTE)0, (BYTE)0, (BYTE)0, // Color Bits Ignored
		(BYTE)(OS::get_singleton()->is_layered_allowed() ? 8 : 0), // Alpha Buffer
		(BYTE)0, // Shift Bit Ignored
		(BYTE)0, // No Accumulation Buffer
		(BYTE)0, (BYTE)0, (BYTE)0, (BYTE)0, // Accumulation Bits Ignored
		(BYTE)24, // 24Bit Z-Buffer (Depth Buffer)
		(BYTE)0, // No Stencil Buffer
		(BYTE)0, // No Auxiliary Buffer
		(BYTE)PFD_MAIN_PLANE, // Main Drawing Layer
		(BYTE)0, // Reserved
		0, 0, 0 // Layer Masks Ignored
	};

	HDC t_hDC = GetDC(p_window);
	if (!t_hDC) {
		return ERR_CANT_CREATE; // Return FALSE
	}

	int t_pixel_format = ChoosePixelFormat(t_hDC, &pfd);
	if (!t_pixel_format) // Did Windows Find A Matching Pixel Format?
	{
		return ERR_CANT_CREATE; // Return FALSE
	}

	BOOL ret = SetPixelFormat(t_hDC, t_pixel_format, &pfd);
	if (!ret) // Are We Able To Set The Pixel Format?
	{
		return ERR_CANT_CREATE; // Return FALSE
	}

	HGLRC t_hRC = wglCreateContext(t_hDC);
	if (!t_hRC) // Are We Able To Get A Rendering Context?
	{
		return ERR_CANT_CREATE; // Return FALSE
	}

	wglMakeCurrent(t_hDC, t_hRC);

	if (false) {
		int attribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 3, //we want a 3.3 context
			WGL_CONTEXT_MINOR_VERSION_ARB, 3,
			//and it shall be forward compatible so that we can only use up to date functionality
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB /*| _WGL_CONTEXT_DEBUG_BIT_ARB*/,
			0
		}; //zero indicates the end of the array

		PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr; //pointer to the method
		wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

		if (wglCreateContextAttribsARB == nullptr) //OpenGL 3.0 is not supported
		{
			wglDeleteContext(t_hRC);
			return ERR_CANT_CREATE;
		}

		HGLRC new_hRC = wglCreateContextAttribsARB(t_hDC, 0, attribs);
		if (!new_hRC) {
			wglDeleteContext(t_hRC);
			return ERR_CANT_CREATE; // Return false
		}
		wglMakeCurrent(t_hDC, nullptr);
		wglDeleteContext(t_hRC);
		t_hRC = new_hRC;

		if (!wglMakeCurrent(t_hDC, t_hRC)) // Try To Activate The Rendering Context
		{
			return ERR_CANT_CREATE; // Return FALSE
		}
	}

	if (p_window_id >= (int) _windows.size()) {
		_windows.resize(p_window_id+1);
	}

	GLWindow &win = _windows[p_window_id];
	win.in_use = true;
	win.window_id = p_window_id;
	win.width = p_width;
	win.height = p_height;
	win.hWnd = p_window;
	win.gldisplay_id = _displays.size();

	GLDisplay d_temp;
	d_temp.hDC = t_hDC;
	d_temp.hRC = t_hRC;
	d_temp.pixel_format = t_pixel_format;
	_displays.push_back(d_temp);

	make_window_current(p_window_id);


	print_line("made_context");
return OK;
}


void ContextGL_Windows::window_resize(DisplayServer::WindowID p_window_id, int p_width, int p_height)
{

}

int ContextGL_Windows::window_get_width(DisplayServer::WindowID p_window)
{
	return get_window(p_window).width;	
}

int ContextGL_Windows::window_get_height(DisplayServer::WindowID p_window)
{
	return get_window(p_window).height;
}

void ContextGL_Windows::window_destroy(DisplayServer::WindowID p_window_id)
{
	GLWindow &win = get_window(p_window_id);
	win.in_use = false;
	
	if (_current_window == &win)
	{
		_current_window = nullptr;
		hDC = nullptr;
		hRC = nullptr;
	}
}

int ContextGL_Windows::get_window_width() {
	return DisplayServer::get_singleton()->window_get_size().x;
	//return OS::get_singleton()->get_video_mode().width;
}

int ContextGL_Windows::get_window_height() {
	//return OS::get_singleton()->get_video_mode().height;
	return DisplayServer::get_singleton()->window_get_size().y;
}

bool ContextGL_Windows::should_vsync_via_compositor() {
	if (DisplayServer::get_singleton()->window_get_mode() == DisplayServer::WINDOW_MODE_FULLSCREEN || !DisplayServer::get_singleton()->vsync_is_using_via_compositor()) {
		return false;
	}

	// Note: All Windows versions supported by Godot have a compositor.
	// It can be disabled on earlier Windows versions.
	BOOL dwm_enabled;

	if (SUCCEEDED(DwmIsCompositionEnabled(&dwm_enabled))) {
		return dwm_enabled;
	}

	return false;
}

void ContextGL_Windows::swap_buffers() {
	const GLDisplay &disp = get_current_display();
	SwapBuffers(disp.hDC);

		// new let's swap buffers on all windows
	//for (unsigned int n=0; n<_windows.size(); n++) {
	//	const GLWindow &win = _windows[n];
	//	if (!win.in_use)
	//		continue;
	//		
	//	const GLDisplay &disp = get_display(win.gldisplay_id);
	//	
	//	wglMakeCurrent(disp.hDC, disp.hRC);
	//	SwapBuffers(disp.hDC);
	//	
	//}

	//if (use_vsync) {
	//	bool vsync_via_compositor_now = should_vsync_via_compositor();
//
	//	if (vsync_via_compositor_now && wglGetSwapIntervalEXT() == 0) {
	//		DwmFlush();
	//	}
//
	//	if (vsync_via_compositor_now != vsync_via_compositor) {
	//		// The previous frame had a different operating mode than this
	//		// frame.  Set the 'vsync_via_compositor' member variable and the
	//		// OpenGL swap interval to their proper values.
	//		set_use_vsync(true);
	//	}
	//}
}

void ContextGL_Windows::set_use_vsync(bool p_use) {
	vsync_via_compositor = p_use && should_vsync_via_compositor();

	if (wglSwapIntervalEXT) {
		int swap_interval = (p_use && !vsync_via_compositor) ? 1 : 0;
		wglSwapIntervalEXT(swap_interval);
	}

	use_vsync = p_use;
}

bool ContextGL_Windows::is_using_vsync() const {
	return use_vsync;
}

#define _WGL_CONTEXT_DEBUG_BIT_ARB 0x0001

Error ContextGL_Windows::initialize() {
	
//	static PIXELFORMATDESCRIPTOR pfd = {
//		sizeof(PIXELFORMATDESCRIPTOR), // Size Of This Pixel Format Descriptor
//		1,
//		PFD_DRAW_TO_WINDOW | // Format Must Support Window
//				PFD_SUPPORT_OPENGL | // Format Must Support OpenGL
//				PFD_DOUBLEBUFFER,
//		(BYTE)PFD_TYPE_RGBA,
//		(BYTE)(OS::get_singleton()->is_layered_allowed() ? 32 : 24),
//		(BYTE)0, (BYTE)0, (BYTE)0, (BYTE)0, (BYTE)0, (BYTE)0, // Color Bits Ignored
//		(BYTE)(OS::get_singleton()->is_layered_allowed() ? 8 : 0), // Alpha Buffer
//		(BYTE)0, // Shift Bit Ignored
//		(BYTE)0, // No Accumulation Buffer
//		(BYTE)0, (BYTE)0, (BYTE)0, (BYTE)0, // Accumulation Bits Ignored
//		(BYTE)24, // 24Bit Z-Buffer (Depth Buffer)
//		(BYTE)0, // No Stencil Buffer
//		(BYTE)0, // No Auxiliary Buffer
//		(BYTE)PFD_MAIN_PLANE, // Main Drawing Layer
//		(BYTE)0, // Reserved
//		0, 0, 0 // Layer Masks Ignored
//	};
//
//	hDC = GetDC(hWnd);
//	if (!hDC) {
//		return ERR_CANT_CREATE; // Return FALSE
//	}
//
//	pixel_format = ChoosePixelFormat(hDC, &pfd);
//	if (!pixel_format) // Did Windows Find A Matching Pixel Format?
//	{
//		return ERR_CANT_CREATE; // Return FALSE
//	}
//
//	BOOL ret = SetPixelFormat(hDC, pixel_format, &pfd);
//	if (!ret) // Are We Able To Set The Pixel Format?
//	{
//		return ERR_CANT_CREATE; // Return FALSE
//	}
//
//	hRC = wglCreateContext(hDC);
//	if (!hRC) // Are We Able To Get A Rendering Context?
//	{
//		return ERR_CANT_CREATE; // Return FALSE
//	}
//
//	wglMakeCurrent(hDC, hRC);
//
//	if (opengl_3_context) {
//		int attribs[] = {
//			WGL_CONTEXT_MAJOR_VERSION_ARB, 3, //we want a 3.3 context
//			WGL_CONTEXT_MINOR_VERSION_ARB, 3,
//			//and it shall be forward compatible so that we can only use up to date functionality
//			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
//			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB /*| _WGL_CONTEXT_DEBUG_BIT_ARB*/,
//			0
//		}; //zero indicates the end of the array
//
//		PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr; //pointer to the method
//		wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
//
//		if (wglCreateContextAttribsARB == nullptr) //OpenGL 3.0 is not supported
//		{
//			wglDeleteContext(hRC);
//			return ERR_CANT_CREATE;
//		}
//
//		HGLRC new_hRC = wglCreateContextAttribsARB(hDC, 0, attribs);
//		if (!new_hRC) {
//			wglDeleteContext(hRC);
//			return ERR_CANT_CREATE; // Return false
//		}
//		wglMakeCurrent(hDC, nullptr);
//		wglDeleteContext(hRC);
//		hRC = new_hRC;
//
//		if (!wglMakeCurrent(hDC, hRC)) // Try To Activate The Rendering Context
//		{
//			return ERR_CANT_CREATE; // Return FALSE
//		}
//	}
//
	wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
	wglGetSwapIntervalEXT = (PFNWGLGETSWAPINTERVALEXTPROC)wglGetProcAddress("wglGetSwapIntervalEXT");
	//glWrapperInit(wrapper_get_proc_address);

	return OK;
}

ContextGL_Windows::ContextGL_Windows() {
	opengl_3_context = false;
	hWnd = nullptr;
	use_vsync = false;
	vsync_via_compositor = false;
}

ContextGL_Windows::~ContextGL_Windows() {
}

#endif
