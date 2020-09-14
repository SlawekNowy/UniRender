/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/shader_nodes.hpp"
#include "util_raytracing/scene.hpp"
#include "util_raytracing/shader.hpp"
#include "util_raytracing/ccl_shader.hpp"
#include <render/nodes.h>
#include <render/shader.h>

#pragma optimize("",off)
std::size_t raytracing::SocketHasher::operator()(const Socket& k) const
{
	assert(k.IsNodeSocket());
	if(k.IsNodeSocket() == false)
		throw Exception{"Only non-concrete sockets can be hashed!"};
	std::string socketName;
	auto *node = k.GetNode(socketName);

	auto seed = util::hash_combine<NodeDesc*>(0u,node);
	seed = util::hash_combine<std::string>(seed,socketName);
	seed = util::hash_combine<bool>(seed,k.IsOutputSocket());

	return seed;
}

raytracing::Socket::Socket(NodeDesc &node,const std::string &socketName,bool output)
{
	m_nodeSocketRef.node = node.shared_from_this();
	m_nodeSocketRef.socketName = socketName;
	m_nodeSocketRef.output = output;
}
raytracing::Socket::Socket(const DataValue &value)
	: m_value{value}
{}
raytracing::Socket::Socket(float value)
	: Socket{DataValue::Create<decltype(value),SocketType::Float>(value)}
{}
raytracing::Socket::Socket(const Vector3 &value)
	: Socket{DataValue::Create<decltype(value),SocketType::Vector>(value)}
{}

