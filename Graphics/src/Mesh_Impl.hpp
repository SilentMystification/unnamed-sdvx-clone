#pragma once
#include "Mesh.hpp"

namespace Graphics
{
	extern uint32 primitiveTypeMap[];

	class Mesh_Impl : public MeshRes
	{
	public:
		uint32 m_buffer = 0;
		uint32 m_vao = 0; // GL3/GLES3 only - see Mesh.cpp
		PrimitiveType m_type;
		uint32 m_glType;
		size_t m_vertexCount;
		bool m_bDynamic = true;

		// GL1_LEGACY only (see Mesh_GL1_LEGACY.cpp): fixed-function has no generic indexed vertex
		// attributes, so SetData() there remembers just the first two VertexFormatList
		// entries as (position, texcoord) for glVertexPointer/glTexCoordPointer - any
		// further attributes (e.g. ParticleVertex's extra per-vertex data, consumed by a
		// geometry shader GL1 has no equivalent for) are dropped, an accepted fidelity loss.
		uint32 m_stride = 0;
		uint32 m_posOffset = 0, m_posComponents = 0, m_posType = 0;
		bool m_hasTexCoord = false;
		uint32 m_texOffset = 0, m_texComponents = 0, m_texType = 0;

		Mesh_Impl() = default;
		~Mesh_Impl();

		// Backend-specific: see Mesh.cpp (GL3/GLES3, guarded) / Mesh_GL1_LEGACY.cpp
		bool Init();
		void SetData(const void* pData, size_t vertexCount, const VertexFormatList& desc) override;
		void Draw() override;
		void Redraw() override;

		void SetPrimitiveType(PrimitiveType pt) override
		{
			m_type = pt;
			m_glType = primitiveTypeMap[(size_t)pt];
		}
		PrimitiveType GetPrimitiveType() const override
		{
			return m_type;
		}
	};
}
