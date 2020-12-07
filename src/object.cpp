/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/object.hpp"
#include "util_raytracing/mesh.hpp"
#include "util_raytracing/scene.hpp"
#include "util_raytracing/shader.hpp"
#include "util_raytracing/model_cache.hpp"
#include <sharedutils/datastream.h>

#pragma optimize("",off)
unirender::PObject unirender::Object::Create(Mesh *mesh) {return PObject{new Object{mesh}};}
unirender::PObject unirender::Object::Create(Mesh &mesh) {return Create(&mesh);}

unirender::PObject unirender::Object::Create(uint32_t version,DataStream &dsIn,const std::function<PMesh(uint32_t)> &fGetMesh)
{
	auto o = Create(nullptr);
	o->Deserialize(version,dsIn,fGetMesh);
	return o;
}

unirender::Object::Object(Mesh *mesh)
	: WorldObject{},BaseObject{},m_mesh{mesh ? mesh->shared_from_this() : nullptr}
{}

void unirender::Object::Serialize(DataStream &dsOut,const std::function<std::optional<uint32_t>(const Mesh&)> &fGetMeshIndex) const
{
	WorldObject::Serialize(dsOut);
	auto idx = fGetMeshIndex(*m_mesh);
	assert(idx.has_value());
	dsOut->Write<uint32_t>(*idx);
	dsOut->WriteString(GetName());
}
void unirender::Object::Serialize(DataStream &dsOut,const std::unordered_map<const Mesh*,size_t> &meshToIndexTable) const
{
	Serialize(dsOut,[&meshToIndexTable](const Mesh &mesh) -> std::optional<uint32_t> {
		auto it = meshToIndexTable.find(&mesh);
		return (it != meshToIndexTable.end()) ? it->second : std::optional<uint32_t>{};
	});
}
void unirender::Object::Deserialize(uint32_t version,DataStream &dsIn,const std::function<PMesh(uint32_t)> &fGetMesh)
{
	WorldObject::Deserialize(version,dsIn);
	auto meshIdx = dsIn->Read<uint32_t>();
	m_name = dsIn->ReadString();
	auto mesh = fGetMesh(meshIdx);
	assert(mesh);
	m_mesh = mesh;
}

util::WeakHandle<unirender::Object> unirender::Object::GetHandle()
{
	return util::WeakHandle<unirender::Object>{shared_from_this()};
}

void unirender::Object::DoFinalize(Scene &scene)
{
	m_mesh->Finalize(scene);
}

const unirender::Mesh &unirender::Object::GetMesh() const {return const_cast<Object*>(this)->GetMesh();}
unirender::Mesh &unirender::Object::GetMesh() {return *m_mesh;}

const umath::Transform &unirender::Object::GetMotionPose() const {return m_motionPose;}
void unirender::Object::SetMotionPose(const umath::Transform &pose) {m_motionPose = pose;}

void unirender::Object::SetName(const std::string &name) {m_name = name;}
const std::string &unirender::Object::GetName() const {return m_name;}
#pragma optimize("",on)
