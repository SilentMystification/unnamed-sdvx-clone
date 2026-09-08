/*
	Fixed-function has no generic indexed vertex attributes (glVertexAttribPointer is a
	GL2.0/ARB_vertex_program concept) and no VAOs - only the fixed client-array slots
	(glVertexPointer/glTexCoordPointer/glColorPointer/...), each bound to one semantic.

	SetData() interprets just the first two VertexFormatList entries as (position,
	texcoord) - the layout used by every sprite/UI/font mesh in this engine (SimpleVertex,
	TextVertex - see MeshGenerators.hpp/Font.cpp). Any further attributes (e.g.
	ParticleVertex's two extra Vector4s, meant for a geometry shader GL1 has no equivalent
	for) are dropped - particles render as flat-textured sprites instead, an accepted
	fidelity loss alongside the rest of the GL1_LEGACY backend (see Material_GL1_LEGACY.cpp and
	humming-sleeping-stonebraker.md).

	Vertex color comes entirely from Material_GL1's glColor4f (a constant for the whole
	draw call, set from the "color" MaterialParameter) - there is no glColorPointer here,
	matching how none of the engine's vertex formats actually carry a per-vertex color.
*/
#include "stdafx.h"
#include "Mesh_Impl.hpp"
#include "RenderBackendHooks.hpp"

namespace Graphics
{
	Mesh_Impl::~Mesh_Impl()
	{
		// GLuint is `unsigned long` on this target's classic GL headers, not `unsigned
		// int` like m_buffer (uint32) - same size, different type name.
		if(m_buffer)
			glDeleteBuffers(1, (GLuint*)&m_buffer);
	}

	bool Mesh_Impl::Init()
	{
		glGenBuffers(1, (GLuint*)&m_buffer);
		return m_buffer != 0;
	}

	void Mesh_Impl::SetData(const void* pData, size_t vertexCount, const VertexFormatList& desc)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_buffer);

		m_vertexCount = vertexCount;
		size_t totalVertexSize = 0;
		for(auto e : desc)
			totalVertexSize += e.componentSize * e.components;
		m_stride = (uint32)totalVertexSize;

		m_hasTexCoord = false;
		size_t offset = 0;
		for(size_t i = 0; i < desc.size(); i++)
		{
			const VertexFormatDesc& e = desc[i];
			uint32 type = e.isFloat ? RenderBackendHooks::FloatAttribType(e.componentSize)
				: (e.componentSize == 4 ? (e.isSigned ? GL_INT : GL_UNSIGNED_INT)
				: (e.componentSize == 2 ? (e.isSigned ? GL_SHORT : GL_UNSIGNED_SHORT)
				: (e.isSigned ? GL_BYTE : GL_UNSIGNED_BYTE)));

			if(i == 0)
			{
				m_posOffset = (uint32)offset;
				m_posComponents = e.components;
				m_posType = type;
			}
			else if(i == 1)
			{
				m_texOffset = (uint32)offset;
				m_texComponents = e.components;
				m_texType = type;
				m_hasTexCoord = true;
			}
			// Further attributes (i >= 2) have no fixed-function client-array slot they can
			// generically map to here and are dropped - see file header comment.

			offset += e.componentSize * e.components;
		}

		glBufferData(GL_ARRAY_BUFFER, totalVertexSize * vertexCount, pData, m_bDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	static void BindClientArrays(Mesh_Impl& mesh)
	{
		// Defensive re-enable, not just re-point: GL_VERTEX_ARRAY is meant to stay on for
		// the whole context lifetime (see OpenGL_GL1_LEGACY.cpp's Init()), but nuklear_gl1.h
		// disables all its client arrays (including this one) after each of its own render
		// passes to restore its caller's state - without this, every mesh drawn after the
		// first nuklear UI render would silently submit zero vertices from then on.
		glEnableClientState(GL_VERTEX_ARRAY);
		glBindBuffer(GL_ARRAY_BUFFER, mesh.m_buffer);
		glVertexPointer((int)mesh.m_posComponents, mesh.m_posType, (int)mesh.m_stride, (void*)(size_t)mesh.m_posOffset);
		if(mesh.m_hasTexCoord)
		{
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glTexCoordPointer((int)mesh.m_texComponents, mesh.m_texType, (int)mesh.m_stride, (void*)(size_t)mesh.m_texOffset);
		}
		else
		{
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
	}

	void Mesh_Impl::Draw()
	{
		BindClientArrays(*this);
		glDrawArrays(m_glType, 0, (int)m_vertexCount);
		// Leaving this mesh's VBO bound to GL_ARRAY_BUFFER after we're done corrupts the
		// next thing to call glVertexPointer/glTexCoordPointer/glColorPointer with a real
		// CPU pointer instead of a buffer offset (nuklear_gl1.h does exactly this) - a
		// bound nonzero GL_ARRAY_BUFFER makes GL reinterpret that pointer as a byte offset
		// into THIS buffer instead of a memory address, producing garbage/degenerate
		// geometry. This was the real cause of "renders once, then permanently black with
		// a couple stray white pixels" - whichever ran first this frame determined whether
		// nuklear's next draw call got real pointers or garbage offsets.
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
	void Mesh_Impl::Redraw()
	{
		// No VAO to leave bound between draws on this backend - just re-issue the
		// (cheap, client-side state only) pointer setup each time.
		BindClientArrays(*this);
		glDrawArrays(m_glType, 0, (int)m_vertexCount);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
}
