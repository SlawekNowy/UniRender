/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include "definitions.hpp"
#include <sharedutils/util.h>
#include <mathutil/uvec.h>
#include <mathutil/transform.hpp>

export module pragma.scenekit:world_object;

export namespace pragma::scenekit {
	class WorldObject;
	using PWorldObject = std::shared_ptr<WorldObject>;
	class DLLRTUTIL WorldObject {
	  public:
		virtual ~WorldObject() = default;
		void SetPos(const Vector3 &pos);
		const Vector3 &GetPos() const;

		void SetRotation(const Quat &rot);
		const Quat &GetRotation() const;

		void SetScale(const Vector3 &scale);
		const Vector3 &GetScale() const;

		umath::ScaledTransform &GetPose();
		const umath::ScaledTransform &GetPose() const;

		void SetUuid(const util::Uuid &uuid) { m_uuid = uuid; }
		const util::Uuid &GetUuid() const { return m_uuid; }

		void Serialize(DataStream &dsOut) const;
		void Deserialize(uint32_t version, DataStream &dsIn);
	  protected:
		WorldObject();
	  private:
		umath::ScaledTransform m_pose = {};
		util::Uuid m_uuid = {0, 0};
	};
};
