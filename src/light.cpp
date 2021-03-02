/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
*/

#include "util_raytracing/light.hpp"
#include "util_raytracing/scene.hpp"
#include "util_raytracing/shader.hpp"
#include "util_raytracing/ccl_shader.hpp"
#include <render/light.h>
#include <render/scene.h>
#include <render/nodes.h>
#include <mathutil/umath_lighting.hpp>
#include <sharedutils/util_pragma.hpp>
#include <sharedutils/datastream.h>
#include <udm.hpp>

#pragma optimize("",off)
unirender::PLight unirender::Light::Create()
{
	auto pLight = PLight{new Light{}};
	return pLight;
}

unirender::PLight unirender::Light::Create(udm::LinkedPropertyWrapper &prop)
{
	auto light = Create();
	light->Deserialize(prop);
	return light;
}

unirender::Light::Light()
	: WorldObject{}
{}

util::WeakHandle<unirender::Light> unirender::Light::GetHandle()
{
	return util::WeakHandle<unirender::Light>{shared_from_this()};
}

void unirender::Light::SetType(Type type) {m_type = type;}

void unirender::Light::SetConeAngles(umath::Radian innerAngle,umath::Radian outerAngle)
{
	m_spotInnerAngle = innerAngle;
	m_spotOuterAngle = outerAngle;
}

void unirender::Light::SetColor(const Color &color)
{
	m_color = color.ToVector3();
	// Alpha is ignored
}
void unirender::Light::SetIntensity(Lumen intensity) {m_intensity = intensity;}

void unirender::Light::SetSize(float size) {m_size = size;}

void unirender::Light::SetAxisU(const Vector3 &axisU) {m_axisU = axisU;}
void unirender::Light::SetAxisV(const Vector3 &axisV) {m_axisV = axisV;}
void unirender::Light::SetSizeU(float sizeU) {m_sizeU = sizeU;}
void unirender::Light::SetSizeV(float sizeV) {m_sizeV = sizeV;}

void unirender::Light::Serialize(udm::LinkedPropertyWrapper &prop) const
{
	WorldObject::Serialize(prop);

	prop["type"] = "light";
	prop["size"] = m_size;
	prop["color"] = m_color;
	prop["intensity"] = m_intensity;
	prop["type"] = m_type;
	prop["spot.innerConeAngle"] = m_spotInnerAngle;
	prop["spot.outerConeAngle"] = m_spotOuterAngle;
	prop["axisU"] = m_axisU;
	prop["axisV"] = m_axisV;
	prop["sizeU"] = m_sizeU;
	prop["sizeV"] = m_sizeV;
	prop["round"] = m_bRound;
	// m_flags
}

void unirender::Light::Deserialize(udm::LinkedPropertyWrapper &prop)
{
	WorldObject::Deserialize(prop);

	prop["size"](m_size);
	prop["color"](m_color);
	prop["intensity"](m_intensity);
	prop["type"](m_type);
	prop["spot.innerConeAngle"](m_spotInnerAngle);
	prop["spot.outerConeAngle"](m_spotOuterAngle);
	prop["axisU"](m_axisU);
	prop["axisV"](m_axisV);
	prop["sizeU"](m_sizeU);
	prop["sizeV"](m_sizeV);
	prop["round"](m_bRound);
}

void unirender::Light::DoFinalize(Scene &scene)
{

}
#pragma optimize("",on)
