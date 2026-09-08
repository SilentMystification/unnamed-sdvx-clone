/*
	OpenGL include file for the GL3 (desktop core-profile) render backend.
	Used on Windows, Linux and macOS-Intel/Apple Silicon.
*/
#pragma once

#ifdef _WIN32
#include <GL/glew.h>
#include <GL/wglew.h>
#elif __APPLE__
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#elif __linux
#include <GL/glew.h>
#include <GL/glxew.h>
#endif
