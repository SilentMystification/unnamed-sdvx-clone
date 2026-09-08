#include "stdafx.h"
#include "Mesh_Impl.hpp"
#include "RenderBackendHooks.hpp"
#include <Graphics/ResourceManagers.hpp>

namespace Graphics
{
	uint32 primitiveTypeMap[] =
	{
		GL_TRIANGLES,
		GL_TRIANGLE_STRIP,
		GL_TRIANGLE_FAN,
		GL_LINES,
		GL_LINE_STRIP,
		GL_POINTS,
	};

#ifndef USC_GL1_LEGACY
	// GL3/GLES3: generic indexed vertex attributes (glVertexAttribPointer) + VAO.
	// GL1_LEGACY has neither (no VAOs, no generic attribute indices - only the fixed
	// position/texcoord/color/etc. client-array slots) so it provides its own Init()/
	// SetData()/destructor entirely; see Mesh_GL1_LEGACY.cpp.
	Mesh_Impl::~Mesh_Impl()
	{
		if(m_buffer)
			glDeleteBuffers(1, &m_buffer);
		if(m_vao)
			glDeleteVertexArrays(1, &m_vao);
	}

	bool Mesh_Impl::Init()
	{
		glGenBuffers(1, &m_buffer);
		glGenVertexArrays(1, &m_vao);
		return m_buffer != 0 && m_vao != 0;
	}

	void Mesh_Impl::SetData(const void* pData, size_t vertexCount, const VertexFormatList& desc)
	{
		glBindVertexArray(m_vao);
		glBindBuffer(GL_ARRAY_BUFFER, m_buffer);

		m_vertexCount = vertexCount;
		size_t totalVertexSize = 0;
		for(auto e : desc)
			totalVertexSize += e.componentSize * e.components;
		size_t index = 0;
		size_t offset = 0;
		for(auto e : desc)
		{
			uint32 type = -1;
			if(!e.isFloat)
			{
				if(e.componentSize == 4)
					type = e.isSigned ? GL_INT : GL_UNSIGNED_INT;
				else if(e.componentSize == 2)
					type = e.isSigned ? GL_SHORT : GL_UNSIGNED_SHORT;
				else if(e.componentSize == 1)
					type = e.isSigned ? GL_BYTE : GL_UNSIGNED_BYTE;
			}
			else
			{
				type = RenderBackendHooks::FloatAttribType(e.componentSize);
			}
			assert(type != (uint32)-1);
			glVertexAttribPointer((int)index, (int)e.components, type, GL_TRUE, (int)totalVertexSize, (void*)offset);
			glEnableVertexAttribArray((int)index);
			offset += e.componentSize * e.components;
			index++;
		}
		glBufferData(GL_ARRAY_BUFFER, totalVertexSize * vertexCount, pData, m_bDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
#endif

	Mesh MeshRes::Create(class OpenGL* gl)
	{
		Mesh_Impl* pImpl = new Mesh_Impl();
		if(!pImpl->Init())
		{
			delete pImpl;
			return Mesh();
		}
		else
		{
			return GetResourceManager<ResourceType::Mesh>().Register(pImpl);
		}
	}
}
