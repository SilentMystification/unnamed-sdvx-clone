/*
	OpenGL include file for the GL1_LEGACY (fixed-function) render backend.
	Targets macOS 10.4 Tiger / PowerPC, where OpenGL/gl3.h does not exist and
	no GPU in the supported hardware range (GeForce2/4MX, Rage128, Radeon 9200)
	implements GLSL or core-profile GL.
*/
#pragma once

#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
