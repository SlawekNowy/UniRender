/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
*/

#include "util_raytracing/data_value.hpp"
#include <udm.hpp>

#pragma optimize("",off)
void unirender::DataValue::Serialize(udm::LinkedPropertyWrapper &prop) const
{
	prop["type"] = type;
	if(value == nullptr)
	{
		prop.Add("value",udm::Type::Nil);
		return;
	}
	switch(type)
	{
	case SocketType::Bool:
		prop["value"] = *static_cast<STBool*>(value.get());
		break;
	case SocketType::Float:
		prop["value"] = *static_cast<STFloat*>(value.get());
		break;
	case SocketType::Int:
		prop["value"] = *static_cast<STInt*>(value.get());
		break;
	case SocketType::UInt:
		prop["value"] = *static_cast<STUInt*>(value.get());
		break;
	case SocketType::Color:
		prop["value"] = *static_cast<STColor*>(value.get());
		break;
	case SocketType::Vector:
		prop["value"] = *static_cast<STVector*>(value.get());
		break;
	case SocketType::Point:
		prop["value"] = *static_cast<STPoint*>(value.get());
		break;
	case SocketType::Normal:
		prop["value"] = *static_cast<STNormal*>(value.get());
		break;
	case SocketType::Point2:
		prop["value"] = *static_cast<STPoint2*>(value.get());
		break;
	case SocketType::Enum:
		prop["value"] = *static_cast<STEnum*>(value.get());
		break;
	case SocketType::Transform:
		prop["value"] = glm::transpose(*static_cast<STTransform*>(value.get()));
		break;
	case SocketType::String:
		prop["value"] = *static_cast<STString*>(value.get());
		break;
	case SocketType::FloatArray:
	{
		auto &v = *static_cast<STFloatArray*>(value.get());
		prop["value"] = udm::compress_lz4_blob(v);
		break;
	}
	case SocketType::ColorArray:
	{
		auto &v = *static_cast<STColorArray*>(value.get());
		prop["value"] = udm::compress_lz4_blob(v);
		break;
	}
	case SocketType::Closure:
	case SocketType::Node:
		break;
	}
	static_assert(umath::to_integral(SocketType::Count) == 16);
}
unirender::DataValue unirender::DataValue::Deserialize(udm::LinkedPropertyWrapper &prop)
{
	auto type = SocketType::Invalid;
	prop["type"](type);
	auto value = prop["value"];
	if(!value)
		return DataValue{type,nullptr};
	switch(type)
	{
	case SocketType::Bool:
		return DataValue::Create<STBool,SocketType::Bool>(value.ToValue<STBool>({}));
	case SocketType::Float:
		return DataValue::Create<STFloat,SocketType::Float>(value.ToValue<STFloat>({}));
	case SocketType::Int:
		return DataValue::Create<STInt,SocketType::Int>(value.ToValue<STInt>({}));
	case SocketType::UInt:
		return DataValue::Create<STUInt,SocketType::UInt>(value.ToValue<STUInt>({}));
	case SocketType::Color:
		return DataValue::Create<STColor,SocketType::Color>(value.ToValue<STColor>({}));
	case SocketType::Vector:
		return DataValue::Create<STVector,SocketType::Vector>(value.ToValue<STVector>({}));
	case SocketType::Point:
		return DataValue::Create<STPoint,SocketType::Point>(value.ToValue<STPoint>({}));
	case SocketType::Normal:
		return DataValue::Create<STNormal,SocketType::Normal>(value.ToValue<STNormal>({}));
	case SocketType::Point2:
		return DataValue::Create<STPoint2,SocketType::Point2>(value.ToValue<STPoint2>({}));
	case SocketType::Enum:
		return DataValue::Create<STEnum,SocketType::Enum>(value.ToValue<STEnum>({}));
	case SocketType::Transform:
		return DataValue::Create<STTransform,SocketType::Transform>(glm::transpose(value.ToValue<Mat3x4>({})));
	case SocketType::String:
		return DataValue::Create<STString,SocketType::String>(value.ToValue<STString>({}));
	case SocketType::FloatArray:
	{
		STFloatArray values {};
		value.GetBlobData(values);
		return DataValue::Create<STFloatArray,SocketType::Transform>(std::move(values));
	}
	case SocketType::ColorArray:
	{
		STColorArray values {};
		value.GetBlobData(values);
		return DataValue::Create<STColorArray,SocketType::Transform>(std::move(values));
	}
	case SocketType::Closure:
	case SocketType::Node:
		return DataValue{type,nullptr};
	}
}

