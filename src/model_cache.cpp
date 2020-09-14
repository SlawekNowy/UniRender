/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/scene.hpp"
#include "util_raytracing/mesh.hpp"
#include "util_raytracing/object.hpp"
#include "util_raytracing/ccl_shader.hpp"
#include "util_raytracing/model_cache.hpp"

#undef GetObject

#pragma optimize("",off)
std::shared_ptr<raytracing::ShaderCache> raytracing::ShaderCache::Create()
{
	return std::shared_ptr<ShaderCache>{new ShaderCache{}};
}
std::shared_ptr<raytracing::ShaderCache> raytracing::ShaderCache::Create(DataStream &ds,NodeManager &nodeManager)
{
	auto cache = Create();
	cache->Deserialize(ds,nodeManager);
	return cache;
}

const std::vector<std::shared_ptr<raytracing::Shader>> &raytracing::ShaderCache::GetShaders() const {return m_shaders;}
std::vector<std::shared_ptr<raytracing::Shader>> &raytracing::ShaderCache::GetShaders() {return m_shaders;}

size_t raytracing::ShaderCache::AddShader(Shader &shader)
{
	if(m_shaders.size() == m_shaders.capacity())
		m_shaders.reserve(m_shaders.size() *1.5 +50);
	m_shaders.push_back(shader.shared_from_this());
	return m_shaders.size() -1;
}

raytracing::PShader raytracing::ShaderCache::GetShader(uint32_t idx) const {return (idx < m_shaders.size()) ? m_shaders.at(idx) : nullptr;}

void raytracing::ShaderCache::Merge(const ShaderCache &other)
{
	m_shaders.reserve(m_shaders.size() +other.m_shaders.size());
	for(auto &s : other.m_shaders)
		m_shaders.push_back(s);
}

std::unordered_map<const raytracing::Shader*,size_t> raytracing::ShaderCache::GetShaderToIndexTable() const
{
	std::unordered_map<const Shader*,size_t> shaderToIndex;
	shaderToIndex.reserve(m_shaders.size());
	for(auto i=decltype(m_shaders.size()){0u};i<m_shaders.size();++i)
		shaderToIndex[m_shaders.at(i).get()] = i;
	return shaderToIndex;
}

void raytracing::ShaderCache::Serialize(DataStream &dsOut)
{
	dsOut->Write<decltype(Scene::SERIALIZATION_VERSION)>(Scene::SERIALIZATION_VERSION);
	dsOut->Write<uint32_t>(m_shaders.size());

	std::unordered_map<const Shader*,uint32_t> shaderToIndex;
	shaderToIndex.reserve(m_shaders.size());
	uint32_t shaderIdx = 0;
	for(auto &s : m_shaders)
	{
		s->Serialize(dsOut);
		shaderToIndex[s.get()] = shaderIdx++;
	}
}
void raytracing::ShaderCache::Deserialize(DataStream &dsIn,NodeManager &nodeManager)
{
	auto version = dsIn->Read<uint32_t>();
	if(version < 3 || version > Scene::SERIALIZATION_VERSION)
		return;
	auto n = dsIn->Read<uint32_t>();
	m_shaders.resize(n);
	for(auto i=decltype(n){0u};i<n;++i)
	{
		auto shader = Shader::Create<GenericShader>();
		shader->Deserialize(dsIn,nodeManager);
		m_shaders.at(i) = shader;
	}
}

//////////

