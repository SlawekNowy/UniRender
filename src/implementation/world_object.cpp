/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include <sharedutils/datastream.h>
#include <mathutil/uvec.h>

module pragma.scenekit;

import :world_object;

pragma::scenekit::WorldObject::WorldObject() {}

void pragma::scenekit::WorldObject::SetPos(const Vector3 &pos) { m_pose.SetOrigin(pos); }
const Vector3 &pragma::scenekit::WorldObject::GetPos() const { return m_pose.GetOrigin(); }

void pragma::scenekit::WorldObject::SetRotation(const Quat &rot) { m_pose.SetRotation(rot); }
const Quat &pragma::scenekit::WorldObject::GetRotation() const { return m_pose.GetRotation(); }

void pragma::scenekit::WorldObject::SetScale(const Vector3 &scale) { m_pose.SetScale(scale); }
const Vector3 &pragma::scenekit::WorldObject::GetScale() const { return m_pose.GetScale(); }

umath::ScaledTransform &pragma::scenekit::WorldObject::GetPose() { return m_pose; }
const umath::ScaledTransform &pragma::scenekit::WorldObject::GetPose() const { return const_cast<WorldObject *>(this)->GetPose(); }

void pragma::scenekit::WorldObject::Serialize(DataStream &dsOut) const
{
	dsOut->Write(m_pose);
	dsOut->Write(m_uuid);
}
void pragma::scenekit::WorldObject::Deserialize(uint32_t version, DataStream &dsIn)
{
	m_pose = dsIn->Read<decltype(m_pose)>();
	m_uuid = dsIn->Read<decltype(m_uuid)>();
}
