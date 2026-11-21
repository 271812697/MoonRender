#pragma once
#include <memory>
#include <vector>
#include <Rendering/Geometry/Vertex.h>
#include <Rendering/Geometry/BoundingSphere.h>
#include <Rendering/Geometry/bbox.h>
#include <Rendering/Geometry/bvh.h>
#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/Resources/IMesh.h>
#include <Rendering/Settings/EPrimitiveMode.h>

namespace Rendering::Backend
{
	struct HwBufferObject;
	struct HwVertexArray;
}

namespace Rendering::Resources
{

	class Mesh : public IMesh
	{
	public:
		Mesh(
			const std::vector<Geometry::Vertex>& p_vertices,
			const std::vector<uint32_t>& p_indices,
			uint32_t p_materialIndex = 0,
			::Rendering::Settings::EPrimitiveMode primitiveMode = Settings::EPrimitiveMode::TRIANGLES
		);
		Mesh(
			const std::vector<Geometry::VertexPositionNormal>& p_vertices,
			const std::vector<uint32_t>& p_indices,
			uint32_t p_materialIndex = 0,
			::Rendering::Settings::EPrimitiveMode primitiveMode = Settings::EPrimitiveMode::TRIANGLES
		);
		Mesh(
			const std::vector<Geometry::VertexBVH>& p_vertices,
			const std::vector<uint32_t>& p_indices,
			uint32_t p_materialIndex = 0,
			::Rendering::Settings::EPrimitiveMode primitiveMode = Settings::EPrimitiveMode::TRIANGLES
		);
		~Mesh();
		virtual void Bind(int index=0) const override;
		virtual void Unbind() const override;
		virtual uint32_t GetVertexCount() const override;
		virtual uint32_t GetIndexCount(int index=0) const override;
		/** The GL name of the array object the draw path binds for sub-mesh p_index, resolved from
		 * the handle on the side that owns the context. */
		virtual uint32_t GetVertexArrayId(int p_index = 0) const override;
		/** Bytes between two vertices of the GPU vertex buffer.
		 *
		 * The three vertex layouts this class accepts are different sizes (a plain
		 * Vertex is 14 floats, a VertexPositionNormal 6, a VertexBVH 10), so
		 * anything that reads the buffer by hand - the point pass instancing a
		 * sprite per vertex - has to ask for the stride instead of assuming one. */
		uint32_t GetVertexStride() const { return m_vertexStride; }

		/** The resources this mesh owns, as handles (Filament's VertexBuffer/IndexBuffer/
		 * VertexArray handles): the GL objects behind them belong to the side that owns the
		 * context and are reached with Backend::Records or HAL::ResourceOps. */
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& GetVertexArrayHandle(int p_index = 0) const;
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& GetVertexBufferHandle() const { return m_vertexBuffer; }
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& GetIndexBufferHandle(int p_index = 0) const;

		virtual const Rendering::Geometry::BoundingSphere& GetBoundingSphere()  override;
		virtual const Rendering::Geometry::bbox& GetBoundingBox() override;
		/** Returned by reference: these lists are read every frame while parsing
		 * the scene, and returning by value used to allocate two vectors per
		 * mesh per frame. */
		const std::vector<uint32_t>& GetMaterialIndex() const;
		const std::vector<uint32_t>& GetSubRangeBufferIndex() const;
		void AddMaterial(int materialIndex,int subRangeIndex);
		Maths::FVector3 GetVertexPosition(int index);
		Geometry::VertexBVH GetVertexBVH(int index);
		std::vector<Geometry::VertexBVH>& GetVerticesBVH();
		std::vector<uint32_t>& GetIndices();
		Settings::EPrimitiveMode GetPrimitiveMode() const;
		void SetPrimitiveMode(Settings::EPrimitiveMode mode) { mPrimitiveMode = mode; }
		void BuildBvh();
		Geometry::Bvh* GetBvh();
		void UploadIndices(const std::vector<std::pair<int,int>>&childListt,int index=0);
		void UploadIndices(const std::vector<uint32_t>& p_indices, int index = 0);
		void AddSubRangeBuffer();
		void ComputeBoundingSphereAndBox();
	private:
		/** Allocates the handles of the mesh's GPU resources and has their objects made (recorded
		 * when the context lives on another thread - see HAL::ResourceOps). */
		void CreateResources();

		/** The GPU half of the mesh: fills the buffer named by the handle, uploads the indices and
		 * applies the layout. Everything it does is a command where the context lives on another
		 * thread (see HAL::ResourceOps), so it is safe to call from the loader thread. */
		void Upload(const std::vector<Geometry::Vertex>& p_vertices, const std::vector<uint32_t>& p_indices);
		void Upload(const std::vector<Geometry::VertexPositionNormal>&p_vertices, const std::vector<uint32_t>& p_indices);
		void Upload(const std::vector<Geometry::VertexBVH>& p_vertices, const std::vector<uint32_t>& p_indices);
		void ComputeBoundingSphereAndBox(const std::vector<Geometry::Vertex>& p_vertices);
		void ComputeBoundingSphereAndBox(const std::vector<Geometry::VertexPositionNormal>& p_vertices);
		void ComputeBoundingSphereAndBox(const std::vector<Geometry::VertexBVH>& p_vertices);

	private:
		::Rendering::Settings::EPrimitiveMode mPrimitiveMode = ::Rendering::Settings::EPrimitiveMode::TRIANGLES;
		const uint32_t m_vertexCount;
		const uint32_t m_indicesCount;
		/** Set by whichever Upload() overload filled the vertex buffer. */
		uint32_t m_vertexStride = 0;
		std::vector<uint32_t> uploadIndicesCount;

		//to sure which subRange to which material
		std::vector<uint32_t> m_materialIndex;
		std::vector<uint32_t> m_subRangeIndex;

		/** The mesh's GPU resources, as handles (see Backend::HandleAllocator). */
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject> m_vertexBuffer;
		std::vector<Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>> m_IndexBuffers;
		std::vector<Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>> m_vertexArrays;
		bool isIndex = false;
		std::vector<Geometry::VertexBVH> m_vertices;
		std::vector<uint32_t> m_indices;
		Geometry::Bvh* m_bvh=nullptr;
		Geometry::bbox m_boundingBox;
		Geometry::BoundingSphere m_boundingSphere;
	};
}