raytracing::ModelCacheChunk::ModelCacheChunk(ShaderCache &shaderCache)
	: m_shaderCache{shaderCache.shared_from_this()},m_serializationVersion{Scene::SERIALIZATION_VERSION}
{}
raytracing::ModelCacheChunk::ModelCacheChunk(DataStream &dsIn,raytracing::NodeManager &nodeManager)
	: m_serializationVersion{Scene::SERIALIZATION_VERSION}
{
	Deserialize(dsIn,nodeManager);
}
const std::vector<DataStream> &raytracing::ModelCacheChunk::GetBakedObjectData() const {return m_bakedObjects;}
const std::vector<DataStream> &raytracing::ModelCacheChunk::GetBakedMeshData() const {return m_bakedMeshes;}
std::unordered_map<const raytracing::Mesh*,size_t> raytracing::ModelCacheChunk::GetMeshToIndexTable() const
{
	std::unordered_map<const Mesh*,size_t> meshToIndex;
	meshToIndex.reserve(m_meshes.size());
	for(auto i=decltype(m_meshes.size()){0u};i<m_meshes.size();++i)
		meshToIndex[m_meshes.at(i).get()] = i;
	return meshToIndex;
}
void raytracing::ModelCacheChunk::Bake()
{
	if(umath::is_flag_set(m_flags,Flags::HasBakedData))
		return;
	auto meshToIndexTable = GetMeshToIndexTable();
	m_bakedObjects.reserve(m_objects.size());
	for(auto &o : m_objects)
	{
		DataStream ds;
		o->Serialize(ds,meshToIndexTable);

		auto hash = util::murmur_hash3(ds->GetData(),ds->GetDataSize(),MURMUR_SEED);
		ds->Write(hash);
		o->SetHash(std::move(hash));

		ds->SetOffset(0);
		m_bakedObjects.push_back(ds);
	}
	
	auto shaderToIndexTable = m_shaderCache->GetShaderToIndexTable();
	m_bakedMeshes.reserve(m_meshes.size());
	for(auto &m : m_meshes)
	{
		DataStream ds;
		m->Serialize(ds,shaderToIndexTable);

		auto hash = util::murmur_hash3(ds->GetData(),ds->GetDataSize(),MURMUR_SEED);
		ds->Write(hash);
		m->SetHash(std::move(hash));

		ds->SetOffset(0);
		m_bakedMeshes.push_back(ds);
	}
	m_flags |= Flags::HasBakedData;
}

const std::vector<std::shared_ptr<raytracing::Mesh>> &raytracing::ModelCacheChunk::GetMeshes() const {return const_cast<ModelCacheChunk*>(this)->GetMeshes();}
std::vector<std::shared_ptr<raytracing::Mesh>> &raytracing::ModelCacheChunk::GetMeshes() {return m_meshes;}
const std::vector<std::shared_ptr<raytracing::Object>> &raytracing::ModelCacheChunk::GetObjects() const {return const_cast<ModelCacheChunk*>(this)->GetObjects();}
std::vector<std::shared_ptr<raytracing::Object>> &raytracing::ModelCacheChunk::GetObjects() {return m_objects;}

size_t raytracing::ModelCacheChunk::AddMesh(Mesh &mesh)
{
	Unbake();
	if(m_meshes.size() == m_meshes.capacity())
		m_meshes.reserve(m_meshes.size() *1.5 +50);
	m_meshes.push_back(mesh.shared_from_this());
	return m_meshes.size() -1;
}
size_t raytracing::ModelCacheChunk::AddObject(Object &obj)
{
	Unbake();
	if(m_objects.size() == m_objects.capacity())
		m_objects.reserve(m_objects.size() *1.5 +50);
	m_objects.push_back(obj.shared_from_this());
	return m_objects.size() -1;
}

raytracing::PMesh raytracing::ModelCacheChunk::GetMesh(uint32_t idx) const {return (idx < m_meshes.size()) ? m_meshes.at(idx) : nullptr;}
raytracing::PObject raytracing::ModelCacheChunk::GetObject(uint32_t idx) const {return (idx < m_objects.size()) ? m_objects.at(idx) : nullptr;}

void raytracing::ModelCacheChunk::GenerateUnbakedData(bool force)
{
	if(umath::is_flag_set(m_flags,Flags::HasUnbakedData) && force == false)
		return;
	auto &shaders = m_shaderCache->GetShaders();
	m_meshes.resize(m_bakedMeshes.size());
	for(auto i=decltype(m_bakedMeshes.size()){0u};i<m_bakedMeshes.size();++i)
	{
		auto &ds = m_bakedMeshes.at(i);
		auto mesh = Mesh::Create(ds,[&](uint32_t idx) -> PShader {
			return (idx < shaders.size()) ? shaders.at(idx) : nullptr;
		});
		auto hash = ds->Read<util::MurmurHash3>();
		mesh->SetHash(std::move(hash));
		m_meshes.at(i) = mesh;
	}

	m_objects.resize(m_bakedObjects.size());
	for(auto i=decltype(m_bakedObjects.size()){0u};i<m_bakedObjects.size();++i)
	{
		auto &ds = m_bakedObjects.at(i);
		auto obj = Object::Create(m_serializationVersion,ds,[this](uint32_t idx) -> PMesh {
			return (idx < m_meshes.size()) ? m_meshes.at(idx) : nullptr;
		});
		auto hash = ds->Read<util::MurmurHash3>();
		obj->SetHash(std::move(hash));
		m_objects.at(i) = obj;
	}
	m_flags |= Flags::HasUnbakedData;
}