raytracing::SocketType raytracing::Socket::GetType() const
{
	if(IsConcreteValue())
		return m_value->type;
	std::string socketName;
	auto *node = GetNode(socketName);
	auto *desc = node ? node->FindSocketDesc(*this) : nullptr;
	return desc ? desc->dataValue.type : SocketType::Invalid;
}
raytracing::Socket raytracing::Socket::operator-() const
{
	if(is_vector_type(GetType()))
		return Socket{Vector3{}} -*this;
	return Socket{0.f} -*this;
}
raytracing::Socket raytracing::Socket::operator+(float f) const {return operator+(Socket{f});}
raytracing::Socket raytracing::Socket::operator-(float f) const {return operator-(Socket{f});}
raytracing::Socket raytracing::Socket::operator*(float f) const {return operator*(Socket{f});}
raytracing::Socket raytracing::Socket::operator/(float f) const {return operator/(Socket{f});}
raytracing::Socket raytracing::Socket::operator%(float f) const {return operator%(Socket{f});}
raytracing::Socket raytracing::Socket::operator^(float f) const {return operator^(Socket{f});}
raytracing::Socket raytracing::Socket::operator<(float f) const {return operator<(Socket{f});}
raytracing::Socket raytracing::Socket::operator<=(float f) const {return operator<=(Socket{f});}
raytracing::Socket raytracing::Socket::operator>(float f) const {return operator>(Socket{f});}
raytracing::Socket raytracing::Socket::operator>=(float f) const {return operator>=(Socket{f});}
raytracing::Socket raytracing::Socket::ApplyOperator(const Socket &other,ccl::NodeMathType opType,std::optional<ccl::NodeVectorMathType> opTypeVec,float(*applyValue)(float,float)) const
{
	auto srcType = GetType();
	auto dstType = other.GetType();
	// Cases:
	// #1 float x float
	// #2 float x Vector
	// #3 Vector x float
	// #4 Vector x Vector
	// Each one can be concrete type or actual node socket input/output
	if(IsConcreteValue() && other.IsConcreteValue()) // Special case where we can apply the operation directly
	{
		auto applyVecValue = [&applyValue](const Vector3 &v0,const Vector3 &v1) -> Vector3 {
			return Vector3 {
				applyValue(v0.x,v1.x),
				applyValue(v0.y,v1.y),
				applyValue(v0.z,v1.z)
			};
		};
		if(is_vector_type(srcType))
		{
			auto valSrc = *GetValue()->ToValue<Vector3>();
			if(is_vector_type(dstType))
			{
				// Case #4
				auto valDst = *other.GetValue()->ToValue<Vector3>();
				return applyVecValue(valSrc,valDst);
			}
			if(is_numeric_type(dstType) == false)
				return 0.f; // Invalid case
			// Case #3
			auto valDst = *other.GetValue()->ToValue<float>();
			return applyVecValue(valSrc,{valDst,valDst,valDst});
		}
		if(is_vector_type(dstType))
		{
			if(is_numeric_type(srcType) == false)
				return 0.f; // Invalid case
			// Case #2
			auto valDst = *other.GetValue()->ToValue<Vector3>();
			auto valSrc = *GetValue()->ToValue<float>();
			return applyVecValue({valSrc,valSrc,valSrc},valDst);
		}
		// Case #1
		auto valSrc = GetValue()->ToValue<float>();
		auto valDst = other.GetValue()->ToValue<float>();
		if(valSrc.has_value() == false || valDst.has_value() == false)
			return 0.f; // Invalid case
		return applyValue(*valSrc,*valDst);
	}

	auto &ref = IsConcreteValue() ? other.m_nodeSocketRef : m_nodeSocketRef;
	if(ref.node.expired())
		return 0.f; // Invalid case
	auto node = ref.node.lock();
	auto *parent = node->GetParent();
	if(parent == nullptr)
		return 0.f; // Invalid case

	if(is_vector_type(srcType))
	{
		if(opTypeVec.has_value() == false)
			return 0.f; // Invalid case
		if(is_vector_type(dstType))
		{
			// Case #4
			auto &n = parent->AddVectorMathNode(*this,other,*opTypeVec);
			return *n.GetPrimaryOutputSocket();
		}
		if(is_numeric_type(dstType) == false)
			return 0.f; // Invalid case
		// Case #3
		auto &vDst = parent->AddNode(NODE_COMBINE_XYZ);
		parent->Link(other,vDst.GetInputSocket(nodes::combine_xyz::IN_X));
		parent->Link(other,vDst.GetInputSocket(nodes::combine_xyz::IN_Y));
		parent->Link(other,vDst.GetInputSocket(nodes::combine_xyz::IN_Z));
		
		auto &n = parent->AddVectorMathNode(*this,vDst,*opTypeVec);
		return *n.GetPrimaryOutputSocket();
	}
	if(is_vector_type(dstType))
	{
		if(opTypeVec.has_value() == false || is_numeric_type(srcType) == false)
			return 0.f; // Invalid case
		// Case #2
		auto &vSrc = parent->AddNode(NODE_COMBINE_XYZ);
		parent->Link(*this,vSrc.GetInputSocket(nodes::combine_xyz::IN_X));
		parent->Link(*this,vSrc.GetInputSocket(nodes::combine_xyz::IN_Y));
		parent->Link(*this,vSrc.GetInputSocket(nodes::combine_xyz::IN_Z));
		
		auto &n = parent->AddVectorMathNode(vSrc,other,*opTypeVec);
		return *n.GetPrimaryOutputSocket();
	}

	// Case #1
	return parent->AddMathNode(*this,other,opType);
}
raytracing::Socket raytracing::Socket::operator+(const Socket &socket) const {return ApplyOperator(socket,ccl::NodeMathType::NODE_MATH_ADD,ccl::NodeVectorMathType::NODE_VECTOR_MATH_ADD,[](float a,float b) -> float {return a +b;});}
raytracing::Socket raytracing::Socket::operator-(const Socket &socket) const {return ApplyOperator(socket,ccl::NodeMathType::NODE_MATH_SUBTRACT,ccl::NodeVectorMathType::NODE_VECTOR_MATH_SUBTRACT,[](float a,float b) -> float {return a -b;});}
raytracing::Socket raytracing::Socket::operator*(const Socket &socket) const {return ApplyOperator(socket,ccl::NodeMathType::NODE_MATH_MULTIPLY,ccl::NodeVectorMathType::NODE_VECTOR_MATH_MULTIPLY,[](float a,float b) -> float {return a *b;});}
raytracing::Socket raytracing::Socket::operator/(const Socket &socket) const {return ApplyOperator(socket,ccl::NodeMathType::NODE_MATH_DIVIDE,ccl::NodeVectorMathType::NODE_VECTOR_MATH_DIVIDE,[](float a,float b) -> float {return a /b;});}
raytracing::Socket raytracing::Socket::operator%(const Socket &socket) const {return ApplyOperator(socket,ccl::NodeMathType::NODE_MATH_MODULO,ccl::NodeVectorMathType::NODE_VECTOR_MATH_MODULO,[](float a,float b) -> float {return fmodf(a,b);});}
raytracing::Socket raytracing::Socket::operator^(const Socket &socket) const {return ApplyOperator(socket,ccl::NodeMathType::NODE_MATH_POWER,{},[](float a,float b) -> float {return powf(a,b);});}

