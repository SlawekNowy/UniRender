/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __RT_MODEL_CACHE_HPP__
#define __RT_MODEL_CACHE_HPP__

#include "definitions.hpp"
#include <memory>
#include <unordered_map>

#undef GetObject

class DataStream;
namespace raytracing
{
	class NodeManager;
	class Scene;
	class Mesh;
	class Object;
	class Shader;
	class DLLRTUTIL ShaderCache
		: public std::enable_shared_from_this<ShaderCache>
	{
	public:
		static std::shared_ptr<ShaderCache> Create();
		static std::shared_ptr<ShaderCache> Create(DataStream &ds,NodeManager &nodeManager);

		const std::vector<std::shared_ptr<Shader>> &GetShaders() const;
		std::vector<std::shared_ptr<Shader>> &GetShaders();

		size_t AddShader(Shader &shader);
		
		void Merge(const ShaderCache &other);

		PShader GetShader(uint32_t idx) const;

		std::unordered_map<const Shader*,size_t> GetShaderToIndexTable() const;

		void Serialize(DataStream &dsOut);
		void Deserialize(DataStream &dsIn,NodeManager &nodeManager);
	private:
		std::vector<std::shared_ptr<Shader>> m_shaders;
	};

	class ModelCache;
	class DLLRTUTIL ModelCacheChunk
	{
	public:
		static constexpr uint32_t MURMUR_SEED = 195574;
		enum class Flags : uint8_t
		{
			None = 0u,
			HasBakedData = 1u,
			HasUnbakedData = HasBakedData<<1u
		};
		ModelCacheChunk(ShaderCache &shaderCache);
		ModelCacheChunk(DataStream &dsIn,raytracing::NodeManager &nodeManager);
		void Bake();
		void GenerateUnbakedData(bool force=false);
		
		size_t AddMesh(Mesh &mesh);
		size_t AddObject(Object &obj);

		PMesh GetMesh(uint32_t idx) const;
		PObject GetObject(uint32_t idx) const;

		const std::vector<std::shared_ptr<Mesh>> &GetMeshes() const;
		std::vector<std::shared_ptr<Mesh>> &GetMeshes();
		const std::vector<std::shared_ptr<Object>> &GetObjects() const;
		std::vector<std::shared_ptr<Object>> &GetObjects();

		void Serialize(DataStream &dsOut);
		void Deserialize(DataStream &dsIn,raytracing::NodeManager &nodeManager);

		const std::vector<DataStream> &GetBakedObjectData() const;
		const std::vector<DataStream> &GetBakedMeshData() const;

		ShaderCache &GetShaderCache() const {return *m_shaderCache;}

		std::unordered_map<const Mesh*,size_t> GetMeshToIndexTable() const;
	private:
		void Unbake();

		std::shared_ptr<ShaderCache> m_shaderCache = nullptr;

		Flags m_flags = Flags::HasUnbakedData;
		std::vector<std::shared_ptr<Object>> m_objects;
		std::vector<std::shared_ptr<Mesh>> m_meshes;

		std::vector<DataStream> m_bakedObjects;
		std::vector<DataStream> m_bakedMeshes;
		uint32_t m_serializationVersion;
	};

	class DLLRTUTIL ModelCache
		: public std::enable_shared_from_this<ModelCache>
	{
	public:
		static std::shared_ptr<ModelCache> Create();
		static std::shared_ptr<ModelCache> Create(DataStream &ds,raytracing::NodeManager &nodeManager);

		void Merge(ModelCache &other);

		void Serialize(DataStream &dsOut);
		void Deserialize(DataStream &dsIn,raytracing::NodeManager &nodeManager);

		ModelCacheChunk &AddChunk(ShaderCache &shaderCache);
		const std::vector<ModelCacheChunk> &GetChunks() const {return const_cast<ModelCache*>(this)->GetChunks();}
		std::vector<ModelCacheChunk> &GetChunks() {return m_chunks;}

		void Bake();
		void GenerateData();
	private:
		ModelCache()=default;
		std::vector<ModelCacheChunk> m_chunks {};
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(raytracing::ModelCacheChunk::Flags)

#endif
