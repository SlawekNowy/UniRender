/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __PR_CYCLES_CAMERA_HPP__
#define __PR_CYCLES_CAMERA_HPP__

#include "definitions.hpp"
#include "world_object.hpp"
#include <sharedutils/util_weak_handle.hpp>
#include <memory>
#include <optional>

namespace ccl {class Camera;};
class DataStream;
namespace raytracing
{
	class Camera;
	using PCamera = std::shared_ptr<Camera>;
	class Scene;
	class DLLRTUTIL Camera
		: public WorldObject,
		public std::enable_shared_from_this<Camera>
	{
	public:
		enum class CameraType : uint8_t
		{
			Perspective = 0,
			Orthographic,
			Panorama
		};
		enum class PanoramaType : uint8_t
		{
			Equirectangular = 0,
			FisheyeEquidistant,
			FisheyeEquisolid,
			Mirrorball
		};

		static PCamera Create(Scene &scene);
		util::WeakHandle<Camera> GetHandle();

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn);

		void SetResolution(uint32_t width,uint32_t height);
		void GetResolution(uint32_t &width,uint32_t &height) const;
		void SetFarZ(float farZ);
		void SetNearZ(float nearZ);
		void SetFOV(umath::Radian fov);
		void SetCameraType(CameraType type);
		void SetPanoramaType(PanoramaType type);
		void SetShutterTime(float timeInFrames);
		void SetRollingShutterEnabled(bool enabled);
		void SetRollingShutterDuration(float duration);

		void SetDepthOfFieldEnabled(bool enabled);
		void SetFocalDistance(float focalDistance);
		void SetApertureSize(float size);
		void SetApertureSizeFromFStop(float fstop,umath::Millimeter focalLength);
		void SetFOVFromFocalLength(umath::Millimeter focalLength,umath::Millimeter sensorSize);
		void SetBokehRatio(float ratio);
		void SetBladeCount(uint32_t numBlades);
		void SetBladesRotation(float rotation);

		float GetAspectRatio() const;
		float GetNearZ() const;
		float GetFarZ() const;
		virtual void DoFinalize() override;

		ccl::Camera *operator->();
		ccl::Camera *operator*();
	private:
		Camera(Scene &scene,ccl::Camera &cam);
		ccl::Camera &m_camera;

		bool m_dofEnabled = false;
	};
};

#endif