std::string unirender::to_string(SocketType type)
{
	switch(type)
	{
	case SocketType::Bool:
		return "Bool";
	case SocketType::Float:
		return "Float";
	case SocketType::Int:
		return "Int";
	case SocketType::UInt:
		return "UInt";
	case SocketType::Color:
		return "Color";
	case SocketType::Vector:
		return "Vector";
	case SocketType::Point:
		return "Point";
	case SocketType::Normal:
		return "Normal";
	case SocketType::Point2:
		return "Point2";
	case SocketType::Closure:
		return "Closure";
	case SocketType::String:
		return "String";
	case SocketType::Enum:
		return "Enum";
	case SocketType::Transform:
		return "Transform";
	case SocketType::Node:
		return "Node";
	case SocketType::FloatArray:
		return "FloatArray";
	case SocketType::ColorArray:
		return "ColorArray";
	}
	return "Invalid";
}
std::optional<unirender::DataValue> unirender::convert(const void *value,SocketType srcType,SocketType dstType)
{
	if(is_convertible_to(srcType,dstType) == false)
		return {};
	std::optional<DataValue> dstDataValue {};
	switch(dstType)
	{
	case SocketType::Bool:
	{
		auto dstValue = convert<STBool>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STBool>(*dstValue)};
		break;
	}
	case SocketType::Float:
	{
		auto dstValue = convert<STFloat>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STFloat>(*dstValue)};
		break;
	}
	case SocketType::Int:
	{
		auto dstValue = convert<STInt>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STInt>(*dstValue)};
		break;
	}
	case SocketType::Enum:
	{
		auto dstValue = convert<STEnum>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STEnum>(*dstValue)};
		break;
	}
	case SocketType::UInt:
	{
		auto dstValue = convert<STUInt>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STUInt>(*dstValue)};
		break;
	}
	case SocketType::Color:
	{
		auto dstValue = convert<STColor>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STColor>(*dstValue)};
		break;
	}
	case SocketType::Vector:
	{
		auto dstValue = convert<STVector>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STVector>(*dstValue)};
		break;
	}
	case SocketType::Point:
	{
		auto dstValue = convert<STPoint>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STPoint>(*dstValue)};
		break;
	}
	case SocketType::Normal:
	{
		auto dstValue = convert<STNormal>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STNormal>(*dstValue)};
		break;
	}
	case SocketType::Point2:
	{
		auto dstValue = convert<STPoint2>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STPoint2>(*dstValue)};
		break;
	}
	case SocketType::String:
	{
		auto dstValue = convert<STString>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STString>(*dstValue)};
		break;
	}
	case SocketType::Transform:
	{
		auto dstValue = convert<STTransform>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STTransform>(*dstValue)};
		break;
	}
	case SocketType::FloatArray:
	{
		auto dstValue = convert<STFloatArray>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STFloatArray>(*dstValue)};
		break;
	}
	case SocketType::ColorArray:
	{
		auto dstValue = convert<STColorArray>(value,srcType);
		if(dstValue.has_value())
			dstDataValue = DataValue{dstType,std::make_shared<STColorArray>(*dstValue)};
		break;
	}
	}
	return dstDataValue;
}
#pragma optimize("",on)
