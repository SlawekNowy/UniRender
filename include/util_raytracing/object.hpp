/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __PR_CYCLES_OBJECT_HPP__
#define __PR_CYCLES_OBJECT_HPP__

#include "definitions.hpp"
#include "world_object.hpp"
#include <mathutil/transform.hpp>
#include <memory>

namespace ccl {class Object;};
class DataStream;
namespace raytracing
{
	class Scene;
	class Mesh;
	using PMesh = std::shared_ptr<Mesh>;
	class Object;
	using PObject = std::shared_ptr<Object>;
	class DLLRTUTIL Object
		: public WorldObject,
		public std::enable_shared_from_this<Object>
	{
	public:
		static PObject Create(Scene &scene,Mesh &mesh);
		static PObject Create(Scene &scene,uint32_t version,DataStream &dsIn);
		util::WeakHandle<Object> GetHandle();
		virtual void DoFinalize() override;

		uint32_t GetId() const;
		const Mesh &GetMesh() const;
		Mesh &GetMesh();

		void Serialize(DataStream &dsOut) const;
		void Deserialize(uint32_t version,DataStream &dsIn);

		const umath::Transform &GetMotionPose() const;
		void SetMotionPose(const umath::Transform &pose);

		ccl::Object *operator->();
		ccl::Object *operator*();
	private:
		static PObject Create(Scene &scene,Mesh *mesh);
		Object(Scene &scene,ccl::Object &object,uint32_t objectId,Mesh *mesh);
		ccl::Object &m_object;
		uint32_t m_id = 0;
		PMesh m_mesh = nullptr;

		// TODO
		umath::Transform m_motionPose = {};
	};
};

#endif
