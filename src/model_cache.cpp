/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
*/

#include "util_raytracing/scene.hpp"
#include "util_raytracing/mesh.hpp"
#include "util_raytracing/object.hpp"
#include "util_raytracing/ccl_shader.hpp"
#include "util_raytracing/model_cache.hpp"
#include <udm.hpp>

#undef GetObject

#pragma optimize("",off)
std::shared_ptr<unirender::ShaderCache> unirender::ShaderCache::Create()
{
	return std::shared_ptr<ShaderCache>{new ShaderCache{}};
}
std::shared_ptr<unirender::ShaderCache> unirender::ShaderCache::Create(udm::LinkedPropertyWrapper &prop,NodeManager &nodeManager)
{
	auto cache = Create();
	cache->Deserialize(prop,nodeManager);
	return cache;
}

const std::vector<std::shared_ptr<unirender::Shader>> &unirender::ShaderCache::GetShaders() const {return m_shaders;}
std::vector<std::shared_ptr<unirender::Shader>> &unirender::ShaderCache::GetShaders() {return m_shaders;}

size_t unirender::ShaderCache::AddShader(Shader &shader)
{
	if(m_shaders.size() == m_shaders.capacity())
		m_shaders.reserve(m_shaders.size() *1.5 +50);
	m_shaders.push_back(shader.shared_from_this());
	return m_shaders.size() -1;
}

unirender::PShader unirender::ShaderCache::GetShader(uint32_t idx) const {return (idx < m_shaders.size()) ? m_shaders.at(idx) : nullptr;}

void unirender::ShaderCache::Merge(const ShaderCache &other)
{
	m_shaders.reserve(m_shaders.size() +other.m_shaders.size());
	for(auto &s : other.m_shaders)
		m_shaders.push_back(s);
}

std::unordered_map<const unirender::Shader*,size_t> unirender::ShaderCache::GetShaderToIndexTable() const
{
	std::unordered_map<const Shader*,size_t> shaderToIndex;
	shaderToIndex.reserve(m_shaders.size());
	for(auto i=decltype(m_shaders.size()){0u};i<m_shaders.size();++i)
		shaderToIndex[m_shaders.at(i).get()] = i;
	return shaderToIndex;
}

void unirender::ShaderCache::Serialize(udm::LinkedPropertyWrapper &prop)
{
	auto udmShaders = prop.AddArray("shaders",m_shaders.size());

	for(auto i=decltype(m_shaders.size()){0u};i<m_shaders.size();++i)
		m_shaders[i]->Serialize(udmShaders[i]);
}
void unirender::ShaderCache::Deserialize(udm::LinkedPropertyWrapper &prop,NodeManager &nodeManager)
{
	auto udmShaders = prop["shaders"];
	auto n = udmShaders.GetSize();
	m_shaders.resize(n);
	for(auto i=decltype(n){0u};i<n;++i)
	{
		auto shader = Shader::Create<GenericShader>();
		shader->Deserialize(udmShaders[i],nodeManager);
		m_shaders.at(i) = shader;
	}
}

//////////

unirender::ModelCacheChunk::ModelCacheChunk(ShaderCache &shaderCache)
	: m_shaderCache{shaderCache.shared_from_this()}
{}
unirender::ModelCacheChunk::ModelCacheChunk(udm::LinkedPropertyWrapper &prop,unirender::NodeManager &nodeManager)
{
	Deserialize(prop,nodeManager);
}
const std::vector<std::shared_ptr<udm::Property>> &unirender::ModelCacheChunk::GetBakedObjectData() const {return m_bakedObjects;}
const std::vector<std::shared_ptr<udm::Property>> &unirender::ModelCacheChunk::GetBakedMeshData() const {return m_bakedMeshes;}
std::unordered_map<const unirender::Mesh*,size_t> unirender::ModelCacheChunk::GetMeshToIndexTable() const
{
	std::unordered_map<const Mesh*,size_t> meshToIndex;
	meshToIndex.reserve(m_meshes.size());
	for(auto i=decltype(m_meshes.size()){0u};i<m_meshes.size();++i)
		meshToIndex[m_meshes.at(i).get()] = i;
	return meshToIndex;
}
void unirender::ModelCacheChunk::Bake()
{
	if(umath::is_flag_set(m_flags,Flags::HasBakedData))
		return;
	auto meshToIndexTable = GetMeshToIndexTable();
	m_bakedObjects.reserve(m_objects.size());
	for(auto &o : m_objects)
	{
		auto prop = udm::Property::Create(udm::Type::Element);
		o->Serialize(udm::LinkedPropertyWrapper{*prop},meshToIndexTable);

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
		auto prop = udm::Property::Create(udm::Type::Element);
		m->Serialize(udm::LinkedPropertyWrapper{*prop},shaderToIndexTable);

		auto hash = util::murmur_hash3(ds->GetData(),ds->GetDataSize(),MURMUR_SEED);
		ds->Write(hash);
		m->SetHash(std::move(hash));

		ds->SetOffset(0);
		m_bakedMeshes.push_back(ds);
	}
	m_flags |= Flags::HasBakedData;
}