void raytracing::ModelCacheChunk::Unbake()
{
	if(umath::is_flag_set(m_flags,Flags::HasBakedData) == false)
		return;
	if(umath::is_flag_set(m_flags,Flags::HasUnbakedData) == false)
		GenerateUnbakedData();
	m_bakedObjects.clear();
	m_bakedMeshes.clear();
	umath::remove_flag(m_flags,Flags::HasBakedData);
}

void raytracing::ModelCacheChunk::Serialize(DataStream &dsOut)
{
	Bake();
	
	dsOut->Write<decltype(Scene::SERIALIZATION_VERSION)>(Scene::SERIALIZATION_VERSION);
	GetShaderCache().Serialize(dsOut);
	size_t size = 0;
	for(auto &ds : m_bakedObjects)
		size += ds->GetDataSize();
	for(auto &ds : m_bakedMeshes)
		size += ds->GetDataSize();

	dsOut->Reserve(dsOut->GetOffset() +sizeof(uint32_t) *2 +size +(m_bakedObjects.size() +m_bakedMeshes.size()) *sizeof(size_t));

	auto fWriteList = [&dsOut](const std::vector<DataStream> &list) {
		dsOut->Write<uint32_t>(list.size());
		for(auto &ds : list)
		{
			dsOut->Write<size_t>(ds->GetDataSize());
			dsOut->Write(const_cast<DataStream&>(ds)->GetData(),ds->GetDataSize());
		}
	};
	fWriteList(m_bakedObjects);
	fWriteList(m_bakedMeshes);
}
void raytracing::ModelCacheChunk::Deserialize(DataStream &dsIn,raytracing::NodeManager &nodeManager)
{
	auto version = dsIn->Read<uint32_t>();
	if(version < 3 || version > Scene::SERIALIZATION_VERSION)
		return;
	m_shaderCache = ShaderCache::Create(dsIn,nodeManager);
	m_serializationVersion = version;
	auto fReadList = [&dsIn](std::vector<DataStream> &list) {
		auto numObjects = dsIn->Read<uint32_t>();
		list.resize(numObjects);
		for(auto i=decltype(numObjects){0u};i<numObjects;++i)
		{
			DataStream ds {};
			auto size = dsIn->Read<size_t>();
			ds->Resize(size);
			ds->SetOffset(0);
			dsIn->Read(ds->GetData(),size);
			list.at(i) = ds;
		}
	};
	fReadList(m_bakedObjects);
	fReadList(m_bakedMeshes);
	m_flags = Flags::HasBakedData;
}

//////////

std::shared_ptr<raytracing::ModelCache> raytracing::ModelCache::Create()
{
	return std::shared_ptr<ModelCache>{new ModelCache{}};
}

std::shared_ptr<raytracing::ModelCache> raytracing::ModelCache::Create(DataStream &ds,raytracing::NodeManager &nodeManager)
{
	auto cache = Create();
	cache->Deserialize(ds,nodeManager);
	return cache;
}

void raytracing::ModelCache::Merge(ModelCache &other)
{
	m_chunks.reserve(m_chunks.size() +other.m_chunks.size());
	for(auto &chunk : other.m_chunks)
		m_chunks.push_back(chunk);
}

void raytracing::ModelCache::Bake()
{
	for(auto &chunk : m_chunks)
		chunk.Bake();
}

void raytracing::ModelCache::GenerateData()
{
	for(auto &chunk : m_chunks)
		chunk.GenerateUnbakedData(true);
}

void raytracing::ModelCache::Serialize(DataStream &dsOut)
{
	Bake();
	dsOut->Write<decltype(Scene::SERIALIZATION_VERSION)>(Scene::SERIALIZATION_VERSION);

	dsOut->Write<uint32_t>(m_chunks.size());
	for(auto &chunk : m_chunks)
		chunk.Serialize(dsOut);
}
void raytracing::ModelCache::Deserialize(DataStream &dsIn,raytracing::NodeManager &nodeManager)
{
	auto version = dsIn->Read<uint32_t>();
	if(version < 3 || version > Scene::SERIALIZATION_VERSION)
		return;
	auto numChunks = dsIn->Read<uint32_t>();
	m_chunks.reserve(numChunks);
	for(auto i=decltype(numChunks){0u};i<numChunks;++i)
		m_chunks.emplace_back(dsIn,nodeManager);
}
raytracing::ModelCacheChunk &raytracing::ModelCache::AddChunk(ShaderCache &shaderCache)
{
	if(m_chunks.size() == m_chunks.capacity())
		m_chunks.reserve(m_chunks.size() *1.5 +10);
	m_chunks.emplace_back(shaderCache);
	return m_chunks.back();
}
#pragma optimize("",on)
