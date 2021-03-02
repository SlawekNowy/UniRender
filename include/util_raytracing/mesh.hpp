/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
*/

#ifndef __PR_CYCLES_MESH_HPP__
#define __PR_CYCLES_MESH_HPP__

#include "definitions.hpp"
#include "scene_object.hpp"
#include "shader_nodes.hpp"
#include <memory>
#include <optional>
#include <mathutil/uvec.h>
#include <sharedutils/util.h>

namespace udm {struct LinkedPropertyWrapper;};
class DataStream;
namespace unirender
{
	struct VertexWeight
	{
		std::array<int32_t,8> boneIndices {-1};
		std::array<float,8> boneWeights {0.f};
	};
	using BoneTransforms = std::vector<umath::ScaledTransform>;
	struct DLLRTUTIL MeshData
		: public std::enable_shared_from_this<MeshData>
	{
		void Reserve(size_t n)
		{

		}
		void Resize(size_t n)
		{

		}
		std::vector<Vector3> positions;
		std::vector<Vector3> normals;
		std::vector<Vector2> uvs;
		std::vector<Vector2> lightmapUvs;
		std::vector<Vector4> tangents;
		std::vector<int32_t> indices;
		std::vector<float> alphas;
		std::vector<float> wrinkles;
		std::vector<VertexWeight> vertexWeights;

		unirender::PShader shader = nullptr;
	};

	class Shader;
	class Scene;
	class Mesh;
	class ShaderCache;
	using PMesh = std::shared_ptr<Mesh>;
	using PShader = std::shared_ptr<Shader>;
	class DLLRTUTIL Mesh
		: public BaseObject,
		public std::enable_shared_from_this<Mesh>
	{
	public:
		enum class Flags : uint8_t
		{
			None = 0u,
			HasAlphas = 1u,
			HasWrinkles = HasAlphas<<1u
		};
		static const std::string TANGENT_POSTFIX;
		static const std::string TANGENT_SIGN_POSTIFX;
		using Smooth = uint8_t; // Boolean value

		static PMesh Create(const std::string &name,uint64_t numVerts,uint64_t numTris,Flags flags=Flags::None);
		static PMesh Create(udm::LinkedPropertyWrapper &prop,const std::function<PShader(uint32_t)> &fGetShader);
		static PMesh Create(udm::LinkedPropertyWrapper &prop,const ShaderCache &cache);
		util::WeakHandle<Mesh> GetHandle();

		void Serialize(udm::LinkedPropertyWrapper &prop,const std::function<std::optional<uint32_t>(const Shader&)> &fGetShaderIndex) const;
		void Serialize(udm::LinkedPropertyWrapper &prop,const std::unordered_map<const Shader*,size_t> shaderToIndexTable) const;
		void Deserialize(udm::LinkedPropertyWrapper &prop,const std::function<PShader(uint32_t)> &fGetShader);

		void Merge(const Mesh &other);

		const std::vector<PShader> &GetSubMeshShaders() const;
		std::vector<PShader> &GetSubMeshShaders();
		void SetLightmapUVs(std::vector<Vector2> &&lightmapUvs);
		uint64_t GetVertexCount() const;
		uint64_t GetTriangleCount() const;
		uint32_t GetVertexOffset() const;
		bool HasAlphas() const;
		bool HasWrinkles() const;

		bool AddVertex(const Vector3 &pos,const Vector3 &n,const Vector4 &t,const Vector2 &uv);
		bool AddAlpha(float alpha);
		bool AddWrinkleFactor(float wrinkle);
		bool AddTriangle(uint32_t idx0,uint32_t idx1,uint32_t idx2,uint32_t shaderIndex);
		uint32_t AddSubMeshShader(Shader &shader);
		void Validate() const;

		void SetMeshData(MeshData &meshData) {m_meshData = meshData.shared_from_this();}
		const std::shared_ptr<MeshData> &GetMeshData() const {return m_meshData;}

		// For internal use only
		std::vector<uint32_t> &GetOriginalShaderIndexTable() {return m_originShaderIndexTable;}
	private:
		Mesh(uint64_t numVerts,uint64_t numTris,Flags flags=Flags::None);

		std::shared_ptr<MeshData> m_meshData = nullptr;
		std::shared_ptr<BoneTransforms> m_boneTransforms = nullptr;
		Flags m_flags = Flags::None;

		std::vector<uint32_t> m_originShaderIndexTable;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(unirender::Mesh::Flags)

#endif