constexpr float COMPARISON_EPSILON = 0.00001f;
raytracing::Socket raytracing::Socket::ApplyComparisonOperator(const Socket &other,bool(*op)(float,float),Socket(*opNode)(GroupNodeDesc&,const Socket&,const Socket&)) const
{
	if(IsConcreteValue() && other.IsConcreteValue())
	{
		auto valSrc = GetValue()->ToValue<float>();
		auto valDst = other.GetValue()->ToValue<float>();
		if(valSrc.has_value() == false || valDst.has_value() == false)
			return {}; // Invalid case
		return op(*valSrc,*valDst);
	}
	auto &ref = IsConcreteValue() ? other.m_nodeSocketRef : m_nodeSocketRef;
	if(ref.node.expired())
		return 0.f; // Invalid case
	auto node = ref.node.lock();
	auto *parent = node->GetParent();
	if(parent == nullptr)
		return 0.f; // Invalid case
	return opNode(*parent,*this,other);
}
raytracing::Socket raytracing::Socket::operator<(const Socket &socket) const
{
	return ApplyComparisonOperator(socket,[](float a,float b) -> bool {return a < b;},[](GroupNodeDesc &node,const Socket &a,const Socket &b) -> Socket {
		return node.AddMathNode(a,b,ccl::NodeMathType::NODE_MATH_LESS_THAN);
	});
}
raytracing::Socket raytracing::Socket::operator<=(const Socket &socket) const
{
	return ApplyComparisonOperator(socket,[](float a,float b) -> bool {return a <= b;},[](GroupNodeDesc &node,const Socket &a,const Socket &b) -> Socket {
		return node.AddMathNode(a,b +COMPARISON_EPSILON,ccl::NodeMathType::NODE_MATH_LESS_THAN);
	});
}
raytracing::Socket raytracing::Socket::operator>(const Socket &socket) const
{
	return ApplyComparisonOperator(socket,[](float a,float b) -> bool {return a > b;},[](GroupNodeDesc &node,const Socket &a,const Socket &b) -> Socket {
		return node.AddMathNode(a,b,ccl::NodeMathType::NODE_MATH_GREATER_THAN);
	});
}
raytracing::Socket raytracing::Socket::operator>=(const Socket &socket) const
{
	return ApplyComparisonOperator(socket,[](float a,float b) -> bool {return a >= b;},[](GroupNodeDesc &node,const Socket &a,const Socket &b) -> Socket {
		return node.AddMathNode(a,b -COMPARISON_EPSILON,ccl::NodeMathType::NODE_MATH_GREATER_THAN);
	});
}

bool raytracing::Socket::operator==(const Socket &other) const
{
	auto fOptionalEqual = [](const std::optional<DataValue> &a,const std::optional<DataValue> &b) -> bool {
		if(a.has_value() != b.has_value())
			return false;
		if(a.has_value() == false)
			return true;
		return *a == *b;
	};
	return fOptionalEqual(m_value,other.m_value) && m_nodeSocketRef.node.lock().get() == other.m_nodeSocketRef.node.lock().get() && m_nodeSocketRef.socketName == other.m_nodeSocketRef.socketName;
}
bool raytracing::Socket::operator!=(const Socket &other) const {return !operator==(other);}

void raytracing::Socket::Link(const Socket &other)
{
	if(IsConcreteValue() && other.IsConcreteValue())
		throw Exception{"Cannot link two concrete sockets!"};
	auto *node = GetNode();
	if(node == nullptr)
		node = other.GetNode();
	auto *parent = node ? node->GetParent() : nullptr;
	if(parent == nullptr)
		return;
	parent->Link(*this,other);
}
std::string raytracing::Socket::ToString() const
{
	std::string str = "Socket[" +to_string(GetType()) +"]";
	if(IsConcreteValue())
	{
		str += "[";
		if(m_value->value == nullptr)
			str += "NULL";
		else
		{
			switch(m_value->type)
			{
			case raytracing::SocketType::Bool:
				str += std::to_string(*static_cast<raytracing::STBool*>(m_value->value.get()));
				break;
			case raytracing::SocketType::Float:
				str += std::to_string(*static_cast<raytracing::STFloat*>(m_value->value.get()));
				break;
			case raytracing::SocketType::Int:
				str += std::to_string(*static_cast<raytracing::STInt*>(m_value->value.get()));
				break;
			case raytracing::SocketType::UInt:
				str += std::to_string(*static_cast<raytracing::STUInt*>(m_value->value.get()));
				break;
			case raytracing::SocketType::Color:
				str += uvec::to_string(static_cast<raytracing::STColor*>(m_value->value.get()));
				break;
			case raytracing::SocketType::Vector:
				str += uvec::to_string(static_cast<raytracing::STVector*>(m_value->value.get()));
				break;
			case raytracing::SocketType::Point:
				str += uvec::to_string(static_cast<raytracing::STPoint*>(m_value->value.get()));
				break;
			case raytracing::SocketType::Normal:
				str += uvec::to_string(static_cast<raytracing::STNormal*>(m_value->value.get()));
				break;
			case raytracing::SocketType::Point2:
			{
				std::stringstream ss;
				ss<<*static_cast<raytracing::STPoint2*>(m_value->value.get());
				str += ss.str();
				break;
			}
			case raytracing::SocketType::String:
				str += *static_cast<raytracing::STString*>(m_value->value.get());
				break;
			case raytracing::SocketType::Enum:
				str += std::to_string(*static_cast<raytracing::STEnum*>(m_value->value.get()));
				break;
			case raytracing::SocketType::Transform:
			{
				std::stringstream ss;
				auto &t = *static_cast<raytracing::STTransform*>(m_value->value.get());
				ss<<t[0][0]<<','<<t[0][1]<<','<<t[0][2]<<','<<t[1][0]<<','<<t[1][1]<<','<<t[1][2]<<','<<t[2][0]<<','<<t[2][1]<<','<<t[2][2]<<','<<t[3][0]<<','<<t[3][1]<<','<<t[3][2];
				str += ss.str();
				break;
			}
			case raytracing::SocketType::FloatArray:
			{
				str += '{';
				auto first = true;
				for(auto &v : *static_cast<raytracing::STFloatArray*>(m_value->value.get()))
				{
					if(first)
						first = false;
					else
						str += ',';
					str += std::to_string(v);
				}
				str += '}';
				break;
			}
			case raytracing::SocketType::ColorArray:
			{
				str += '{';
				auto first = true;
				for(auto &v : *static_cast<raytracing::STColorArray*>(m_value->value.get()))
				{
					if(first)
						first = false;
					else
						str += ',';
					str += std::to_string(v.r) +' ' +std::to_string(v.g) +' ' +std::to_string(v.b);
				}
				str += '}';
				break;
			}
			}
			static_assert(umath::to_integral(raytracing::SocketType::Count) == 16);
		}
		str += "]";
	}
	else
	{
		auto *node = GetNode();
		str += "[";
		str += node ? node->ToString() : "NULL";
		str += "]";
		
		str += "[";
		str += m_nodeSocketRef.socketName;
		str += "]";
	}
	return str;
}

