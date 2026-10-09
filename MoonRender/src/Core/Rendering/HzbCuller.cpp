#include <Core/Rendering/HzbCuller.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>

namespace
{
	// Below this clip-space w the corner is behind (or on) the near plane; such
	// nodes are never culled to stay conservative.
	constexpr float kMinClipW = 1e-5f;

	// Absolute floor for the relative depth bias, so nodes at extremely small
	// window depths (very far away) still get a usable epsilon.
	constexpr float kMinDepthBias = 1e-9f;
}

namespace Core::Rendering
{
	void HzbCuller::SetGrid(uint32_t p_width, uint32_t p_height, std::vector<float> p_depths)
	{
		if (p_width == 0 || p_height == 0 || p_depths.size() != static_cast<size_t>(p_width) * p_height)
		{
			ClearGrid();
			return;
		}
		m_width = p_width;
		m_height = p_height;
		m_depths = std::move(p_depths);
		m_stats.gridWidth = p_width;
		m_stats.gridHeight = p_height;

		// Grid diagnostics: an all-zero grid (reversed-Z far/background) means the
		// depth resolve or the pyramid reduce produced nothing usable, in which
		// case nothing can ever be culled.
		float minDepth = 1.0f;
		float maxDepth = 0.0f;
		double sum = 0.0;
		for (const float depth : m_depths)
		{
			minDepth = std::min(minDepth, depth);
			maxDepth = std::max(maxDepth, depth);
			sum += depth;
		}
		m_stats.gridMinDepth = minDepth;
		m_stats.gridMaxDepth = maxDepth;
		m_stats.gridMeanDepth = static_cast<float>(
			sum / static_cast<double>(m_depths.size())
		);
	}

	void HzbCuller::ClearGrid()
	{
		m_depths.clear();
		m_width = 0;
		m_height = 0;
		m_stats.gridWidth = 0;
		m_stats.gridHeight = 0;
		m_occludedInstances.clear();
	}

	bool HzbCuller::IsOccluded(
		const ::Rendering::Resources::Mesh* p_mesh,
		int64_t p_actorID
	) const
	{
		if (p_mesh == nullptr)
		{
			return false;
		}
		const InstanceKey key{ p_mesh, p_actorID };
		return m_occludedInstances.find(key) != m_occludedInstances.end();
	}

