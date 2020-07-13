/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
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

#pragma optimize("",off)
raytracing::PLight raytracing::Light::Create(Scene &scene)
{
	if(scene.GetRenderMode() == Scene::RenderMode::BakeAmbientOcclusion)
		return nullptr;
	auto *light = new ccl::Light{}; // Object will be removed automatically by cycles
	light->tfm = ccl::transform_identity();

	scene->lights.push_back(light);
	auto pLight = PLight{new Light{scene,*light}};
	scene.m_lights.push_back(pLight);
	return pLight;
}

raytracing::PLight raytracing::Light::Create(Scene &scene,DataStream &dsIn)
{
	auto light = Create(scene);
	light->Deserialize(dsIn);
	return light;
}

raytracing::Light::Light(Scene &scene,ccl::Light &light)
	: WorldObject{scene},m_light{light}
{}

util::WeakHandle<raytracing::Light> raytracing::Light::GetHandle()
{
	return util::WeakHandle<raytracing::Light>{shared_from_this()};
}

void raytracing::Light::SetType(Type type) {m_type = type;}

void raytracing::Light::SetConeAngles(umath::Radian innerAngle,umath::Radian outerAngle)
{
	m_spotInnerAngle = innerAngle;
	m_spotOuterAngle = outerAngle;
}

void raytracing::Light::SetColor(const Color &color)
{
	m_color = color.ToVector3();
	// Alpha is ignored
}
void raytracing::Light::SetIntensity(Lumen intensity) {m_intensity = intensity;}

void raytracing::Light::SetSize(float size) {m_size = size;}

void raytracing::Light::SetAxisU(const Vector3 &axisU) {m_axisU = axisU;}
void raytracing::Light::SetAxisV(const Vector3 &axisV) {m_axisV = axisV;}
void raytracing::Light::SetSizeU(float sizeU) {m_sizeU = sizeU;}
void raytracing::Light::SetSizeV(float sizeV) {m_sizeV = sizeV;}

void raytracing::Light::Serialize(DataStream &dsOut) const
{
	WorldObject::Serialize(dsOut);
	dsOut->Write(m_size);
	dsOut->Write(m_color);
	dsOut->Write(m_intensity);
	dsOut->Write(m_type);
	dsOut->Write(m_spotInnerAngle);
	dsOut->Write(m_spotOuterAngle);
	dsOut->Write(m_axisU);
	dsOut->Write(m_axisV);
	dsOut->Write(m_sizeU);
	dsOut->Write(m_sizeV);
	dsOut->Write(m_bRound);
}

void raytracing::Light::Deserialize(DataStream &dsIn)
{
	WorldObject::Deserialize(dsIn);
	m_size = dsIn->Read<decltype(m_size)>();
	m_color = dsIn->Read<decltype(m_color)>();
	m_intensity = dsIn->Read<decltype(m_intensity)>();
	m_type = dsIn->Read<decltype(m_type)>();
	m_spotInnerAngle = dsIn->Read<decltype(m_spotInnerAngle)>();
	m_spotOuterAngle = dsIn->Read<decltype(m_spotOuterAngle)>();
	m_axisU = dsIn->Read<decltype(m_axisU)>();
	m_axisV = dsIn->Read<decltype(m_axisV)>();
	m_sizeU = dsIn->Read<decltype(m_sizeU)>();
	m_sizeV = dsIn->Read<decltype(m_sizeV)>();
	m_bRound = dsIn->Read<decltype(m_bRound)>();
}

