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
	: WorldObject{},SceneObject{scene},m_camera{cam}
{
	cam.set_camera_type(ccl::CameraType::CAMERA_PERSPECTIVE);
	cam.set_matrix(ccl::transform_identity());
	cam.set_interocular_distance(0.065);
}

util::WeakHandle<Camera> Camera::GetHandle()
{
	return util::WeakHandle<Camera>{shared_from_this()};
}

void Camera::Serialize(DataStream &dsOut) const
{
	WorldObject::Serialize(dsOut);
	dsOut->Write(m_camera.get_camera_type());
	dsOut->Write(m_camera.get_matrix());
	dsOut->Write(m_camera.get_full_width());
	dsOut->Write(m_camera.get_full_height());
	dsOut->Write(m_camera.get_farclip());
	dsOut->Write(m_camera.get_nearclip());
	dsOut->Write(m_camera.get_fov());
	dsOut->Write(m_camera.get_focaldistance());
	dsOut->Write(m_camera.get_aperturesize());
	dsOut->Write(m_camera.get_aperture_ratio());
	dsOut->Write(m_camera.get_blades());
	dsOut->Write(m_camera.get_bladesrotation());
	dsOut->Write(m_camera.get_panorama_type());
	dsOut->Write(m_camera.get_shuttertime());
	dsOut->Write(m_camera.get_rolling_shutter_type());
	dsOut->Write(m_camera.get_rolling_shutter_duration());
	dsOut->Write(m_camera.get_fisheye_lens());
	dsOut->Write(m_camera.get_fisheye_fov());
	dsOut->Write(m_dofEnabled);

	dsOut->Write(m_camera.get_interocular_distance());
	dsOut->Write(m_camera.get_longitude_min());
	dsOut->Write(m_camera.get_longitude_max());
	dsOut->Write(m_camera.get_latitude_min());
	dsOut->Write(m_camera.get_latitude_max());
	dsOut->Write(m_camera.get_use_spherical_stereo());
	dsOut->Write(m_stereoscopic);
}
void Camera::Deserialize(uint32_t version,DataStream &dsIn)
{
	WorldObject::Deserialize(version,dsIn);
	m_camera.set_camera_type(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_camera_type())>>>());
	m_camera.set_matrix(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_matrix())>>>());
	m_camera.set_full_width(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_full_width())>>>());
	m_camera.set_full_height(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_full_height())>>>());
	m_camera.set_farclip(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_farclip())>>>());
	m_camera.set_nearclip(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_nearclip())>>>());
	m_camera.set_fov(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_fov())>>>());
	m_camera.set_focaldistance(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_focaldistance())>>>());
	m_camera.set_aperturesize(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_aperturesize())>>>());
	m_camera.set_aperture_ratio(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_aperture_ratio())>>>());
	m_camera.set_blades(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_blades())>>>());
	m_camera.set_bladesrotation(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_bladesrotation())>>>());
	m_camera.set_panorama_type(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_panorama_type())>>>());
	m_camera.set_shuttertime(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_shuttertime())>>>());
	m_camera.set_rolling_shutter_type(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_rolling_shutter_type())>>>());
	m_camera.set_rolling_shutter_duration(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_rolling_shutter_duration())>>>());
	m_camera.set_fisheye_lens(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_fisheye_lens())>>>());
	m_camera.set_fisheye_fov(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_fisheye_fov())>>>());
	m_dofEnabled = dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_dofEnabled)>>>();

	if(version < 1)
		return;

	m_camera.set_interocular_distance(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_interocular_distance())>>>());
	m_camera.set_longitude_min(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_longitude_min())>>>());
	m_camera.set_longitude_max(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_longitude_max())>>>());
	m_camera.set_latitude_min(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_latitude_min())>>>());
	m_camera.set_latitude_max(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_latitude_max())>>>());
	m_camera.set_use_spherical_stereo(dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_camera.get_use_spherical_stereo())>>>());
	m_stereoscopic = dsIn->Read<std::remove_const_t<std::remove_reference_t<decltype(m_stereoscopic)>>>();
}

void Camera::SetInterocularDistance(umath::Meter dist) {m_camera.set_interocular_distance(dist);}
void Camera::SetEquirectangularHorizontalRange(umath::Degree range)
{
	range = umath::deg_to_rad(range);
	m_camera.set_longitude_min(-range /2.f);
	m_camera.set_longitude_max(range /2.f);
}
void Camera::SetEquirectangularVerticalRange(umath::Degree range)
{
	range = umath::deg_to_rad(range);
	m_camera.set_latitude_min(-range /2.f);
	m_camera.set_latitude_max(range /2.f);
}
void Camera::SetStereoscopic(bool stereo)
{
	m_camera.set_use_spherical_stereo(stereo);
	m_stereoscopic = stereo;
}
bool Camera::IsStereoscopic() const {return m_stereoscopic && m_camera.get_camera_type() == ccl::CameraType::CAMERA_PANORAMA;}
void Camera::SetStereoscopicEye(StereoEye eye)
{
	switch(eye)
	{
	case StereoEye::Left:
		m_camera.set_stereo_eye(ccl::Camera::StereoEye::STEREO_LEFT);
		break;
	case StereoEye::Right:
		m_camera.set_stereo_eye(ccl::Camera::StereoEye::STEREO_RIGHT);
		break;
	}
}

