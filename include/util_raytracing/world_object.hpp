/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __PR_CYCLES_WORLD_OBJECT_HPP__
#define __PR_CYCLES_WORLD_OBJECT_HPP__

#include "definitions.hpp"
#include "scene_object.hpp"
#include <mathutil/uvec.h>
#include <mathutil/transform.hpp>

class DataStream;
namespace raytracing
{
	class WorldObject;
	using PWorldObject = std::shared_ptr<WorldObject>;
	class DLLRTUTIL WorldObject
		: public SceneObject
	{
	public:
		void SetPos(const Vector3 &pos);
		const Vector3 &GetPos() const;

		void SetRotation(const Quat &rot);
		const Quat &GetRotation() const;

		void SetScale(const Vector3 &scale);
		const Vector3 &GetScale() const;

		umath::ScaledTransform &GetPose();
		const umath::ScaledTransform &GetPose() const;

		void Serialize(DataStream &dsOut) const;
		void Deserialize(uint32_t version,DataStream &dsIn);
	protected:
		WorldObject(Scene &scene);
	private:
		umath::ScaledTransform m_pose = {};
	};
};

#endif
