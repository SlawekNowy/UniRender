/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#ifndef __PR_CYCLES_LIGHT_HPP__
#define __PR_CYCLES_LIGHT_HPP__

#include "definitions.hpp"
#include "world_object.hpp"
#include <sharedutils/util_pragma.hpp>
#include <mathutil/color.h>
#include <mathutil/uvec.h>
#include <memory>

namespace unirender
{
	using Lumen = float;
	class Light;
	using PLight = std::shared_ptr<Light>;
	class DLLRTUTIL Light
		: public WorldObject,public BaseObject,
		public std::enable_shared_from_this<Light>
	{
	public:
		enum class Flags : uint8_t
		{
			None = 0u
		};
		enum class Type : uint8_t
		{
			Point = 0u,
			Spot,
			Directional,

			Area,
			Background,
			Triangle
		};
		static PLight Create();
		static PLight Create(uint32_t version,DataStream &dsIn);
		util::WeakHandle<Light> GetHandle();

		void SetType(Type type);
		void SetConeAngle(umath::Degree outerAngle,umath::Fraction blendFraction);
		void SetColor(const Color &color);
		void SetIntensity(Lumen intensity);
		void SetSize(float size);
		virtual void DoFinalize(Scene &scene) override;

		void SetAxisU(const Vector3 &axisU);
		void SetAxisV(const Vector3 &axisV);
		void SetSizeU(float sizeU);
		void SetSizeV(float sizeV);

		void Serialize(DataStream &dsOut) const;
		void Deserialize(uint32_t version,DataStream &dsIn);

		Type GetType() const {return m_type;}
		float GetSize() const {return m_size;}
		const Vector3 &GetColor() const {return m_color;}
		Lumen GetIntensity() const {return m_intensity;}
		umath::Fraction GetBlendFraction() const {return m_blendFraction;}
		umath::Degree GetOuterConeAngle() const {return m_spotOuterAngle;}
		const Vector3 &GetAxisU() const {return m_axisU;}
		const Vector3 &GetAxisV() const {return m_axisV;}
		float GetSizeU() const {return m_sizeU;}
		float GetSizeV() const {return m_sizeV;}
		bool IsRound() const {return m_bRound;}
		Flags GetFlags() const {return m_flags;}
	private:
		Light();

		// Note: All of these are automatically serialized/deserialized!
		// There must be no unserializable data after this point!
		float m_size = util::pragma::metres_to_units(1.f);
		Vector3 m_color = {1.f,1.f,1.f};
		Lumen m_intensity = 1'600.f;
		Type m_type = Type::Point;
		umath::Fraction m_blendFraction = 0.f;
		umath::Degree m_spotOuterAngle = 0.f;

		Vector3 m_axisU = {};
		Vector3 m_axisV = {};
		float m_sizeU = util::pragma::metres_to_units(1.f);
		float m_sizeV = util::pragma::metres_to_units(1.f);
		bool m_bRound = false;
		Flags m_flags = Flags::None;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(unirender::Light::Flags)

#endif
