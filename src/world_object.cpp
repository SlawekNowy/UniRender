/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/world_object.hpp"
#include <sharedutils/datastream.h>

#pragma optimize("",off)
raytracing::WorldObject::WorldObject(Scene &scene)
	: SceneObject{scene}
{}

void raytracing::WorldObject::SetPos(const Vector3 &pos) {m_pose.SetOrigin(pos);}
const Vector3 &raytracing::WorldObject::GetPos() const {return m_pose.GetOrigin();}

void raytracing::WorldObject::SetRotation(const Quat &rot) {m_pose.SetRotation(rot);}
const Quat &raytracing::WorldObject::GetRotation() const {return m_pose.GetRotation();}

void raytracing::WorldObject::SetScale(const Vector3 &scale) {m_pose.SetScale(scale);}
const Vector3 &raytracing::WorldObject::GetScale() const {return m_pose.GetScale();}

umath::ScaledTransform &raytracing::WorldObject::GetPose() {return m_pose;}
const umath::ScaledTransform &raytracing::WorldObject::GetPose() const {return const_cast<WorldObject*>(this)->GetPose();}

void raytracing::WorldObject::Serialize(DataStream &dsOut) const
{
	dsOut->Write(m_pose);
}
void raytracing::WorldObject::Deserialize(uint32_t version,DataStream &dsIn)
{
	m_pose = dsIn->Read<decltype(m_pose)>();
}
#pragma optimize("",on)
