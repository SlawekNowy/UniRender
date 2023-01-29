/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#ifndef __RT_DATA_VALUE_HPP__
#define __RT_DATA_VALUE_HPP__

#include "definitions.hpp"
#include <memory>
#include <optional>
#include <functional>
#include <mathutil/uvec.h>
#include <mathutil/umat.h>
#include <sharedutils/datastream.h>

namespace unirender {
	using STBool = bool;
	using STFloat = float;
	using STInt = int32_t;
	using STUInt = uint32_t;
	using STColor = Vector3;
	using STVector = Vector3;
	using STPoint = Vector3;
	using STNormal = Vector3;
	using STPoint2 = Vector2;
	using STClosure = void;
	using STString = std::string;
	using STEnum = int32_t;
	using STTransform = Mat4x3;
	using STFloatArray = std::vector<STFloat>;
	using STColorArray = std::vector<STColor>;
	using STNode = void;
	enum class SocketType : uint8_t {
		Bool = 0,
		Float,
		Int,
		UInt,
		Color,
		Vector,
		Point,
		Normal,
		Point2,
		Closure,
		String,
		Enum,
		Transform,
		Node,
		FloatArray,
		ColorArray,
		Count,

		Invalid = std::numeric_limits<uint8_t>::max()
	};
	DLLRTUTIL std::string to_string(SocketType type);
	constexpr bool is_numeric_type(SocketType type)
	{
		switch(type) {
		case SocketType::Bool:
		case SocketType::Float:
		case SocketType::Int:
		case SocketType::UInt:
		case SocketType::Enum:
			return true;
		}
		return false;
	}
	constexpr bool is_vector_type(SocketType type)
	{
		switch(type) {
		case SocketType::Color:
		case SocketType::Vector:
		case SocketType::Point:
		case SocketType::Normal:
			return true;
		}
		return false;
	}
	constexpr bool is_vector2_type(SocketType type)
	{
		switch(type) {
		case SocketType::Point2:
			return true;
		}
		return false;
	}
	constexpr bool is_array_type(SocketType type)
	{
		switch(type) {
		case SocketType::FloatArray:
		case SocketType::ColorArray:
			return true;
		}
		return false;
	}
	template<typename T>
	constexpr bool is_convertible_to(SocketType type)
	{
		switch(type) {
		case SocketType::Bool:
			return std::is_convertible_v<T, STBool>;
		case SocketType::Float:
			return std::is_convertible_v<T, STFloat>;
		case SocketType::Int:
			return std::is_convertible_v<T, STInt>;
		case SocketType::Enum:
			return std::is_convertible_v<T, STEnum>;
		case SocketType::UInt:
			return std::is_convertible_v<T, STUInt>;
		case SocketType::Color:
			return std::is_convertible_v<T, STColor>;
		case SocketType::Vector:
			return std::is_convertible_v<T, STVector>;
		case SocketType::Point:
			return std::is_convertible_v<T, STPoint>;
		case SocketType::Normal:
			return std::is_convertible_v<T, STNormal>;
		case SocketType::Point2:
			return std::is_convertible_v<T, STPoint2>;
		case SocketType::String:
			return std::is_convertible_v<T, STString>;
		case SocketType::Transform:
			return std::is_convertible_v<T, STTransform>;
		case SocketType::FloatArray:
			return std::is_convertible_v<T, STFloatArray>;
		case SocketType::ColorArray:
			return std::is_convertible_v<T, STColorArray>;
		}
		return false;
	}
	template<typename T>
	constexpr bool is_convertible_from(SocketType type)
	{
		switch(type) {
		case SocketType::Bool:
			return std::is_convertible_v<STBool, T>;
		case SocketType::Float:
			return std::is_convertible_v<STFloat, T>;
		case SocketType::Int:
			return std::is_convertible_v<STInt, T>;
		case SocketType::Enum:
			return std::is_convertible_v<STEnum, T>;
		case SocketType::UInt:
			return std::is_convertible_v<STUInt, T>;
		case SocketType::Color:
			return std::is_convertible_v<STColor, T>;
		case SocketType::Vector:
			return std::is_convertible_v<STVector, T>;
		case SocketType::Point:
			return std::is_convertible_v<STPoint, T>;
		case SocketType::Normal:
			return std::is_convertible_v<STNormal, T>;
		case SocketType::Point2:
			return std::is_convertible_v<STPoint2, T>;
		case SocketType::String:
			return std::is_convertible_v<STString, T>;
		case SocketType::Transform:
			return std::is_convertible_v<STTransform, T>;
		case SocketType::FloatArray:
			return std::is_convertible_v<STFloatArray, T>;
		case SocketType::ColorArray:
			return std::is_convertible_v<STColorArray, T>;
		}
		return false;
	}
	constexpr bool is_convertible_to(SocketType src, SocketType dst)
	{
		switch(src) {
		case SocketType::Bool:
			return is_convertible_to<STBool>(dst);
		case SocketType::Float:
			return is_convertible_to<STFloat>(dst);
		case SocketType::Int:
			return is_convertible_to<STInt>(dst);
		case SocketType::Enum:
			return is_convertible_to<STEnum>(dst);
		case SocketType::UInt:
			return is_convertible_to<STUInt>(dst);
		case SocketType::Color:
			return is_convertible_to<STColor>(dst);
		case SocketType::Vector:
			return is_convertible_to<STVector>(dst);
		case SocketType::Point:
			return is_convertible_to<STPoint>(dst);
		case SocketType::Normal:
			return is_convertible_to<STNormal>(dst);
		case SocketType::Point2:
			return is_convertible_to<STPoint2>(dst);
		case SocketType::String:
			return is_convertible_to<STString>(dst);
		case SocketType::Transform:
			return is_convertible_to<STTransform>(dst);
		case SocketType::FloatArray:
			return is_convertible_to<STFloatArray>(dst);
		case SocketType::ColorArray:
			return is_convertible_to<STColorArray>(dst);
		}
		return false;
	}

