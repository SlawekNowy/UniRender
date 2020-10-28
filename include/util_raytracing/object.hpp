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
#include <optional>
#include <functional>

namespace ccl {class Object;};
class DataStream;
namespace raytracing
{
	class Scene;
	class Mesh;
	using PMesh = std::shared_ptr<Mesh>;
	class Object;
	using PObject = std::shared_ptr<Object>;
	class ModelCache;
	class DLLRTUTIL Object
		: public WorldObject,public BaseObject,
		public std::enable_shared_from_this<Object>
	{
	public:
		enum class Flags : uint8_t
		{
			None = 0u,
			CCLObjectOwnedByScene = 1u
		};
		static PObject Create(Mesh &mesh);
		static PObject Create(uint32_t version,DataStream &dsIn,const std::function<PMesh(uint32_t)> &fGetMesh);
		virtual ~Object() override;
		util::WeakHandle<Object> GetHandle();
		virtual void DoFinalize(Scene &scene) override;

		const Mesh &GetMesh() const;
		Mesh &GetMesh();

		void Serialize(DataStream &dsOut,const std::function<std::optional<uint32_t>(const Mesh&)> &fGetMeshIndex) const;
		void Serialize(DataStream &dsOut,const std::unordered_map<const Mesh*,size_t> &meshToIndexTable) const;
		void Deserialize(uint32_t version,DataStream &dsIn,const std::function<PMesh(uint32_t)> &fGetMesh);

		const umath::Transform &GetMotionPose() const;
		void SetMotionPose(const umath::Transform &pose);

		void SetName(const std::string &name);
		const std::string &GetName() const;

		ccl::Object *operator->();
		const ccl::Object *operator->() const;
		ccl::Object *operator*();
		const ccl::Object *operator*() const;
	private:
		static PObject Create(Mesh *mesh);
		Object(ccl::Object &object,Mesh *mesh);
		ccl::Object &m_object;
		PMesh m_mesh = nullptr;
		Flags m_flags = Flags::None;
		std::string m_name;

		// TODO
		umath::Transform m_motionPose = {};
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(raytracing::Object::Flags)

#endif
