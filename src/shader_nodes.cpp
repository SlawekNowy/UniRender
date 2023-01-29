/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#include "util_raytracing/shader_nodes.hpp"
#include "util_raytracing/scene.hpp"
#include "util_raytracing/shader.hpp"

#pragma optimize("",off)
std::size_t unirender::SocketHasher::operator()(const Socket& k) const
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

unirender::Socket::Socket(NodeDesc &node,const std::string &socketName,bool output)
{
	m_nodeSocketRef.node = node.shared_from_this();
	m_nodeSocketRef.socketName = socketName;
	m_nodeSocketRef.output = output;
}
unirender::Socket::Socket(const DataValue &value)
	: m_value{value}
{}
unirender::Socket::Socket(float value)
	: Socket{DataValue::Create<decltype(value),SocketType::Float>(value)}
{}
unirender::Socket::Socket(const Vector3 &value)
	: Socket{DataValue::Create<decltype(value),SocketType::Vector>(value)}
{}

unirender::SocketType unirender::Socket::GetType() const
{
	if(IsConcreteValue())
		return m_value->type;
	std::string socketName;
	auto *node = GetNode(socketName);
	auto *desc = node ? node->FindSocketDesc(*this) : nullptr;
	return desc ? desc->dataValue.type : SocketType::Invalid;
}
unirender::Socket unirender::Socket::operator-() const
{
	if(is_vector_type(GetType()))
		return Socket{Vector3{}} -*this;
	return Socket{0.f} -*this;
}
unirender::Socket unirender::Socket::operator+(float f) const {return operator+(Socket{f});}
unirender::Socket unirender::Socket::operator-(float f) const {return operator-(Socket{f});}
unirender::Socket unirender::Socket::operator*(float f) const {return operator*(Socket{f});}
unirender::Socket unirender::Socket::operator/(float f) const {return operator/(Socket{f});}
unirender::Socket unirender::Socket::operator%(float f) const {return operator%(Socket{f});}
unirender::Socket unirender::Socket::operator^(float f) const {return operator^(Socket{f});}
unirender::Socket unirender::Socket::operator<(float f) const {return operator<(Socket{f});}
unirender::Socket unirender::Socket::operator<=(float f) const {return operator<=(Socket{f});}
unirender::Socket unirender::Socket::operator>(float f) const {return operator>(Socket{f});}
unirender::Socket unirender::Socket::operator>=(float f) const {return operator>=(Socket{f});}
unirender::GroupNodeDesc *unirender::Socket::GetCommonGroupNode(const Socket &other) const
{
	unirender::GroupNodeDesc *target = nullptr;
	// Note: We have to handle a special case if either socket is an input socket.
	// This is only allowed if the input socket belongs to a group node and the operation was applied within
	// that group node, in which case we have to add the op node to the group node instead of the parent node.
	if(!IsConcreteValue() && !IsOutputSocket())
	{
		auto node = m_nodeSocketRef.node.lock();
		target = node && node->IsGroupNode() ? static_cast<unirender::GroupNodeDesc*>(node.get()) : nullptr;
	}
	else if(!other.IsConcreteValue() && !other.IsOutputSocket())
	{
		auto node = other.m_nodeSocketRef.node.lock();
		target = node && node->IsGroupNode() ? static_cast<unirender::GroupNodeDesc*>(node.get()) : nullptr;
	}
	else
	{
		auto &ref = IsConcreteValue() ? other.m_nodeSocketRef : m_nodeSocketRef;
		auto node = ref.node.lock();
		target = node ? node->GetParent() : nullptr;
	}
	return target;
}
unirender::Socket unirender::Socket::ApplyOperator(const Socket &other,nodes::math::MathType opType,std::optional<nodes::vector_math::MathType> opTypeVec,float(*applyValue)(float,float)) const
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

	auto *target = GetCommonGroupNode(other);
	if(target == nullptr)
		return 0.f; // Invalid case

	if(is_vector_type(srcType))
	{
		if(opTypeVec.has_value() == false)
			return 0.f; // Invalid case
		if(is_vector_type(dstType))
		{
			// Case #4
			auto &n = target->AddVectorMathNode(*this,other,*opTypeVec);
			return *n.GetPrimaryOutputSocket();
		}
		if(is_numeric_type(dstType) == false)
			return 0.f; // Invalid case
		// Case #3
		auto &vDst = target->AddNode(NODE_COMBINE_XYZ);
		target->Link(other,vDst.GetInputSocket(nodes::combine_xyz::IN_X));
		target->Link(other,vDst.GetInputSocket(nodes::combine_xyz::IN_Y));
		target->Link(other,vDst.GetInputSocket(nodes::combine_xyz::IN_Z));
		
		auto &n = target->AddVectorMathNode(*this,vDst,*opTypeVec);
		return *n.GetPrimaryOutputSocket();
	}
	if(is_vector_type(dstType))
	{
		if(opTypeVec.has_value() == false || is_numeric_type(srcType) == false)
			return 0.f; // Invalid case
		// Case #2
		auto &vSrc = target->AddNode(NODE_COMBINE_XYZ);
		target->Link(*this,vSrc.GetInputSocket(nodes::combine_xyz::IN_X));
		target->Link(*this,vSrc.GetInputSocket(nodes::combine_xyz::IN_Y));
		target->Link(*this,vSrc.GetInputSocket(nodes::combine_xyz::IN_Z));
		
		auto &n = target->AddVectorMathNode(vSrc,other,*opTypeVec);
		return *n.GetPrimaryOutputSocket();
	}

	// Case #1
	return target->AddMathNode(*this,other,opType);
}
unirender::Socket unirender::Socket::operator+(const Socket &socket) const {return ApplyOperator(socket,unirender::nodes::math::MathType::Add,unirender::nodes::vector_math::MathType::Add,[](float a,float b) -> float {return a +b;});}
unirender::Socket unirender::Socket::operator-(const Socket &socket) const {return ApplyOperator(socket,unirender::nodes::math::MathType::Subtract,unirender::nodes::vector_math::MathType::Subtract,[](float a,float b) -> float {return a -b;});}
unirender::Socket unirender::Socket::operator*(const Socket &socket) const {return ApplyOperator(socket,unirender::nodes::math::MathType::Multiply,unirender::nodes::vector_math::MathType::Multiply,[](float a,float b) -> float {return a *b;});}
unirender::Socket unirender::Socket::operator/(const Socket &socket) const {return ApplyOperator(socket,unirender::nodes::math::MathType::Divide,unirender::nodes::vector_math::MathType::Divide,[](float a,float b) -> float {return a /b;});}
unirender::Socket unirender::Socket::operator%(const Socket &socket) const {return ApplyOperator(socket,unirender::nodes::math::MathType::Modulo,unirender::nodes::vector_math::MathType::Modulo,[](float a,float b) -> float {return fmodf(a,b);});}
unirender::Socket unirender::Socket::operator^(const Socket &socket) const {return ApplyOperator(socket,unirender::nodes::math::MathType::Power,{},[](float a,float b) -> float {return powf(a,b);});}