	struct DataValue {
		template<typename T, SocketType type>
		static DataValue Create(const T &value)
		{
			switch(type) {
			case SocketType::Bool:
				if constexpr(is_convertible_to<T>(SocketType::Bool))
					return {type, std::make_shared<STBool>(static_cast<STBool>(value))};
				break;
			case SocketType::Float:
				if constexpr(is_convertible_to<T>(SocketType::Float))
					return {type, std::make_shared<STFloat>(static_cast<STFloat>(value))};
				break;
			case SocketType::Int:
				if constexpr(is_convertible_to<T>(SocketType::Int))
					return {type, std::make_shared<STInt>(static_cast<STInt>(value))};
				break;
			case SocketType::Enum:
				if constexpr(is_convertible_to<T>(SocketType::Enum))
					return {type, std::make_shared<STEnum>(static_cast<STEnum>(value))};
				break;
			case SocketType::UInt:
				if constexpr(is_convertible_to<T>(SocketType::UInt))
					return {type, std::make_shared<STUInt>(static_cast<STUInt>(value))};
				break;
			case SocketType::Color:
				if constexpr(is_convertible_to<T>(SocketType::Color))
					return {type, std::make_shared<STColor>(static_cast<STColor>(value))};
				break;
			case SocketType::Vector:
				if constexpr(is_convertible_to<T>(SocketType::Vector))
					return {type, std::make_shared<STVector>(static_cast<STVector>(value))};
				break;
			case SocketType::Point:
				if constexpr(is_convertible_to<T>(SocketType::Point))
					return {type, std::make_shared<STPoint>(static_cast<STPoint>(value))};
				break;
			case SocketType::Normal:
				if constexpr(is_convertible_to<T>(SocketType::Normal))
					return {type, std::make_shared<STNormal>(static_cast<STNormal>(value))};
				break;
			case SocketType::Point2:
				if constexpr(is_convertible_to<T>(SocketType::Point2))
					return {type, std::make_shared<STPoint2>(static_cast<STPoint2>(value))};
				break;
			case SocketType::String:
				if constexpr(is_convertible_to<T>(SocketType::String))
					return {type, std::make_shared<STString>(static_cast<STString>(value))};
				break;
			case SocketType::Transform:
				if constexpr(is_convertible_to<T>(SocketType::Transform))
					return {type, std::make_shared<STTransform>(static_cast<STTransform>(value))};
				break;
			case SocketType::FloatArray:
				if constexpr(is_convertible_to<T>(SocketType::FloatArray))
					return {type, std::make_shared<STFloatArray>(static_cast<STFloatArray>(value))};
				break;
			case SocketType::ColorArray:
				if constexpr(is_convertible_to<T>(SocketType::ColorArray))
					return {type, std::make_shared<STColorArray>(static_cast<STColorArray>(value))};
				break;
			}
			assert(false);
			return {};
		}
		static DataValue Deserialize(DataStream &dsIn);
		DataValue(SocketType type, const std::shared_ptr<void> &value) : type {type}, value {value} {}
		DataValue() = default;

