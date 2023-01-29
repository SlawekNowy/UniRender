/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#include "util_raytracing/light.hpp"
#include "util_raytracing/scene.hpp"
#include "util_raytracing/shader.hpp"
#include <mathutil/umath_lighting.hpp>
#include <sharedutils/util_pragma.hpp>
#include <sharedutils/datastream.h>

unirender::PLight unirender::Light::Create()
{
	auto pLight = PLight {new Light {}};
	return pLight;
}

unirender::PLight unirender::Light::Create(uint32_t version, DataStream &dsIn)
{
	auto light = Create();
	light->Deserialize(version, dsIn);
	return light;
}

unirender::Light::Light() : WorldObject {} {}

util::WeakHandle<unirender::Light> unirender::Light::GetHandle() { return util::WeakHandle<unirender::Light> {shared_from_this()}; }

void unirender::Light::SetType(Type type) { m_type = type; }

void unirender::Light::SetConeAngle(umath::Degree outerAngle, umath::Fraction blendFraction)
{
	m_blendFraction = blendFraction;
	m_spotOuterAngle = outerAngle;
}

void unirender::Light::SetColor(const Color &color)
{
	m_color = color.ToVector3();
	// Alpha is ignored
}
void unirender::Light::SetIntensity(Lumen intensity) { m_intensity = intensity; }

void unirender::Light::SetSize(float size) { m_size = size; }

void unirender::Light::SetAxisU(const Vector3 &axisU) { m_axisU = axisU; }
void unirender::Light::SetAxisV(const Vector3 &axisV) { m_axisV = axisV; }
void unirender::Light::SetSizeU(float sizeU) { m_sizeU = sizeU; }
void unirender::Light::SetSizeV(float sizeV) { m_sizeV = sizeV; }

void unirender::Light::Serialize(DataStream &dsOut) const
{
	WorldObject::Serialize(dsOut);
	Scene::SerializeDataBlock(*this, dsOut, offsetof(Light, m_size));
}

void unirender::Light::Deserialize(uint32_t version, DataStream &dsIn)
{
	WorldObject::Deserialize(version, dsIn);
	Scene::DeserializeDataBlock(*this, dsIn, offsetof(Light, m_size));
}

void unirender::Light::DoFinalize(Scene &scene) {}
