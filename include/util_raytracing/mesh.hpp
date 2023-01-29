/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
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
#include <sharedutils/util_hair.hpp>

class DataStream;
namespace unirender {
	class Shader;
	class Scene;
	class Mesh;
	class ShaderCache;
	using PMesh = std::shared_ptr<Mesh>;
	using PShader = std::shared_ptr<Shader>;
	class DLLRTUTIL Mesh : public BaseObject, public std::enable_shared_from_this<Mesh> {
	  public:
		struct DLLRTUTIL HairStandDataSet {
			util::HairStrandData strandData;
			uint32_t shaderIndex;
		};
		enum class Flags : uint8_t { None = 0u, HasAlphas = 1u, HasWrinkles = HasAlphas << 1u };
		struct DLLRTUTIL SerializationHeader {
			~SerializationHeader();
			std::string name;
			Flags flags;
			uint64_t numVerts;
			uint64_t numTris;
			void *udmProperty;
		};
		static const std::string TANGENT_POSTFIX;
		static const std::string TANGENT_SIGN_POSTIFX;
		using Smooth = uint8_t; // Boolean value

		static PMesh Create(const std::string &name, uint64_t numVerts, uint64_t numTris, Flags flags = Flags::None);
		static PMesh Create(DataStream &dsIn, const std::function<PShader(uint32_t)> &fGetShader);
		static PMesh Create(DataStream &dsIn, const ShaderCache &cache);
		util::WeakHandle<Mesh> GetHandle();

		void Serialize(DataStream &dsOut, const std::function<std::optional<uint32_t>(const Shader &)> &fGetShaderIndex) const;
		void Serialize(DataStream &dsOut, const std::unordered_map<const Shader *, size_t> shaderToIndexTable) const;
		void Deserialize(DataStream &dsIn, const std::function<PShader(uint32_t)> &fGetShader, SerializationHeader &header);
		static void ReadSerializationHeader(DataStream &dsIn, SerializationHeader &outHeader);

		void Merge(const Mesh &other);

		const std::vector<PShader> &GetSubMeshShaders() const;
		std::vector<PShader> &GetSubMeshShaders();
		void SetLightmapUVs(std::vector<Vector2> &&lightmapUvs);
		uint64_t GetVertexCount() const;
		uint64_t GetTriangleCount() const;
		uint32_t GetVertexOffset() const;
		bool HasAlphas() const;
		bool HasWrinkles() const;

		bool AddVertex(const Vector3 &pos, const Vector3 &n, const Vector4 &t, const Vector2 &uv);
		bool AddAlpha(float alpha);
		bool AddWrinkleFactor(float wrinkle);
		bool AddTriangle(uint32_t idx0, uint32_t idx1, uint32_t idx2, uint32_t shaderIndex);
		uint32_t AddSubMeshShader(Shader &shader);
		void Validate() const;

		const std::vector<Vector3> &GetVertices() const { return m_verts; }
		const std::vector<int> &GetTriangles() const { return m_triangles; }
		const std::vector<Vector3> &GetVertexNormals() const { return m_vertexNormals; }
		const std::vector<Vector2> &GetUvs() const { return m_uvs; }
		const std::vector<Vector2> &GetLightmapUvs() const { return m_lightmapUvs; }
		const std::vector<Vector3> &GetUvTangents() const { return m_uvTangents; }
		const std::vector<float> &GetUvTangentSigns() const { return m_uvTangentSigns; }
		const std::optional<std::vector<float>> &GetAlphas() const { return m_alphas; }
		const std::vector<Smooth> &GetSmooth() const { return m_smooth; }
		const std::vector<int> &GetShaders() const { return m_shader; }
		const std::vector<Vector2> &GetPerVertexUvs() const { return m_perVertexUvs; }
		void AddHairStrandData(const util::HairStrandData &hairStrandData, uint32_t shaderIdx);
		const std::vector<HairStandDataSet> &GetHairStrandDataSets() const;

		// For internal use only
		std::vector<uint32_t> &GetOriginalShaderIndexTable() { return m_originShaderIndexTable; }
	  private:
		Mesh(uint64_t numVerts, uint64_t numTris, Flags flags = Flags::None);
		std::vector<Vector2> m_perVertexUvs = {};
		std::vector<Vector4> m_perVertexTangents = {};
		std::vector<float> m_perVertexTangentSigns = {};
		std::vector<float> m_perVertexAlphas = {};
		std::vector<PShader> m_subMeshShaders = {};
		std::vector<Vector2> m_lightmapUvs = {};
		std::vector<HairStandDataSet> m_hairStrandDataSets;
		uint64_t m_numVerts = 0ull;
		uint64_t m_numTris = 0ull;
		Flags m_flags = Flags::None;

		// Note: These are moved 1:1 into the ccl::Mesh structure during finalization
		std::vector<Vector3> m_verts;
		std::vector<int> m_triangles;
		std::vector<Vector3> m_vertexNormals;
		std::vector<Vector2> m_uvs;
		std::vector<Vector3> m_uvTangents;
		std::vector<float> m_uvTangentSigns;
		std::optional<std::vector<float>> m_alphas {};
		std::vector<Smooth> m_smooth;
		std::vector<int> m_shader;
		size_t m_numNGons = 0;
		size_t m_numSubdFaces = 0;

		std::vector<uint32_t> m_originShaderIndexTable;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(unirender::Mesh::Flags)

#endif