const std::vector<std::shared_ptr<unirender::Mesh>> &unirender::ModelCacheChunk::GetMeshes() const {return const_cast<ModelCacheChunk*>(this)->GetMeshes();}
std::vector<std::shared_ptr<unirender::Mesh>> &unirender::ModelCacheChunk::GetMeshes() {return m_meshes;}
const std::vector<std::shared_ptr<unirender::Object>> &unirender::ModelCacheChunk::GetObjects() const {return const_cast<ModelCacheChunk*>(this)->GetObjects();}
std::vector<std::shared_ptr<unirender::Object>> &unirender::ModelCacheChunk::GetObjects() {return m_objects;}

size_t unirender::ModelCacheChunk::AddMesh(Mesh &mesh)
{
	Unbake();
	if(m_meshes.size() == m_meshes.capacity())
		m_meshes.reserve(m_meshes.size() *1.5 +50);
	m_meshes.push_back(mesh.shared_from_this());
	return m_meshes.size() -1;
}
size_t unirender::ModelCacheChunk::AddObject(Object &obj)
{
	Unbake();
	if(m_objects.size() == m_objects.capacity())
		m_objects.reserve(m_objects.size() *1.5 +50);
	m_objects.push_back(obj.shared_from_this());
	return m_objects.size() -1;
}
void unirender::ModelCacheChunk::RemoveMesh(Mesh &mesh)
{
	auto it = std::find_if(m_meshes.begin(),m_meshes.end(),[&mesh](const std::shared_ptr<Mesh> &other) {
		return other.get() == &mesh;
	});
	if(it == m_meshes.end())
		return;
	m_meshes.erase(it);
}
void unirender::ModelCacheChunk::RemoveObject(Object &obj)
{
	auto it = std::find_if(m_objects.begin(),m_objects.end(),[&obj](const std::shared_ptr<Object> &other) {
		return other.get() == &obj;
	});
	if(it == m_objects.end())
		return;
	m_objects.erase(it);
}

unirender::PMesh unirender::ModelCacheChunk::GetMesh(uint32_t idx) const {return (idx < m_meshes.size()) ? m_meshes.at(idx) : nullptr;}
unirender::PObject unirender::ModelCacheChunk::GetObject(uint32_t idx) const {return (idx < m_objects.size()) ? m_objects.at(idx) : nullptr;}

void unirender::ModelCacheChunk::GenerateUnbakedData(bool force)
{
	if(umath::is_flag_set(m_flags,Flags::HasUnbakedData) && force == false)
		return;
	auto &shaders = m_shaderCache->GetShaders();
	m_meshes.resize(m_bakedMeshes.size());
	for(auto i=decltype(m_bakedMeshes.size()){0u};i<m_bakedMeshes.size();++i)
	{
		auto &prop = m_bakedMeshes.at(i);
		auto mesh = Mesh::Create(udm::LinkedPropertyWrapper{*prop},[&](uint32_t idx) -> PShader {
			return (idx < shaders.size()) ? shaders.at(idx) : nullptr;
		});
		auto hash = ds->Read<util::MurmurHash3>();
		mesh->SetHash(std::move(hash));
		m_meshes.at(i) = mesh;
	}

	m_objects.resize(m_bakedObjects.size());
	for(auto i=decltype(m_bakedObjects.size()){0u};i<m_bakedObjects.size();++i)
	{
		auto &prop = m_bakedObjects.at(i);
		auto obj = Object::Create(udm::LinkedPropertyWrapper{*prop},[this](uint32_t idx) -> PMesh {
			return (idx < m_meshes.size()) ? m_meshes.at(idx) : nullptr;
		});
		auto hash = ds->Read<util::MurmurHash3>();
		obj->SetHash(std::move(hash));
		m_objects.at(i) = obj;
	}
	m_flags |= Flags::HasUnbakedData;
}