	void HzbCuller::Cull(
		const ::Core::SceneSystem::BvhService& p_bvhService,
		const Maths::FMatrix4& p_viewProjection,
		uint32_t p_viewportWidth,
		uint32_t p_viewportHeight
	)
	{
		m_stats.visitedNodes = 0;
		m_stats.culledNodes = 0;
		m_stats.occludedInstances = 0;
		m_stats.occludedNodeTests = 0;
		m_stats.biasRejectedNodes = 0;
		m_stats.backgroundRejectedNodes = 0;
		m_stats.bestMargin = 0.0f;
		m_occludedInstances.clear();

		if (!HasGrid())
		{
			return;
		}

		const auto* bvh = p_bvhService.m_sceneBvh;
		if (bvh == nullptr || bvh->m_root == nullptr)
		{
			return;
		}

		const auto& instances = p_bvhService.mSceneMeshInstances;
		const auto& meshes = p_bvhService.mSceneMeshes;
		m_stats.bvhInstances = static_cast<uint32_t>(instances.size());

		// A projection that is not finite cannot be tested against anything: the corner
		// transforms come out NaN, every comparison against them is false, and the tile
		// scan below reads that as "behind the occluder" for the whole scene. Nothing is
		// marked occluded instead, so a frame with a broken camera still draws.
		for (const float value : p_viewProjection.data)
		{
			if (!std::isfinite(value))
			{
				return;
			}
		}

		m_viewProjection = p_viewProjection;
		m_viewportWidth = std::max(1u, p_viewportWidth);
		m_viewportHeight = std::max(1u, p_viewportHeight);

		// The grid was built with the previous frame's camera. While the camera
		// is static that grid is still exact, so the bias can drop to a tiny
		// epsilon; only real motion needs a safety margin.
		if (m_hasLastCullViewProjection)
		{
			float maxDelta = 0.0f;
			for (int i = 0; i < 16; ++i)
			{
				maxDelta = std::max(
					maxDelta,
					std::fabs(p_viewProjection.data[i] - m_lastCullViewProjection.data[i])
				);
			}
			m_effectiveDepthBias = maxDelta < 1e-6f ? m_staticDepthBias : m_depthBias;
		}
		else
		{
			m_effectiveDepthBias = m_depthBias;
		}
		m_lastCullViewProjection = p_viewProjection;
		m_hasLastCullViewProjection = true;
		m_stats.effectiveBias = m_effectiveDepthBias;

		const auto startTime = std::chrono::high_resolution_clock::now();

		const int* indices = bvh->GetIndices();
		const int instanceCount = static_cast<int>(instances.size());

		// Marks every mesh instance stored under a leaf node.
		const auto markLeaf = [&](const ::Rendering::Geometry::Bvh::Node& p_leaf)
		{
			if (indices == nullptr)
			{
				return;
			}
			for (int i = 0; i < p_leaf.numprims; ++i)
			{
				const int prim = indices[p_leaf.startidx + i];
				if (prim < 0 || prim >= instanceCount)
				{
					continue;
				}
				const int meshID = instances[prim].meshID;
				if (meshID >= 0 && meshID < static_cast<int>(meshes.size()))
				{
					m_occludedInstances.insert(InstanceKey{
						meshes[meshID],
						instances[prim].actorID
					});
				}
			}
		};

		// A pruned internal node hides everything below it, so its whole
		// subtree has to be collected, not only the leaves that were tested.
		const auto markSubtree = [&](const ::Rendering::Geometry::Bvh::Node* p_root)
		{
			m_subtreeStack.clear();
			m_subtreeStack.push_back(p_root);
			while (!m_subtreeStack.empty())
			{
				const auto* current = m_subtreeStack.back();
				m_subtreeStack.pop_back();
				if (current == nullptr)
				{
					continue;
				}
				if (current->type == ::Rendering::Geometry::Bvh::kLeaf)
				{
					markLeaf(*current);
					continue;
				}
				m_subtreeStack.push_back(current->lc);
				m_subtreeStack.push_back(current->rc);
			}
		};

		// At most every instance can end up occluded, so size the hash set once.
		m_occludedInstances.reserve(instances.size());

		m_traversalStack.clear();
		m_traversalStack.push_back(bvh->m_root);

		while (!m_traversalStack.empty())
		{
			const auto* node = m_traversalStack.back();
			m_traversalStack.pop_back();

			if (node == nullptr)
			{
				continue;
			}

			++m_stats.visitedNodes;

			float closestDepth = 0.0f;
			float occluderDepth = 0.0f;
			const auto testResult = TestNode(*node, closestDepth, occluderDepth);
			if (occluderDepth > 0.0f)
			{
				m_stats.bestMargin = std::max(
					m_stats.bestMargin,
					occluderDepth - closestDepth
				);
			}

			if (testResult == ENodeTestResult::Occluded)
			{
				// Whole subtree is behind the occluder depth: prune it and mark
				// every mesh instance inside as hidden. Culling granularity is
				// the mesh instance, which is also the drawable granularity
				// (ParseScene emits one drawable per mesh/sub-range).
				++m_stats.culledNodes;
				++m_stats.occludedNodeTests;
				markSubtree(node);
				continue;
			}

			if (testResult == ENodeTestResult::BiasRejected)
			{
				++m_stats.biasRejectedNodes;
			}
			else if (testResult == ENodeTestResult::Background)
			{
				++m_stats.backgroundRejectedNodes;
			}

			if (node->type == ::Rendering::Geometry::Bvh::kLeaf)
			{
				continue;
			}

			m_traversalStack.push_back(node->lc);
			m_traversalStack.push_back(node->rc);
		}

		m_stats.occludedInstances = static_cast<uint32_t>(m_occludedInstances.size());
		m_stats.cullTimeMs = std::chrono::duration<float, std::milli>(
			std::chrono::high_resolution_clock::now() - startTime
		).count();
	}

