/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
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
#include <kernel/kernel_types.h>

namespace ccl {class Mesh; class Attribute; struct float4; struct float3; struct float2;};
class DataStream;
namespace raytracing
{
	void compute_tangents(ccl::Mesh *mesh,bool need_sign,bool active_render);
	class Shader;
	class CCLShader;
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
			HasWrinkles = HasAlphas<<1u,

			CCLObjectOwnedByScene = HasWrinkles<<1u
		};
		static constexpr ccl::AttributeStandard ALPHA_ATTRIBUTE_TYPE = ccl::AttributeStandard::ATTR_STD_POINTINESS;

		static PMesh Create(const std::string &name,uint64_t numVerts,uint64_t numTris,Flags flags=Flags::None);
		static PMesh Create(DataStream &dsIn,const std::function<PShader(uint32_t)> &fGetShader);
		static PMesh Create(DataStream &dsIn,const ShaderCache &cache);
		virtual ~Mesh() override;
		util::WeakHandle<Mesh> GetHandle();

		void Serialize(DataStream &dsOut,const std::function<std::optional<uint32_t>(const Shader&)> &fGetShaderIndex) const;
		void Serialize(DataStream &dsOut,const std::unordered_map<const Shader*,size_t> shaderToIndexTable) const;
		void Deserialize(DataStream &dsIn,const std::function<PShader(uint32_t)> &fGetShader);

		const ccl::float4 *GetNormals() const;
		const ccl::float3 *GetTangents() const;
		const float *GetTangentSigns() const;
		const float *GetAlphas() const;
		const float *GetWrinkleFactors() const;
		const ccl::float2 *GetUVs() const;
		const ccl::float2 *GetLightmapUVs() const;
		const std::vector<PShader> &GetSubMeshShaders() const;
		std::vector<PShader> &GetSubMeshShaders();
		void SetLightmapUVs(std::vector<ccl::float2> &&lightmapUvs);
		uint64_t GetVertexCount() const;
		uint64_t GetTriangleCount() const;
		uint32_t GetVertexOffset() const;
		std::string GetName() const;
		bool HasAlphas() const;
		bool HasWrinkles() const;

		bool AddVertex(const Vector3 &pos,const Vector3 &n,const Vector4 &t,const Vector2 &uv);
		bool AddAlpha(float alpha);
		bool AddWrinkleFactor(float wrinkle);
		bool AddTriangle(uint32_t idx0,uint32_t idx1,uint32_t idx2,uint32_t shaderIndex);
		uint32_t AddSubMeshShader(Shader &shader);
		void Validate() const;
		ccl::Mesh *operator->();
		ccl::Mesh *operator*();

		// For internal use only
		std::vector<uint32_t> &GetOriginalShaderIndexTable() {return m_originShaderIndexTable;}
	private:
		Mesh(ccl::Mesh &mesh,uint64_t numVerts,uint64_t numTris,Flags flags=Flags::None);
		virtual void DoFinalize(Scene &scene) override;
		std::vector<Vector2> m_perVertexUvs = {};
		std::vector<Vector4> m_perVertexTangents = {};
		std::vector<float> m_perVertexTangentSigns = {};
		std::vector<float> m_perVertexAlphas = {};
		std::vector<PShader> m_subMeshShaders = {};
		ccl::Mesh &m_mesh;
		ccl::float4 *m_normals = nullptr;
		ccl::float3 *m_tangents = nullptr;
		float *m_tangentSigns = nullptr;
		ccl::float2 *m_uvs = nullptr;
		float *m_alphas = nullptr;
		std::vector<ccl::float2> m_lightmapUvs = {};
		uint64_t m_numVerts = 0ull;
		uint64_t m_numTris = 0ull;
		Flags m_flags = Flags::None;

		std::vector<uint32_t> m_originShaderIndexTable;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(raytracing::Mesh::Flags)

#endif
