/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include "definitions.hpp"
#include <mathutil/umath.h>
#include <sharedutils/util_weak_handle.hpp>
#include <memory>
#include <optional>

export module pragma.scenekit:camera;

import :world_object;
import :scene_object;

export namespace pragma::scenekit {
	class Camera;
	using PCamera = std::shared_ptr<Camera>;
	class Scene;
	class DLLRTUTIL Camera : public WorldObject, public SceneObject, public std::enable_shared_from_this<Camera> {
	  public:
		enum class CameraType : uint8_t { Perspective = 0, Orthographic, Panorama };
		enum class PanoramaType : uint8_t { Equirectangular = 0, FisheyeEquidistant, FisheyeEquisolid, Mirrorball };

		static PCamera Create(Scene &scene);
		util::WeakHandle<Camera> GetHandle();

		void SetInterocularDistance(umath::Millimeter dist);
		void SetEquirectangularHorizontalRange(umath::Degree range);
		void SetEquirectangularVerticalRange(umath::Degree range);
		void SetStereoscopic(bool stereo);

		void Serialize(DataStream &dsOut) const;
		void Deserialize(uint32_t version, DataStream &dsIn);

		void SetResolution(uint32_t width, uint32_t height);
		void GetResolution(uint32_t &width, uint32_t &height) const;
		void SetFarZ(umath::Meter farZ);
		void SetNearZ(umath::Meter nearZ);
		void SetFOV(umath::Degree fov);
		void SetCameraType(CameraType type);
		void SetPanoramaType(PanoramaType type);

		void SetDepthOfFieldEnabled(bool enabled);
		void SetFocalDistance(umath::Meter focalDistance);
		void SetApertureSize(float size);
		void SetApertureSizeFromFStop(float fstop, umath::Millimeter focalLength);
		void SetFOVFromFocalLength(umath::Millimeter focalLength, umath::Millimeter sensorSize);
		void SetBokehRatio(float ratio);
		void SetBladeCount(uint32_t numBlades);
		void SetBladesRotation(umath::Degree rotation);

		CameraType GetType() const { return m_type; }
		uint32_t GetWidth() const { return m_width; }
		uint32_t GetHeight() const { return m_height; }
		umath::Meter GetNearZ() const { return m_nearZ; }
		umath::Meter GetFarZ() const { return m_farZ; }
		umath::Degree GetFov() const { return m_fov; }
		umath::Meter GetFocalDistance() const { return m_focalDistance; }
		float GetApertureSize() const { return m_apertureSize; }
		float GetApertureRatio() const { return m_apertureRatio; }
		uint32_t GetBladeCount() const { return m_numBlades; }
		umath::Degree GetBladesRotation() const { return m_bladesRotation; }
		PanoramaType GetPanoramaType() const { return m_panoramaType; }
		bool IsDofEnabled() const { return m_dofEnabled; }
		bool IsStereoscopic() const;
		float GetInterocularDistance() const { return m_interocularDistance; }
		float GetAspectRatio() const;
		umath::Degree GetLongitudeMin() const { return m_longitudeMin; }
		umath::Degree GetLongitudeMax() const { return m_longitudeMax; }
		umath::Degree GetLatitudeMin() const { return m_latitudeMin; }
		umath::Degree GetLatitudeMax() const { return m_latitudeMax; }
	  private:
		Camera(Scene &scene);

		// Note: All of these are automatically serialized/deserialized!
		// There must be no unserializable data after this point!
		CameraType m_type = CameraType::Perspective;
		uint32_t m_width = 1'024;
		uint32_t m_height = 512;
		umath::Meter m_nearZ = 0.1f;
		umath::Meter m_farZ = 1'000.f;
		umath::Degree m_fov = 39.6f;
		umath::Meter m_focalDistance = 10.f;
		float m_apertureSize = 0.f;
		float m_apertureRatio = 1.f;
		uint32_t m_numBlades = 3;
		umath::Degree m_bladesRotation = 0;
		PanoramaType m_panoramaType = PanoramaType::Equirectangular;
		umath::Millimeter m_interocularDistance = 65.f;
		umath::Degree m_longitudeMin = -90.f;
		umath::Degree m_longitudeMax = 90.f;
		umath::Degree m_latitudeMin = -90.f;
		umath::Degree m_latitudeMax = 90.f;
		bool m_dofEnabled = false;
		bool m_stereoscopic = false;
	};
};