constexpr float COMPARISON_EPSILON = 0.00001f;
unirender::Socket unirender::Socket::ApplyComparisonOperator(const Socket &other,bool(*op)(float,float),Socket(*opNode)(GroupNodeDesc&,const Socket&,const Socket&)) const
{
	if(IsConcreteValue() && other.IsConcreteValue())
	{
		auto valSrc = GetValue()->ToValue<float>();
		auto valDst = other.GetValue()->ToValue<float>();
		if(valSrc.has_value() == false || valDst.has_value() == false)
			return {}; // Invalid case
		return op(*valSrc,*valDst);
	}
	auto *target = GetCommonGroupNode(other);
	if(target == nullptr)
		return 0.f; // Invalid case
	return opNode(*target,*this,other);
}
unirender::Socket unirender::Socket::operator<(const Socket &socket) const
{
	return ApplyComparisonOperator(socket,[](float a,float b) -> bool {return a < b;},[](GroupNodeDesc &node,const Socket &a,const Socket &b) -> Socket {
		return node.AddMathNode(a,b,nodes::math::MathType::LessThan);
	});
}
unirender::Socket unirender::Socket::operator<=(const Socket &socket) const
{
	return ApplyComparisonOperator(socket,[](float a,float b) -> bool {return a <= b;},[](GroupNodeDesc &node,const Socket &a,const Socket &b) -> Socket {
		return node.AddMathNode(a,b +COMPARISON_EPSILON,nodes::math::MathType::LessThan);
	});
}
unirender::Socket unirender::Socket::operator>(const Socket &socket) const
{
	return ApplyComparisonOperator(socket,[](float a,float b) -> bool {return a > b;},[](GroupNodeDesc &node,const Socket &a,const Socket &b) -> Socket {
		return node.AddMathNode(a,b,nodes::math::MathType::GreaterThan);
	});
}
unirender::Socket unirender::Socket::operator>=(const Socket &socket) const
{
	return ApplyComparisonOperator(socket,[](float a,float b) -> bool {return a >= b;},[](GroupNodeDesc &node,const Socket &a,const Socket &b) -> Socket {
		return node.AddMathNode(a,b -COMPARISON_EPSILON,nodes::math::MathType::GreaterThan);
	});
}

bool unirender::Socket::operator==(const Socket &other) const
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
bool unirender::Socket::operator!=(const Socket &other) const {return !operator==(other);}