void Camera::SetResolution(uint32_t width,uint32_t height)
{
	m_camera.set_full_width(width);
	m_camera.set_full_height(height);
}

void Camera::GetResolution(uint32_t &width,uint32_t &height) const
{
	width = m_camera.get_full_width();
	height = m_camera.get_full_height();
}

void Camera::SetFarZ(float farZ) {m_camera.set_farclip(Scene::ToCyclesLength(farZ));}
void Camera::SetNearZ(float nearZ) {m_camera.set_nearclip(Scene::ToCyclesLength(nearZ));}
void Camera::SetFOV(umath::Radian fov) {m_camera.set_fov(fov);}
float Camera::GetAspectRatio() const {return static_cast<float>(m_camera.get_full_width()) /static_cast<float>(m_camera.get_full_height());}
float Camera::GetNearZ() const {return m_camera.get_nearclip();}
float Camera::GetFarZ() const {return m_camera.get_farclip();}
void Camera::SetCameraType(CameraType type)
{
	switch(type)
	{
	case CameraType::Perspective:
		m_camera.set_camera_type(ccl::CameraType::CAMERA_PERSPECTIVE);
		break;
	case CameraType::Orthographic:
		m_camera.set_camera_type(ccl::CameraType::CAMERA_ORTHOGRAPHIC);
		break;
	case CameraType::Panorama:
		m_camera.set_camera_type(ccl::CameraType::CAMERA_PANORAMA);
		break;
	}
}
void Camera::SetDepthOfFieldEnabled(bool enabled) {m_dofEnabled = enabled;}
void Camera::SetFocalDistance(float focalDistance) {m_camera.set_focaldistance(Scene::ToCyclesLength(focalDistance));}
void Camera::SetApertureSize(float size) {m_camera.set_aperturesize(size);}
void Camera::SetBokehRatio(float ratio) {m_camera.set_aperture_ratio(ratio);}
void Camera::SetBladeCount(uint32_t numBlades) {m_camera.set_blades(numBlades);}
void Camera::SetBladesRotation(float rotation) {m_camera.set_bladesrotation(rotation);}
void Camera::SetApertureSizeFromFStop(float fstop,umath::Millimeter focalLength)
{
	SetApertureSize(umath::camera::calc_aperture_size_from_fstop(fstop,focalLength,m_camera.get_camera_type() == ccl::CameraType::CAMERA_ORTHOGRAPHIC));
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
		m_camera.set_panorama_type(ccl::PanoramaType::PANORAMA_EQUIRECTANGULAR);
		break;
	case PanoramaType::FisheyeEquidistant:
		m_camera.set_panorama_type(ccl::PanoramaType::PANORAMA_FISHEYE_EQUIDISTANT);
		break;
	case PanoramaType::FisheyeEquisolid:
		m_camera.set_panorama_type(ccl::PanoramaType::PANORAMA_FISHEYE_EQUISOLID);
		break;
	case PanoramaType::Mirrorball:
		m_camera.set_panorama_type(ccl::PanoramaType::PANORAMA_MIRRORBALL);
		break;
	}
}
void Camera::SetShutterTime(float timeInFrames) {m_camera.set_shuttertime(timeInFrames);}
void Camera::SetRollingShutterEnabled(bool enabled)
{
	m_camera.set_rolling_shutter_type(enabled ? ccl::Camera::RollingShutterType::ROLLING_SHUTTER_TOP : ccl::Camera::RollingShutterType::ROLLING_SHUTTER_NONE);
}
void Camera::SetRollingShutterDuration(float duration)
{
	m_camera.set_rolling_shutter_duration(duration);
}

void Camera::DoFinalize(Scene &scene)
{
#ifdef ENABLE_MOTION_BLUR_TEST
	SetShutterTime(1.f);
#endif

	if(m_dofEnabled == false)
		m_camera.set_aperturesize(0.f);
	if(m_camera.get_camera_type() == ccl::CameraType::CAMERA_PANORAMA)
	{
		auto rot = GetRotation();
		switch(m_camera.get_panorama_type())
		{
		case ccl::PanoramaType::PANORAMA_MIRRORBALL:
			rot *= uquat::create(EulerAngles{-90.f,0.f,0.f});
			break;
		case ccl::PanoramaType::PANORAMA_FISHEYE_EQUISOLID:
			m_camera.set_fisheye_lens(10.5f);
			m_camera.set_fisheye_fov(180.f);
			// No break is intentional!
		default:
			rot *= uquat::create(EulerAngles{-90.f,-90.f,0.f});
			break;
		}
		SetRotation(rot);
	}

	m_camera.set_matrix(Scene::ToCyclesTransform(GetPose(),true));
	m_camera.compute_auto_viewplane();

	m_camera.update(*GetScene());
}

ccl::Camera *Camera::operator->() {return &m_camera;}
ccl::Camera *Camera::operator*() {return &m_camera;}
#pragma optimize("",on)