	HzbCuller::ENodeTestResult HzbCuller::TestNode(
		const ::Rendering::Geometry::Bvh::Node& p_node,
		float& p_closestDepth,
		float& p_occluderDepth
	) const
	{
		p_closestDepth = 0.0f;
		p_occluderDepth = 0.0f;

		if (!HasGrid())
		{
			return ENodeTestResult::NoGrid;
		}

		// Reversed-Z: window depth 1 = near, 0 = far/background. The object's
		// closest point is therefore the LARGEST value over its corners.
		float closestDepth = -FLT_MAX;
		float uMin = FLT_MAX;
		float vMin = FLT_MAX;
		float uMax = -FLT_MAX;
		float vMax = -FLT_MAX;

		for (int corner = 0; corner < 8; ++corner)
		{
			const float x = (corner & 1) ? p_node.bounds.pmax.x : p_node.bounds.pmin.x;
			const float y = (corner & 2) ? p_node.bounds.pmax.y : p_node.bounds.pmin.y;
			const float z = (corner & 4) ? p_node.bounds.pmax.z : p_node.bounds.pmin.z;

			const Maths::FVector4 clip = m_viewProjection * Maths::FVector4(x, y, z, 1.0f);
			if (clip.w <= kMinClipW)
			{
				// Straddles the near plane (or behind the eye): keep it.
				return ENodeTestResult::NearClip;
			}

			const float invW = 1.0f / clip.w;
			const float ndcX = clip.x * invW;
			const float ndcY = clip.y * invW;
			// glDepthRange(1, 0) flips the standard NDC->window mapping:
			// near (-1) -> 1, far (+1) -> 0.
			const float depth = 0.5f - clip.z * invW * 0.5f;

			closestDepth = std::max(closestDepth, depth);

			const float u = ndcX * 0.5f + 0.5f;
			const float v = ndcY * 0.5f + 0.5f;
			uMin = std::min(uMin, u);
			uMax = std::max(uMax, u);
			vMin = std::min(vMin, v);
			vMax = std::max(vMax, v);
		}

		if (closestDepth <= 0.0f || closestDepth >= 1.0f)
		{
			// On (or beyond) the near/far plane: keep it.
			return ENodeTestResult::OutOfRange;
		}

		if (uMax < 0.0f || uMin > 1.0f || vMax < 0.0f || vMin > 1.0f)
		{
			// Fully off screen; frustum culling handles this case.
			return ENodeTestResult::Offscreen;
		}

		uMin = std::clamp(uMin, 0.0f, 1.0f);
		uMax = std::clamp(uMax, 0.0f, 1.0f);
		vMin = std::clamp(vMin, 0.0f, 1.0f);
		vMax = std::clamp(vMax, 0.0f, 1.0f);

		int x0 = static_cast<int>(std::floor(uMin * m_width));
		int x1 = static_cast<int>(std::ceil(uMax * m_width)) - 1;
		int y0 = static_cast<int>(std::floor(vMin * m_height));
		int y1 = static_cast<int>(std::ceil(vMax * m_height)) - 1;

		x0 = std::clamp(x0, 0, static_cast<int>(m_width) - 1);
		x1 = std::clamp(x1, 0, static_cast<int>(m_width) - 1);
		y0 = std::clamp(y0, 0, static_cast<int>(m_height) - 1);
		y1 = std::clamp(y1, 0, static_cast<int>(m_height) - 1);

		const float bias = std::max(m_effectiveDepthBias * closestDepth, kMinDepthBias);

		// Conservative occluder depth: the farthest surface inside the covered
		// tile range, which in reversed-Z is the smallest value. The node is
		// occluded only if even that surface is closer (larger value) than the
		// node's closest point, so the scan can stop at the first tile that
		// fails the test instead of always reducing the whole rectangle.
		const float required = closestDepth + bias;
		float occluderDepth = 1.0f;
		for (int y = y0; y <= y1; ++y)
		{
			const size_t row = static_cast<size_t>(y) * m_width;
			for (int x = x0; x <= x1; ++x)
			{
				const float depth = m_depths[row + x];
				if (depth <= 0.0f)
				{
					// The tile still contains background, so no surface there
					// can occlude the node.
					p_closestDepth = closestDepth;
					p_occluderDepth = 0.0f;
					return ENodeTestResult::Background;
				}
				if (depth <= closestDepth)
				{
					p_closestDepth = closestDepth;
					p_occluderDepth = depth;
					return ENodeTestResult::Visible;
				}
				if (depth <= required)
				{
					// Behind the occluder, but the margin is inside the bias.
					p_closestDepth = closestDepth;
					p_occluderDepth = depth;
					return ENodeTestResult::BiasRejected;
				}
				occluderDepth = std::min(occluderDepth, depth);
			}
		}

		p_closestDepth = closestDepth;
		p_occluderDepth = occluderDepth;
		return ENodeTestResult::Occluded;
	}
}