		bool operator==(const DataValue &other) const { return type == other.type && value.get() == other.value.get(); }
		bool operator!=(const DataValue &other) const { return !operator==(other); }

		void Serialize(DataStream &dsOut) const;

		SocketType type = SocketType::Bool;
		std::shared_ptr<void> value = nullptr;

		template<typename T>
		std::optional<T> ToValue() const
		{
			return convert<T>(value.get(), type);
		}
	  private:
	};

	template<typename T>
	constexpr std::optional<T> convert(const void *value, SocketType valueType)
	{
		if(is_convertible_from<T>(valueType)) {
			switch(valueType) {
			case SocketType::Bool:
				{
					if constexpr(std::is_convertible_v<STBool, T>)
						return static_cast<T>(*static_cast<const STBool *>(value));
					break;
				}
			case SocketType::Float:
				{
					if constexpr(std::is_convertible_v<STFloat, T>)
						return static_cast<T>(*static_cast<const STFloat *>(value));
					break;
				}
			case SocketType::Int:
				{
					if constexpr(std::is_convertible_v<STInt, T>)
						return static_cast<T>(*static_cast<const STInt *>(value));
					break;
				}
			case SocketType::UInt:
				{
					if constexpr(std::is_convertible_v<STUInt, T>)
						return static_cast<T>(*static_cast<const STUInt *>(value));
					break;
				}
			case SocketType::Color:
				{
					if constexpr(std::is_convertible_v<STColor, T>)
						return static_cast<T>(*static_cast<const STColor *>(value));
					break;
				}
			case SocketType::Vector:
				{
					if constexpr(std::is_convertible_v<STVector, T>)
						return static_cast<T>(*static_cast<const STVector *>(value));
					break;
				}
			case SocketType::Point:
				{
					if constexpr(std::is_convertible_v<STPoint, T>)
						return static_cast<T>(*static_cast<const STPoint *>(value));
					break;
				}
			case SocketType::Normal:
				{
					if constexpr(std::is_convertible_v<STNormal, T>)
						return static_cast<T>(*static_cast<const STNormal *>(value));
					break;
				}
			case SocketType::Point2:
				{
					if constexpr(std::is_convertible_v<STPoint2, T>)
						return static_cast<T>(*static_cast<const STPoint2 *>(value));
					break;
				}
			case SocketType::String:
				{
					if constexpr(std::is_convertible_v<STString, T>)
						return static_cast<T>(*static_cast<const STString *>(value));
					break;
				}
			case SocketType::Enum:
				{
					if constexpr(std::is_convertible_v<STEnum, T>)
						return static_cast<T>(*static_cast<const STEnum *>(value));
					break;
				}
			case SocketType::Transform:
				{
					if constexpr(std::is_convertible_v<STTransform, T>)
						return static_cast<T>(*static_cast<const STTransform *>(value));
					break;
				}
			case SocketType::FloatArray:
				{
					if constexpr(std::is_convertible_v<STFloatArray, T>)
						return static_cast<T>(*static_cast<const STFloatArray *>(value));
					break;
				}
			case SocketType::ColorArray:
				{
					if constexpr(std::is_convertible_v<STColorArray, T>)
						return static_cast<T>(*static_cast<const STColorArray *>(value));
					break;
				}
			}
		}
		return std::optional<T> {};
	}

	DLLRTUTIL std::optional<DataValue> convert(const void *value, SocketType srcType, SocketType dstType);
};

#endif