void unirender::ModelCacheChunk::Unbake()
{
	if(umath::is_flag_set(m_flags,Flags::HasBakedData) == false)
		return;
	if(umath::is_flag_set(m_flags,Flags::HasUnbakedData) == false)
		GenerateUnbakedData();
	m_bakedObjects.clear();
	m_bakedMeshes.clear();
	umath::remove_flag(m_flags,Flags::HasBakedData);
}

void unirender::ModelCacheChunk::Serialize(udm::LinkedPropertyWrapper &prop)
{
	Bake();
	
	GetShaderCache().Serialize(prop);

	auto udmObjects = prop.AddArray("objects",m_bakedObjects.size());
	for(auto i=decltype(m_bakedObjects.size()){0u};i<m_bakedObjects.size();++i)
		udmObjects[i] = *m_bakedObjects[i];
	
	auto udmMeshes = prop.AddArray("meshes",m_bakedMeshes.size());
	for(auto i=decltype(m_bakedMeshes.size()){0u};i<m_bakedMeshes.size();++i)
		udmMeshes[i] = *m_bakedMeshes[i];
}
void unirender::ModelCacheChunk::Deserialize(udm::LinkedPropertyWrapper &prop,unirender::NodeManager &nodeManager)
{
	m_shaderCache = ShaderCache::Create(prop,nodeManager);

	auto udmObjects = prop["objects"];
	auto n = udmObjects.GetSize();
	m_bakedObjects.resize(n);
	for(auto i=decltype(n){0u};i<n;++i)
		m_bakedObjects[i] = udmObjects[i];

	auto udmMeshes = prop["meshes"];
	n = udmMeshes.GetSize();
	m_bakedMeshes.resize(n);
	for(auto i=decltype(n){0u};i<n;++i)
		m_bakedMeshes[i] = udmMeshes[i];

	m_flags = Flags::HasBakedData;
}

//////////

std::shared_ptr<unirender::ModelCache> unirender::ModelCache::Create()
{
	return std::shared_ptr<ModelCache>{new ModelCache{}};
}

std::shared_ptr<unirender::ModelCache> unirender::ModelCache::Create(udm::LinkedPropertyWrapper &prop,unirender::NodeManager &nodeManager)
{
	auto cache = Create();
	cache->Deserialize(prop,nodeManager);
	return cache;
}

void unirender::ModelCache::SetUnique(bool unique) {m_unique = unique;}
bool unirender::ModelCache::IsUnique() const {return m_unique;}

void unirender::ModelCache::Merge(ModelCache &other)
{
	m_chunks.reserve(m_chunks.size() +other.m_chunks.size());
	for(auto &chunk : other.m_chunks)
		m_chunks.push_back(chunk);
}

void unirender::ModelCache::Bake()
{
	for(auto &chunk : m_chunks)
		chunk.Bake();
}

void unirender::ModelCache::GenerateData()
{
	for(auto &chunk : m_chunks)
		chunk.GenerateUnbakedData(true);
}

void unirender::ModelCache::Serialize(udm::LinkedPropertyWrapper &prop)
{
	Bake();
	
	auto udmChunks = prop.AddArray("chunks",m_chunks.size());
	for(auto i=decltype(m_chunks.size()){0u};i<m_chunks.size();++i)
	{
		auto &chunk = m_chunks[i];
		chunk.Serialize(udmChunks[i]);
	}
}
void unirender::ModelCache::Deserialize(udm::LinkedPropertyWrapper &prop,unirender::NodeManager &nodeManager)
{
	auto udmChunks = prop["chunks"];
	auto numChunks = udmChunks.GetSize();
	m_chunks.reserve(numChunks);
	for(auto i=decltype(numChunks){0u};i<numChunks;++i)
		m_chunks.emplace_back(udmChunks[i],nodeManager);
}
unirender::ModelCacheChunk &unirender::ModelCache::AddChunk(ShaderCache &shaderCache)
{
	if(m_chunks.size() == m_chunks.capacity())
		m_chunks.reserve(m_chunks.size() *1.5 +10);
	m_chunks.emplace_back(shaderCache);
	return m_chunks.back();
}
#pragma optimize("",on)
