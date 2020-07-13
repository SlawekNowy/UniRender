/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/camera.hpp"
#include "util_raytracing/scene.hpp"
#include <mathutil/camera.hpp>
#include <sharedutils/datastream.h>
#include <render/camera.h>
#include <render/scene.h>

using namespace raytracing;

#pragma optimize("",off)
PCamera Camera::Create(Scene &scene)
{
	return PCamera{new Camera{scene,*scene->camera}};
}

Camera::Camera(Scene &scene,ccl::Camera &cam)
	: WorldObject{scene},m_camera{cam}
{
	cam.type = ccl::CameraType::CAMERA_PERSPECTIVE;
	cam.matrix = ccl::transform_identity();
}

util::WeakHandle<Camera> Camera::GetHandle()
{
	return util::WeakHandle<Camera>{shared_from_this()};
}

void Camera::Serialize(DataStream &dsOut) const
{
	WorldObject::Serialize(dsOut);
	dsOut->Write(m_camera.type);
	dsOut->Write(m_camera.matrix);
	dsOut->Write(m_camera.width);
	dsOut->Write(m_camera.height);
	dsOut->Write(m_camera.farclip);
	dsOut->Write(m_camera.nearclip);
	dsOut->Write(m_camera.fov);
	dsOut->Write(m_camera.focaldistance);
	dsOut->Write(m_camera.aperturesize);
	dsOut->Write(m_camera.aperture_ratio);
	dsOut->Write(m_camera.blades);
	dsOut->Write(m_camera.bladesrotation);
	dsOut->Write(m_camera.panorama_type);
	dsOut->Write(m_camera.shuttertime);
	dsOut->Write(m_camera.rolling_shutter_type);
	dsOut->Write(m_camera.rolling_shutter_duration);
	dsOut->Write(m_camera.fisheye_lens);
	dsOut->Write(m_camera.fisheye_fov);
	dsOut->Write(m_dofEnabled);
}
void Camera::Deserialize(DataStream &dsIn)
{
	WorldObject::Deserialize(dsIn);
	m_camera.type = dsIn->Read<decltype(m_camera.type)>();
	m_camera.matrix = dsIn->Read<decltype(m_camera.matrix)>();
	m_camera.width = dsIn->Read<decltype(m_camera.width)>();
	m_camera.height = dsIn->Read<decltype(m_camera.height)>();
	m_camera.farclip = dsIn->Read<decltype(m_camera.farclip)>();
	m_camera.nearclip = dsIn->Read<decltype(m_camera.nearclip)>();
	m_camera.fov = dsIn->Read<decltype(m_camera.fov)>();
	m_camera.focaldistance = dsIn->Read<decltype(m_camera.focaldistance)>();
	m_camera.aperturesize = dsIn->Read<decltype(m_camera.aperturesize)>();
	m_camera.aperture_ratio = dsIn->Read<decltype(m_camera.aperture_ratio)>();
	m_camera.blades = dsIn->Read<decltype(m_camera.blades)>();
	m_camera.bladesrotation = dsIn->Read<decltype(m_camera.bladesrotation)>();
	m_camera.panorama_type = dsIn->Read<decltype(m_camera.panorama_type)>();
	m_camera.shuttertime = dsIn->Read<decltype(m_camera.shuttertime)>();
	m_camera.rolling_shutter_type = dsIn->Read<decltype(m_camera.rolling_shutter_type)>();
	m_camera.rolling_shutter_duration = dsIn->Read<decltype(m_camera.rolling_shutter_duration)>();
	m_camera.fisheye_lens = dsIn->Read<decltype(m_camera.fisheye_lens)>();
	m_camera.fisheye_fov = dsIn->Read<decltype(m_camera.fisheye_fov)>();
	m_dofEnabled = dsIn->Read<decltype(m_dofEnabled)>();
}

void Camera::SetResolution(uint32_t width,uint32_t height)
{
	m_camera.width = width;
	m_camera.height = height;
}

void Camera::GetResolution(uint32_t &width,uint32_t &height) const
{
	width = m_camera.width;
	height = m_camera.height;
}