bool raytracing::Socket::IsValid() const {return IsConcreteValue() || m_nodeSocketRef.node.expired() == false;}
bool raytracing::Socket::IsConcreteValue() const {return m_value.has_value();}
bool raytracing::Socket::IsNodeSocket() const {return !IsConcreteValue();}
bool raytracing::Socket::IsOutputSocket() const {return IsConcreteValue() || m_nodeSocketRef.output;}

raytracing::NodeDesc *raytracing::Socket::GetNode(std::string &outSocketName) const
{
	auto *node = GetNode();
	if(node)
		outSocketName = m_nodeSocketRef.socketName;
	return node;
}
raytracing::NodeDesc *raytracing::Socket::GetNode() const
{
	return m_nodeSocketRef.node.lock().get();
}
std::optional<raytracing::DataValue> raytracing::Socket::GetValue() const {return m_value;}

void raytracing::Socket::Serialize(DataStream &dsOut,const std::unordered_map<const NodeDesc*,uint64_t> &nodeIndexTable) const
{
	if(IsValid() == false)
	{
		dsOut->Write<uint8_t>(0);
		return;
	}
	if(IsConcreteValue())
	{
		dsOut->Write<uint8_t>(1);
		m_value->Serialize(dsOut);
		return;
	}
	dsOut->Write<uint8_t>(2);
	auto it = nodeIndexTable.find(GetNode());
	assert(it != nodeIndexTable.end());
	if(it == nodeIndexTable.end())
		throw Exception{"Invalid parent node"};
	dsOut->Write<NodeIndex>(it->second);
	dsOut->WriteString(m_nodeSocketRef.socketName);
	dsOut->Write<bool>(m_nodeSocketRef.output);
}
void raytracing::Socket::Deserialize(GroupNodeDesc &parentGroupNode,DataStream &dsIn,const std::vector<const NodeDesc*> &nodeIndexTable)
{
	auto type = dsIn->Read<uint8_t>();
	switch(type)
	{
	case 0:
		return;
	case 1:
	{
		m_value = DataValue::Deserialize(dsIn);
		break;
	}
	case 2:
	{
		auto idx = dsIn->Read<NodeIndex>();
		assert(idx < nodeIndexTable.size());
		auto *node = nodeIndexTable.at(idx);
		if(node == nullptr)
			throw Exception{"Invalid parent node"};
		m_nodeSocketRef.node = const_cast<NodeDesc*>(node)->shared_from_this();
		m_nodeSocketRef.socketName = dsIn->ReadString();
		m_nodeSocketRef.output = dsIn->Read<bool>();
		break;
	}
	}
}

std::ostream& operator<<(std::ostream &os,const raytracing::Socket &socket) {os<<socket.ToString(); return os;}

#pragma optimize("",on)
