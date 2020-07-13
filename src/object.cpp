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
#include <sharedutils/datastream.h>
#include <render/object.h>
#include <render/scene.h>
#include <render/mesh.h>

#pragma optimize("",off)
raytracing::PObject raytracing::Object::Create(Scene &scene,Mesh *mesh)
{
	auto *object = new ccl::Object{}; // Object will be removed automatically by cycles
	if(mesh)
		object->mesh = **mesh;
	object->tfm = ccl::transform_identity();
	scene->objects.push_back(object);
	auto pObject = PObject{new Object{scene,*object,static_cast<uint32_t>(scene->objects.size() -1),mesh}};
	scene.m_objects.push_back(pObject);
	return pObject;
}
raytracing::PObject raytracing::Object::Create(Scene &scene,Mesh &mesh) {return Create(scene,&mesh);}

raytracing::PObject raytracing::Object::Create(Scene &scene,DataStream &dsIn)
{
	auto o = Create(scene,nullptr);
	o->Deserialize(dsIn);
	return o;
}

raytracing::Object::Object(Scene &scene,ccl::Object &object,uint32_t objectId,Mesh *mesh)
	: WorldObject{scene},m_object{object},m_mesh{mesh ? mesh->shared_from_this() : nullptr},
	m_id{objectId}
{}

void raytracing::Object::Serialize(DataStream &dsOut) const
{
	WorldObject::Serialize(dsOut);
	auto &scene = GetScene();
	auto &meshes = scene.GetMeshes();
	auto it = std::find(meshes.begin(),meshes.end(),m_mesh);
	assert(it != meshes.end());
	dsOut->Write<uint32_t>(it -meshes.begin());
}
void raytracing::Object::Deserialize(DataStream &dsIn)
{
	WorldObject::Deserialize(dsIn);
	auto meshIdx = dsIn->Read<uint32_t>();
	auto &scene = GetScene();
	auto &meshes = scene.GetMeshes();
	assert(meshIdx < meshes.size());
	m_mesh = meshes.at(meshIdx);
	m_object.mesh = **m_mesh;
}

uint32_t raytracing::Object::GetId() const {return m_id;}

util::WeakHandle<raytracing::Object> raytracing::Object::GetHandle()
{
	return util::WeakHandle<raytracing::Object>{shared_from_this()};
}

void raytracing::Object::DoFinalize()
{
	m_mesh->Finalize();
	m_object.tfm = Scene::ToCyclesTransform(GetPose());

#ifdef ENABLE_MOTION_BLUR_TEST
	m_motionPose.SetOrigin(Vector3{100.f,100.f,100.f});
	m_object.motion.push_back_slow(Scene::ToCyclesTransform(GetMotionPose()));
#endif
}

const raytracing::Mesh &raytracing::Object::GetMesh() const {return const_cast<Object*>(this)->GetMesh();}
raytracing::Mesh &raytracing::Object::GetMesh() {return *m_mesh;}

const umath::Transform &raytracing::Object::GetMotionPose() const {return m_motionPose;}
void raytracing::Object::SetMotionPose(const umath::Transform &pose) {m_motionPose = pose;}

ccl::Object *raytracing::Object::operator->() {return &m_object;}
ccl::Object *raytracing::Object::operator*() {return &m_object;}
#pragma optimize("",on)