void Camera::SetFarZ(float farZ) {m_camera.farclip = Scene::ToCyclesLength(farZ);}
void Camera::SetNearZ(float nearZ) {m_camera.nearclip = Scene::ToCyclesLength(nearZ);}
void Camera::SetFOV(umath::Radian fov) {m_camera.fov = fov;}
float Camera::GetAspectRatio() const {return static_cast<float>(m_camera.width) /static_cast<float>(m_camera.height);}
float Camera::GetNearZ() const {return m_camera.nearclip;}
float Camera::GetFarZ() const {return m_camera.farclip;}
void Camera::SetCameraType(CameraType type)
{
	switch(type)
	{
	case CameraType::Perspective:
		m_camera.type = ccl::CameraType::CAMERA_PERSPECTIVE;
		break;
	case CameraType::Orthographic:
		m_camera.type = ccl::CameraType::CAMERA_ORTHOGRAPHIC;
		break;
	case CameraType::Panorama:
		m_camera.type = ccl::CameraType::CAMERA_PANORAMA;
		break;
	}
}
void Camera::SetDepthOfFieldEnabled(bool enabled) {m_dofEnabled = enabled;}
void Camera::SetFocalDistance(float focalDistance) {m_camera.focaldistance = Scene::ToCyclesLength(focalDistance);}
void Camera::SetApertureSize(float size) {m_camera.aperturesize = size;}
void Camera::SetBokehRatio(float ratio) {m_camera.aperture_ratio = ratio;}
void Camera::SetBladeCount(uint32_t numBlades) {m_camera.blades = numBlades;}
void Camera::SetBladesRotation(float rotation) {m_camera.bladesrotation = rotation;}
void Camera::SetApertureSizeFromFStop(float fstop,umath::Millimeter focalLength)
{
	SetApertureSize(umath::camera::calc_aperture_size_from_fstop(fstop,focalLength,m_camera.type == ccl::CameraType::CAMERA_ORTHOGRAPHIC));
}
void Camera::SetFOVFromFocalLength(umath::Millimeter focalLength,umath::Millimeter sensorSize)
{
	SetFOV(umath::camera::calc_fov_from_lens(sensorSize,focalLength,GetAspectRatio()));
}
void Camera::SetPanoramaType(PanoramaType type)
{
	switch(type)
	{
	case PanoramaType::Equirectangular:
		m_camera.panorama_type = ccl::PanoramaType::PANORAMA_EQUIRECTANGULAR;
		break;
	case PanoramaType::FisheyeEquidistant:
		m_camera.panorama_type = ccl::PanoramaType::PANORAMA_FISHEYE_EQUIDISTANT;
		break;
	case PanoramaType::FisheyeEquisolid:
		m_camera.panorama_type = ccl::PanoramaType::PANORAMA_FISHEYE_EQUISOLID;
		break;
	case PanoramaType::Mirrorball:
		m_camera.panorama_type = ccl::PanoramaType::PANORAMA_MIRRORBALL;
		break;
	}
}
void Camera::SetShutterTime(float timeInFrames) {m_camera.shuttertime = timeInFrames;}
void Camera::SetRollingShutterEnabled(bool enabled)
{
	m_camera.rolling_shutter_type = enabled ? ccl::Camera::RollingShutterType::ROLLING_SHUTTER_TOP : ccl::Camera::RollingShutterType::ROLLING_SHUTTER_NONE;
}
void Camera::SetRollingShutterDuration(float duration)
{
	m_camera.rolling_shutter_duration = duration;
}

void Camera::DoFinalize()
{
#ifdef ENABLE_MOTION_BLUR_TEST
	SetShutterTime(1.f);
#endif

	if(m_dofEnabled == false)
		m_camera.aperturesize = 0.f;
	if(m_camera.type == ccl::CameraType::CAMERA_PANORAMA)
	{
		auto rot = GetRotation();
		switch(m_camera.panorama_type)
		{
		case ccl::PanoramaType::PANORAMA_MIRRORBALL:
			rot *= uquat::create(EulerAngles{-90.f,0.f,0.f});
			break;
		case ccl::PanoramaType::PANORAMA_FISHEYE_EQUISOLID:
			m_camera.fisheye_lens = 10.5f;
			m_camera.fisheye_fov = 180.f;
			// No break is intentional!
		default:
			rot *= uquat::create(EulerAngles{-90.f,-90.f,0.f});
			break;
		}
		SetRotation(rot);
	}

	m_camera.matrix = Scene::ToCyclesTransform(GetPose());
	m_camera.compute_auto_viewplane();

	m_camera.need_update = true;
	m_camera.update(*GetScene());
}

ccl::Camera *Camera::operator->() {return &m_camera;}
ccl::Camera *Camera::operator*() {return &m_camera;}
#pragma optimize("",on)