void unirender::Socket::Link(const Socket &other)
{
	if(IsConcreteValue() && other.IsConcreteValue())
		throw Exception{"Cannot link two concrete sockets!"};
	auto *node0 = GetNode();
	auto *node1 = other.GetNode();
	if(!node0 && !node1)
	{
		// Unreachable
		throw Exception{"Attempted to link two non-concrete sockets that don't belong to any socket! This should never happen!"};
	}
	unirender::GroupNodeDesc *node = nullptr;
	if(node0 && !node1)
		node = node0->GetParent();
	else if(!node0 && node1)
		node = node1->GetParent();
	else if(node0 == node1)
	{
		// Special case where an input socket in a group node is linked directly to
		// one of its output sockets. This is the only case where a node can link to itself!
		assert(node0->IsGroupNode());
		node = node0->IsGroupNode() ? static_cast<unirender::GroupNodeDesc*>(node0) : nullptr;
	}
	else if(node0->GetParent() == node1->GetParent())
	{
		// Different nodes linked together; only allowed if they share the same parent
		node = node0->GetParent();
	}
	else if(node0->GetParent() == node1)
		node = node0->GetParent(); // Link from group node input/output socket to socket of other node within node group
	else if(node1->GetParent() == node0)
		node = node1->GetParent(); // Link from group node input/output socket to socket of other node within node group
	if(node == nullptr)
		return;
	node->Link(*this,other);
}
std::string unirender::Socket::ToString() const
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
			case unirender::SocketType::Bool:
				str += std::to_string(*static_cast<unirender::STBool*>(m_value->value.get()));
				break;
			case unirender::SocketType::Float:
				str += std::to_string(*static_cast<unirender::STFloat*>(m_value->value.get()));
				break;
			case unirender::SocketType::Int:
				str += std::to_string(*static_cast<unirender::STInt*>(m_value->value.get()));
				break;
			case unirender::SocketType::UInt:
				str += std::to_string(*static_cast<unirender::STUInt*>(m_value->value.get()));
				break;
			case unirender::SocketType::Color:
				str += uvec::to_string(static_cast<unirender::STColor*>(m_value->value.get()));
				break;
			case unirender::SocketType::Vector:
				str += uvec::to_string(static_cast<unirender::STVector*>(m_value->value.get()));
				break;
			case unirender::SocketType::Point:
				str += uvec::to_string(static_cast<unirender::STPoint*>(m_value->value.get()));
				break;
			case unirender::SocketType::Normal:
				str += uvec::to_string(static_cast<unirender::STNormal*>(m_value->value.get()));
				break;
			case unirender::SocketType::Point2:
			{
				std::stringstream ss;
				ss<<*static_cast<unirender::STPoint2*>(m_value->value.get());
				str += ss.str();
				break;
			}
			case unirender::SocketType::String:
				str += *static_cast<unirender::STString*>(m_value->value.get());
				break;
			case unirender::SocketType::Enum:
				str += std::to_string(*static_cast<unirender::STEnum*>(m_value->value.get()));
				break;
			case unirender::SocketType::Transform:
			{
				std::stringstream ss;
				auto &t = *static_cast<unirender::STTransform*>(m_value->value.get());
				ss<<t[0][0]<<','<<t[0][1]<<','<<t[0][2]<<','<<t[1][0]<<','<<t[1][1]<<','<<t[1][2]<<','<<t[2][0]<<','<<t[2][1]<<','<<t[2][2]<<','<<t[3][0]<<','<<t[3][1]<<','<<t[3][2];
				str += ss.str();
				break;
			}
			case unirender::SocketType::FloatArray:
			{
				str += '{';
				auto first = true;
				for(auto &v : *static_cast<unirender::STFloatArray*>(m_value->value.get()))
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
			case unirender::SocketType::ColorArray:
			{
				str += '{';
				auto first = true;
				for(auto &v : *static_cast<unirender::STColorArray*>(m_value->value.get()))
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
			static_assert(umath::to_integral(unirender::SocketType::Count) == 16);
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

bool unirender::Socket::IsValid() const {return IsConcreteValue() || m_nodeSocketRef.node.expired() == false;}
bool unirender::Socket::IsConcreteValue() const {return m_value.has_value();}
bool unirender::Socket::IsNodeSocket() const {return !IsConcreteValue();}
bool unirender::Socket::IsOutputSocket() const {return IsConcreteValue() || m_nodeSocketRef.output;}

unirender::NodeDesc *unirender::Socket::GetNode(std::string &outSocketName) const
{
	auto *node = GetNode();
	if(node)
		outSocketName = m_nodeSocketRef.socketName;
	return node;
}
unirender::NodeDesc *unirender::Socket::GetNode() const
{
	return m_nodeSocketRef.node.lock().get();
}
std::optional<unirender::DataValue> unirender::Socket::GetValue() const {return m_value;}

void unirender::Socket::Serialize(DataStream &dsOut,const std::unordered_map<const NodeDesc*,uint64_t> &nodeIndexTable) const
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
void unirender::Socket::Deserialize(GroupNodeDesc &parentGroupNode,DataStream &dsIn,const std::vector<const NodeDesc*> &nodeIndexTable)
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

std::ostream& operator<<(std::ostream &os,const unirender::Socket &socket) {os<<socket.ToString(); return os;}

#pragma optimize("",on)
