#include "stdafx.h"
#include "Mesh_Impl.hpp"

namespace Graphics
{
	// Desktop GL3: Draw() leaves the VAO bound so a following Redraw() of the same
	// mesh can skip re-binding it.
	void Mesh_Impl::Draw()
	{
		glBindVertexArray(m_vao);
		glDrawArrays(m_glType, 0, (int)m_vertexCount);
	}
	void Mesh_Impl::Redraw()
	{
		glDrawArrays(m_glType, 0, (int)m_vertexCount);
	}
}
