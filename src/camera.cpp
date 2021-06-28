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

using namespace unirender;

#pragma optimize("",off)
PCamera Camera::Create(Scene &scene)
{
	return PCamera{new Camera{scene}};
}

Camera::Camera(Scene &scene)
	: WorldObject{},SceneObject{scene}
{}

util::WeakHandle<Camera> Camera::GetHandle()
{
	return util::WeakHandle<Camera>{shared_from_this()};
}

void Camera::Serialize(DataStream &dsOut) const
{
	WorldObject::Serialize(dsOut);
	Scene::SerializeDataBlock(*this,dsOut,offsetof(Camera,m_type));
}
void Camera::Deserialize(uint32_t version,DataStream &dsIn)
{
	WorldObject::Deserialize(version,dsIn);
	Scene::DeserializeDataBlock(*this,dsIn,offsetof(Camera,m_type));
}

void Camera::SetInterocularDistance(umath::Millimeter dist) {m_interocularDistance = dist;}
void Camera::SetEquirectangularHorizontalRange(umath::Degree range)
{
	m_longitudeMin = -range /2.f;
	m_longitudeMax = range /2.f;
}
void Camera::SetEquirectangularVerticalRange(umath::Degree range)
{
	m_latitudeMin = -range /2.f;
	m_latitudeMax = range /2.f;
}
void Camera::SetStereoscopic(bool stereo) {m_stereoscopic = stereo;}
bool Camera::IsStereoscopic() const {return m_stereoscopic && m_type == CameraType::Panorama;}

void Camera::SetResolution(uint32_t width,uint32_t height)
{
	m_width = width;
	m_height = height;
}

void Camera::GetResolution(uint32_t &width,uint32_t &height) const
{
	width = m_width;
	height = m_height;
}

void Camera::SetFarZ(umath::Meter farZ) {m_farZ = farZ;}
void Camera::SetNearZ(umath::Meter nearZ) {m_nearZ = nearZ;}
void Camera::SetFOV(umath::Degree fov) {m_fov = fov;}
float Camera::GetAspectRatio() const {return static_cast<float>(m_width) /static_cast<float>(m_height);}
void Camera::SetCameraType(CameraType type) {m_type = type;}
void Camera::SetDepthOfFieldEnabled(bool enabled) {m_dofEnabled = enabled;}
void Camera::SetFocalDistance(umath::Meter focalDistance) {m_focalDistance = focalDistance;}
void Camera::SetApertureSize(float size) {m_apertureSize = size;}
void Camera::SetBokehRatio(float ratio) {m_apertureRatio = ratio;}
void Camera::SetBladeCount(uint32_t numBlades) {m_numBlades = numBlades;}
void Camera::SetBladesRotation(umath::Degree rotation) {m_bladesRotation = rotation;}
void Camera::SetApertureSizeFromFStop(float fstop,umath::Millimeter focalLength)
{
	SetApertureSize(umath::camera::calc_aperture_size_from_fstop(fstop,focalLength,m_type == CameraType::Orthographic));
}
void Camera::SetFOVFromFocalLength(umath::Millimeter focalLength,umath::Millimeter sensorSize)
{
	SetFOV(umath::camera::calc_fov_from_lens(sensorSize,focalLength,GetAspectRatio()));
}
void Camera::SetPanoramaType(PanoramaType type) {m_panoramaType = type;}
#pragma optimize("",on)