void raytracing::Light::DoFinalize()
{
	switch(m_type)
	{
	case Type::Spot:
		m_light.type = ccl::LightType::LIGHT_SPOT;
		break;
	case Type::Directional:
		m_light.type = ccl::LightType::LIGHT_DISTANT;
		break;
	case Type::Area:
		m_light.type = ccl::LightType::LIGHT_AREA;
		break;
	case Type::Background:
		m_light.type = ccl::LightType::LIGHT_BACKGROUND;
		break;
	case Type::Triangle:
		m_light.type = ccl::LightType::LIGHT_TRIANGLE;
		break;
	case Type::Point:
	default:
		m_light.type = ccl::LightType::LIGHT_POINT;
		break;
	}

	PShader shader = nullptr;
	switch(m_type)
	{
	case Type::Point:
	{
		shader = raytracing::Shader::Create<raytracing::ShaderGeneric>(GetScene(),"point_shader");
		break;
	}
	case Type::Spot:
	{
		shader = raytracing::Shader::Create<raytracing::ShaderGeneric>(GetScene(),"spot_shader");

		auto &rot = GetRotation();
		auto forward = uquat::forward(rot);
		m_light.spot_smooth = 1.f;
		m_light.dir = raytracing::Scene::ToCyclesNormal(forward);
		m_light.spot_smooth = (m_spotOuterAngle > 0.f) ? (1.f -m_spotInnerAngle /m_spotOuterAngle) : 1.f;
		m_light.spot_angle = m_spotOuterAngle;
		break;
	}
	case Type::Directional:
	{
		shader = raytracing::Shader::Create<raytracing::ShaderGeneric>(GetScene(),"distance_shader");

		auto &rot = GetRotation();
		auto forward = uquat::forward(rot);
		m_light.dir = raytracing::Scene::ToCyclesNormal(forward);
		break;
	}
	case Type::Area:
	{
		shader = raytracing::Shader::Create<raytracing::ShaderGeneric>(GetScene(),"area_shader");
		m_light.axisu = raytracing::Scene::ToCyclesNormal(m_axisU);
		m_light.axisv = raytracing::Scene::ToCyclesNormal(m_axisV);
		m_light.sizeu = raytracing::Scene::ToCyclesLength(m_sizeU);
		m_light.sizev = raytracing::Scene::ToCyclesLength(m_sizeV);
		m_light.round = m_bRound;

		auto &rot = GetRotation();
		auto forward = uquat::forward(rot);
		m_light.dir = raytracing::Scene::ToCyclesNormal(forward);
		break;
	}
	case Type::Background:
	{
		shader = raytracing::Shader::Create<raytracing::ShaderGeneric>(GetScene(),"background_shader");
		break;
	}
	case Type::Triangle:
	{
		shader = raytracing::Shader::Create<raytracing::ShaderGeneric>(GetScene(),"triangle_shader");
		break;
	}
	}

	auto cclShader = shader ? shader->GenerateCCLShader() : nullptr;
	if(cclShader)
	{
		auto nodeEmission = cclShader->AddEmissionNode();
		//nodeEmission->SetInputArgument<float>("strength",watt);
		//nodeEmission->SetInputArgument<ccl::float3>("color",ccl::float3{1.f,1.f,1.f});
		cclShader->Link(nodeEmission,cclShader->GetOutputNode().inSurface);

		m_light.shader = **cclShader;
	}

	auto lightType = (m_type == Type::Spot) ? util::pragma::LightType::Spot : (m_type == Type::Directional) ? util::pragma::LightType::Directional : util::pragma::LightType::Point;
	auto watt = util::pragma::light_intensity_to_watts(m_intensity,lightType);

	// Multiple importance sampling. It's disabled by default for some reason, but it's usually best to keep it on.
	m_light.use_mis = true;

	//static float lightIntensityFactor = 10.f;
	//watt *= lightIntensityFactor;

	watt *= GetScene().GetLightIntensityFactor();
	m_light.strength = ccl::float3{m_color.r,m_color.g,m_color.b} *watt;
	m_light.size = raytracing::Scene::ToCyclesLength(m_size);
	m_light.co = raytracing::Scene::ToCyclesPosition(GetPos());
	m_light.samples = 4;
	m_light.max_bounces = 1'024;
	m_light.map_resolution = 2'048;
	// Test
	/*m_light.strength = ccl::float3{0.984539f,1.f,0.75f} *40.f;
	m_light.size = 0.25f;
	m_light.max_bounces = 1'024;
	m_light.type = ccl::LightType::LIGHT_POINT;*/
}

ccl::Light *raytracing::Light::operator->() {return &m_light;}
ccl::Light *raytracing::Light::operator*() {return &m_light;}
#pragma optimize("",on)
