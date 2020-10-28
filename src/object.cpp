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
#include <render/object.h>
#include <render/scene.h>
#include <render/mesh.h>

#pragma optimize("",off)
raytracing::PObject raytracing::Object::Create(Mesh *mesh)
{
	auto *object = new ccl::Object{}; // Object will be removed automatically by cycles
	if(mesh)
		object->mesh = **mesh;
	object->tfm = ccl::transform_identity();
	return PObject{new Object{*object,mesh}};
}
raytracing::PObject raytracing::Object::Create(Mesh &mesh) {return Create(&mesh);}

raytracing::PObject raytracing::Object::Create(uint32_t version,DataStream &dsIn,const std::function<PMesh(uint32_t)> &fGetMesh)
{
	auto o = Create(nullptr);
	o->Deserialize(version,dsIn,fGetMesh);
	return o;
}

raytracing::Object::Object(ccl::Object &object,Mesh *mesh)
	: WorldObject{},BaseObject{},m_object{object},m_mesh{mesh ? mesh->shared_from_this() : nullptr}
{}

raytracing::Object::~Object()
{
	if(umath::is_flag_set(m_flags,Flags::CCLObjectOwnedByScene) == false)
		delete &m_object;
}

void raytracing::Object::Serialize(DataStream &dsOut,const std::function<std::optional<uint32_t>(const Mesh&)> &fGetMeshIndex) const
{
	WorldObject::Serialize(dsOut);
	auto idx = fGetMeshIndex(*m_mesh);
	assert(idx.has_value());
	dsOut->Write<uint32_t>(*idx);
	dsOut->WriteString(GetName());
}
void raytracing::Object::Serialize(DataStream &dsOut,const std::unordered_map<const Mesh*,size_t> &meshToIndexTable) const
{
	Serialize(dsOut,[&meshToIndexTable](const Mesh &mesh) -> std::optional<uint32_t> {
		auto it = meshToIndexTable.find(&mesh);
		return (it != meshToIndexTable.end()) ? it->second : std::optional<uint32_t>{};
	});
}
void raytracing::Object::Deserialize(uint32_t version,DataStream &dsIn,const std::function<PMesh(uint32_t)> &fGetMesh)
{
	WorldObject::Deserialize(version,dsIn);
	auto meshIdx = dsIn->Read<uint32_t>();
	m_name = dsIn->ReadString();
	auto mesh = fGetMesh(meshIdx);
	assert(mesh);
	m_mesh = mesh;
	m_object.mesh = **m_mesh;
}

util::WeakHandle<raytracing::Object> raytracing::Object::GetHandle()
{
	return util::WeakHandle<raytracing::Object>{shared_from_this()};
}

void raytracing::Object::DoFinalize(Scene &scene)
{
	m_mesh->Finalize(scene);
	m_object.tfm = Scene::ToCyclesTransform(GetPose());
	m_flags |= Flags::CCLObjectOwnedByScene;
	// m_object.tag_update(*scene);

#ifdef ENABLE_MOTION_BLUR_TEST
	m_motionPose.SetOrigin(Vector3{100.f,100.f,100.f});
	m_object.motion.push_back_slow(Scene::ToCyclesTransform(GetMotionPose()));
#endif
}

const raytracing::Mesh &raytracing::Object::GetMesh() const {return const_cast<Object*>(this)->GetMesh();}
raytracing::Mesh &raytracing::Object::GetMesh() {return *m_mesh;}

const umath::Transform &raytracing::Object::GetMotionPose() const {return m_motionPose;}
void raytracing::Object::SetMotionPose(const umath::Transform &pose) {m_motionPose = pose;}

void raytracing::Object::SetName(const std::string &name) {m_name = name;}
const std::string &raytracing::Object::GetName() const {return m_name;}

ccl::Object *raytracing::Object::operator->() {return &m_object;}
const ccl::Object *raytracing::Object::operator->() const {return const_cast<Object*>(this)->operator->();}
ccl::Object *raytracing::Object::operator*() {return &m_object;}
const ccl::Object *raytracing::Object::operator*() const {return const_cast<Object*>(this)->operator*();}
#pragma optimize("",on)
