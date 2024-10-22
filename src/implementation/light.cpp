/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include <mathutil/umath_lighting.hpp>
#include <mathutil/color.h>
#include <sharedutils/util_pragma.hpp>
#include <sharedutils/datastream.h>
#include <sharedutils/util_weak_handle.hpp>

module pragma.scenekit;

import :light;
import :scene;

pragma::scenekit::PLight pragma::scenekit::Light::Create()
{
	auto pLight = PLight {new Light {}};
	return pLight;
}

pragma::scenekit::PLight pragma::scenekit::Light::Create(uint32_t version, DataStream &dsIn)
{
	auto light = Create();
	light->Deserialize(version, dsIn);
	return light;
}

pragma::scenekit::Light::Light() : WorldObject {} {}

util::WeakHandle<pragma::scenekit::Light> pragma::scenekit::Light::GetHandle() { return util::WeakHandle<pragma::scenekit::Light> {shared_from_this()}; }

void pragma::scenekit::Light::SetType(Type type) { m_type = type; }

void pragma::scenekit::Light::SetConeAngle(umath::Degree outerAngle, umath::Fraction blendFraction)
{
	m_blendFraction = blendFraction;
	m_spotOuterAngle = outerAngle;
}

void pragma::scenekit::Light::SetColor(const Color &color)
{
	m_color = color.ToVector3();
	// Alpha is ignored
}
void pragma::scenekit::Light::SetIntensity(Lumen intensity) { m_intensity = intensity; }

void pragma::scenekit::Light::SetSize(float size) { m_size = size; }

void pragma::scenekit::Light::SetAxisU(const Vector3 &axisU) { m_axisU = axisU; }
void pragma::scenekit::Light::SetAxisV(const Vector3 &axisV) { m_axisV = axisV; }
void pragma::scenekit::Light::SetSizeU(float sizeU) { m_sizeU = sizeU; }
void pragma::scenekit::Light::SetSizeV(float sizeV) { m_sizeV = sizeV; }

void pragma::scenekit::Light::Serialize(DataStream &dsOut) const
{
	WorldObject::Serialize(dsOut);
	Scene::SerializeDataBlock(*this, dsOut, offsetof(Light, m_size));
}

void pragma::scenekit::Light::Deserialize(uint32_t version, DataStream &dsIn)
{
	WorldObject::Deserialize(version, dsIn);
	Scene::DeserializeDataBlock(*this, dsIn, offsetof(Light, m_size));
}

void pragma::scenekit::Light::DoFinalize(Scene &scene) {}
