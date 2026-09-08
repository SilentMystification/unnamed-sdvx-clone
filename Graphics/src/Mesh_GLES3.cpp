#include "stdafx.h"
#include "Mesh_Impl.hpp"

namespace Graphics
{
	// Embedded: always explicitly bind/unbind the VAO around each draw call.
	void Mesh_Impl::Draw()
	{
		glBindVertexArray(m_vao);
		glDrawArrays(m_glType, 0, (int)m_vertexCount);
		glBindVertexArray(0);
	}
	void Mesh_Impl::Redraw()
	{
		glBindVertexArray(m_vao);
		glDrawArrays(m_glType, 0, (int)m_vertexCount);
		glBindVertexArray(0);
	}
}
