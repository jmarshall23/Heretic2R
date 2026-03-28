//
// gl1_SDL.c
//
// Copyright 1998 Raven Software
//

#include "gl_SDL.h"
#include "gl_Local.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static SDL_Window* window = NULL;
static HDC         windowDC = NULL;
static HGLRC       context = NULL;

// Swaps the buffers and shows the next frame.
void RI_EndFrame(void) // mxd. GLimp_EndFrame in original logic.
{
	if (windowDC != NULL)
	{
		SwapBuffers(windowDC);
	}
}

// This function returns the flags used at the SDL window creation by GLimp_InitGraphics().
// In case of error -1 is returned.
int RI_PrepareForWindow(void)
{
	// No SDL_GL usage anymore.
	// We only need a normal window; the D3D12 shim handles the WGL/OpenGL side.
	return 0;
}

// Enables or disables the vsync.
void R_SetVsync(void)
{
	int vsync = 0;

	if (r_vsync->value == 1.0f)
		vsync = 1;
	else if (r_vsync->value == 2.0f)
		vsync = -1;

	if (!wglSwapIntervalEXT(vsync) && vsync == -1)
	{
		ri.Con_Printf(PRINT_ALL, "Failed to set adaptive VSync, reverting to normal VSync.\n");
		wglSwapIntervalEXT(1);
	}

	// TODO: update r_vsync cvar if desired
}

// Initializes the rendering context through the D3D12 WGL shim.
qboolean RI_InitContext(void* win)
{
#ifdef _WIN32
	SDL_PropertiesID props;
	HWND hwnd = NULL;

	if (win == NULL)
	{
		ri.Sys_Error(ERR_FATAL, "RI_InitContext() called with NULL argument!");
		return false;
	}

	window = (SDL_Window*)win;

	// SDL3 exposes the native HWND/HDC through window properties on Windows.
	props = SDL_GetWindowProperties(window);
	if (props == 0)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): SDL_GetWindowProperties() failed: %s\n", SDL_GetError());
		window = NULL;
		return false;
	}

	hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
	if (hwnd == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): failed to get Win32 HWND from SDL window.\n");
		window = NULL;
		return false;
	}

	windowDC = GetDC(hwnd);
	if (windowDC == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): GetDC() failed.\n");
		window = NULL;
		return false;
	}

	context = (HGLRC)wglCreateContext(windowDC);
	if (context == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): failed to create rendering context.\n");
		ReleaseDC(hwnd, windowDC);
		windowDC = NULL;
		window = NULL;
		return false;
	}

	if (!wglMakeCurrent(windowDC, context))
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): wglMakeCurrent() failed.\n");
		wglDeleteContext(context);
		context = NULL;
		ReleaseDC(hwnd, windowDC);
		windowDC = NULL;
		window = NULL;
		return false;
	}

	R_SetVsync();
	vid_gamma->modified = true; // Force R_UpdateGamma() call in R_BeginFrame().

	return true;
#else
	ri.Con_Printf(PRINT_ALL, "RI_InitContext(): D3D12 shim path is only implemented on Windows.\n");
	return false;
#endif
}

// Shuts the rendering context down.
void RI_ShutdownContext(void)
{
#ifdef _WIN32
	if (context != NULL)
	{
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(context);
		context = NULL;
	}

	if (window != NULL && windowDC != NULL)
	{
		SDL_PropertiesID props = SDL_GetWindowProperties(window);
		HWND hwnd = NULL;

		if (props != 0)
			hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

		if (hwnd != NULL)
			ReleaseDC(hwnd, windowDC);

		windowDC = NULL;
	}

	window = NULL;
#endif
}