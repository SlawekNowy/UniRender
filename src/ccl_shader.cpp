/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/shader.hpp"
#include "util_raytracing/ccl_shader.hpp"
#include "util_raytracing/scene.hpp"
#include "util_raytracing/mesh.hpp"
#include "util_raytracing/exception.hpp"
#include <render/shader.h>
#include <render/graph.h>
#include <render/scene.h>
#include <render/nodes.h>
#include <OpenImageIO/ustring.h>
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>

#pragma optimize("",off)
std::shared_ptr<raytracing::CCLShader> raytracing::CCLShader::Create(Scene &scene,ccl::Shader &cclShader,const GroupNodeDesc &desc,bool useCache)
{
	if(useCache)
	{
		auto shader = scene.GetCachedShader(desc);
		if(shader)
			return shader;
	}
	cclShader.volume_sampling_method = ccl::VOLUME_SAMPLING_MULTIPLE_IMPORTANCE;

	ccl::ShaderGraph *graph = new ccl::ShaderGraph();
	auto pShader = std::shared_ptr<CCLShader>{new CCLShader{scene,cclShader,*graph}};
	pShader->m_flags |= Flags::CCLShaderOwnedByScene;

	pShader->InitializeNodeGraph(desc);
	if(useCache)
		scene.AddShader(*pShader,&desc);
	return pShader;
}
std::shared_ptr<raytracing::CCLShader> raytracing::CCLShader::Create(Scene &scene,const GroupNodeDesc &desc)
{
	auto shader = scene.GetCachedShader(desc);
	if(shader)
		return shader;
	auto *cclShader = new ccl::Shader{}; // Object will be removed automatically by cycles
	cclShader->name = desc.GetName();
	scene->shaders.push_back(cclShader);
	return Create(scene,*cclShader,desc,true);
}

raytracing::CCLShader::CCLShader(Scene &scene,ccl::Shader &cclShader,ccl::ShaderGraph &cclShaderGraph)
	: m_scene{scene},m_cclShader{cclShader},m_cclGraph{cclShaderGraph}
{}

raytracing::CCLShader::~CCLShader()
{
	if(umath::is_flag_set(m_flags,Flags::CCLShaderGraphOwnedByScene) == false)
		delete &m_cclGraph;
	if(umath::is_flag_set(m_flags,Flags::CCLShaderOwnedByScene) == false)
		delete &m_cclShader;
}

ccl::Shader *raytracing::CCLShader::operator->() {return &m_cclShader;}
ccl::Shader *raytracing::CCLShader::operator*() {return &m_cclShader;}

void raytracing::CCLShader::DoFinalize(Scene &scene)
{
	BaseObject::DoFinalize(scene);
	m_flags |= Flags::CCLShaderGraphOwnedByScene | Flags::CCLShaderOwnedByScene;
	m_cclShader.set_graph(&m_cclGraph);
	m_cclShader.tag_update(*m_scene);
}

ccl::ShaderNode *raytracing::CCLShader::AddNode(const std::string &typeName)
{
	auto *nodeType = ccl::NodeType::find(ccl::ustring{typeName});
	auto *snode = nodeType ? static_cast<ccl::ShaderNode*>(nodeType->create(nodeType)) : nullptr;
	if(snode == nullptr)
	{
		m_scene.HandleError("Unable to create ccl node of type '" +typeName +"': Invalid type?");
		return nullptr;
	}

	auto name = GetCurrentInternalNodeName();
	snode->name = name;
	m_cclGraph.add(snode);
	return snode;
}

void raytracing::CCLShader::InitializeNode(const NodeDesc &desc,std::unordered_map<const NodeDesc*,ccl::ShaderNode*> &nodeToCclNode,const GroupSocketTranslationTable &groupIoSockets)
{
	if(desc.IsGroupNode())
	{
		auto &groupDesc = *static_cast<const GroupNodeDesc*>(&desc);
		auto &childNodes = groupDesc.GetChildNodes();
		for(auto &childNode : childNodes)
			InitializeNode(*childNode,nodeToCclNode,groupIoSockets);
		
		auto getCclSocket = [this,&groupIoSockets,&nodeToCclNode](const Socket &socket,bool input) -> std::optional<std::pair<ccl::ShaderNode*,std::string>> {
			auto it = groupIoSockets.find(socket);
			if(it != groupIoSockets.end())
				return input ? it->second.input : it->second.output;
			std::string socketName;
			auto *node = socket.GetNode(socketName);
			auto itNode = nodeToCclNode.find(node);
			if(itNode == nodeToCclNode.end())
			{
				// m_scene.HandleError("Unable to locate ccl node for from node '" +node->GetName() +"'!");
				return {};
			}
			auto *cclNode = itNode->second;
			return std::pair<ccl::ShaderNode*,std::string>{cclNode,socketName};
		};
		auto &links = groupDesc.GetLinks();
		for(auto &link : links)
		{
			auto cclFromSocket = getCclSocket(link.fromSocket,false);
			auto cclToSocket = getCclSocket(link.toSocket,true);
			if(cclFromSocket.has_value() == false || cclToSocket.has_value() == false)
			{
				if(cclToSocket.has_value() && link.fromSocket.IsNodeSocket() && link.fromSocket.IsOutputSocket() == false)
				{
					std::string fromSocketName;
					auto *fromNode = link.fromSocket.GetNode(fromSocketName);
					if(fromNode)
					{
						auto *fromSocketDesc = fromNode->FindPropertyDesc(fromSocketName);
						// This is a special case where the input socket is actually a property,
						// so instead of linking, we just assign the property value directly.
						auto *prop = FindProperty(*cclToSocket->first,cclToSocket->second);
						if(fromSocketDesc && fromSocketDesc->dataValue.value && prop)
							ApplySocketValue(*fromSocketDesc,*cclToSocket->first,*prop);
					}
				}
				continue;
			}
			auto *output = FindOutput(*cclFromSocket->first,cclFromSocket->second);
			auto *input = FindInput(*cclToSocket->first,cclToSocket->second);
			if(output == nullptr)
			{
				m_scene.HandleError("Invalid CCL output '" +cclFromSocket->second +"' for node of type '" +std::string{typeid(*cclFromSocket->first).name()} +"'!");
				continue;
			}
			if(input == nullptr)
			{
				m_scene.HandleError("Invalid CCL input '" +cclToSocket->second +"' for node of type '" +std::string{typeid(*cclToSocket->first).name()} +"'!");
				continue;
			}
			m_cclGraph.connect(output,input);
		}
		return;
	}
	auto &typeName = desc.GetTypeName();
	if(typeName == "output")
	{
		// Output node already exists by default
		nodeToCclNode[&desc] = m_cclGraph.output();
		return;
	}
	auto *snode = AddNode(typeName);
	if(snode == nullptr)
		return;
	for(auto &pair : desc.GetInputs())
	{
		auto *input = FindInput(*snode,pair.first);
		if(input == nullptr)
			continue; // TODO
		ApplySocketValue(pair.second,*snode,input->socket_type);
	}

	for(auto &pair : desc.GetProperties())
	{
		auto *input = snode->type->find_input(ccl::ustring{pair.first.c_str()});
		if(input == nullptr)
			continue;
		ApplySocketValue(pair.second,*snode,*input);
	}

	nodeToCclNode[&desc] = snode;
}

template<typename TSrc,typename TDst>
	static ccl::array<TDst> to_ccl_array(const std::vector<TSrc> &input,const std::function<TDst(const TSrc&)> &converter)
{
	ccl::array<TDst> output {};
	output.resize(input.size());
	for(auto i=decltype(input.size()){0u};i<input.size();++i)
		output[i] = converter(input.at(i));
	return output;
}

void raytracing::CCLShader::ApplySocketValue(const NodeSocketDesc &sockDesc,ccl::Node &node,const ccl::SocketType &sockType)
{
	switch(sockDesc.dataValue.type)
	{
	case SocketType::Bool:
		static_assert(std::is_same_v<STBool,bool>);
		node.set(sockType,*static_cast<STBool*>(sockDesc.dataValue.value.get()));
		break;
	case SocketType::Float:
		static_assert(std::is_same_v<STFloat,float>);
		node.set(sockType,*static_cast<STFloat*>(sockDesc.dataValue.value.get()));
		break;
	case SocketType::Int:
		static_assert(std::is_same_v<STInt,ccl::int32_t>);
		node.set(sockType,*static_cast<STInt*>(sockDesc.dataValue.value.get()));
		break;
	case SocketType::Enum:
		static_assert(std::is_same_v<STEnum,ccl::int32_t>);
		node.set(sockType,*static_cast<STEnum*>(sockDesc.dataValue.value.get()));
		break;
	case SocketType::UInt:
		static_assert(std::is_same_v<STUInt,ccl::uint>);
		node.set(sockType,*static_cast<STUInt*>(sockDesc.dataValue.value.get()));
		break;
	case SocketType::Color:
	case SocketType::Vector:
	case SocketType::Point:
	case SocketType::Normal:
	{
		static_assert(std::is_same_v<STColor,Vector3> && std::is_same_v<STVector,Vector3> && std::is_same_v<STPoint,Vector3> && std::is_same_v<STNormal,Vector3>);
		auto &v = *static_cast<STVector*>(sockDesc.dataValue.value.get());
		node.set(sockType,ccl::float3{v.x,v.y,v.z});
		break;
	}
	case SocketType::Point2:
	{
		static_assert(std::is_same_v<STPoint2,Vector2>);
		auto &v = *static_cast<STPoint2*>(sockDesc.dataValue.value.get());
		node.set(sockType,ccl::float2{v.x,v.y});
		break;
	}
	case SocketType::String:
	{
		static_assert(std::is_same_v<STString,std::string>);
		auto &v = *static_cast<std::string*>(sockDesc.dataValue.value.get());
		node.set(sockType,v.c_str());
		break;
	}
	case SocketType::Transform:
	{
		static_assert(std::is_same_v<STTransform,Mat4x3>);
		auto &v = *static_cast<Mat4x3*>(sockDesc.dataValue.value.get());
		node.set(sockType,ccl::Transform{
			v[0][0],v[0][1],v[0][2],
			v[1][0],v[1][1],v[1][2],
			v[2][0],v[2][1],v[2][2],
			v[3][0],v[3][1],v[3][2]
		});
		break;
	}
	case SocketType::FloatArray:
	{
		static_assert(std::is_same_v<STFloatArray,std::vector<STFloat>>);
		auto &v = *static_cast<std::vector<STFloat>*>(sockDesc.dataValue.value.get());
		node.set(sockType,to_ccl_array<float,float>(v,[](const float &v) -> float {return v;}));
		break;
	}
	case SocketType::ColorArray:
	{
		static_assert(std::is_same_v<STColorArray,std::vector<STColor>>);
		auto &v = *static_cast<std::vector<STColor>*>(sockDesc.dataValue.value.get());
		node.set(sockType,to_ccl_array<Vector3,ccl::float3>(v,[](const Vector3 &v) -> ccl::float3 {return ccl::float3{v.x,v.y,v.z};}));
		break;
	}
	}
	static_assert(umath::to_integral(SocketType::Count) == 16);
}

void raytracing::CCLShader::ConvertGroupSocketsToNodes(const GroupNodeDesc &groupDesc,GroupSocketTranslationTable &outGroupIoSockets)
{
	// Note: Group nodes don't exist in Cycles, they're implicit and replaced by their contents.
	// To do so, we convert the input and output sockets to constant nodes and re-direct all links
	// that point to these sockets to the new nodes instead.
	auto convertGroupSocketsToNodes = [this,&groupDesc,&outGroupIoSockets](const std::unordered_map<std::string,NodeSocketDesc> &sockets,bool output) {
		for(auto &pair : sockets)
		{
			Socket socket {const_cast<GroupNodeDesc&>(groupDesc),pair.first,output};
			auto &socketDesc = pair.second;
			GroupSocketTranslation socketTranslation {};
			if(is_convertible_to(socketDesc.dataValue.type,SocketType::Float))
			{
				auto *nodeMath = static_cast<ccl::MathNode*>(AddNode(NODE_MATH));
				assert(nodeMath);
				nodeMath->type = ccl::NodeMathType::NODE_MATH_ADD;
				nodeMath->value1 = 0.f;
				nodeMath->value2 = 0.f;

				if(socketDesc.dataValue.value)
				{
					auto v = socketDesc.dataValue.ToValue<float>();
					if(v.has_value())
						nodeMath->value1 = *v;
				}
				socketTranslation.input = {nodeMath,nodes::math::IN_VALUE1};
				socketTranslation.output = {nodeMath,nodes::math::OUT_VALUE};
			}
			else if(is_convertible_to(socketDesc.dataValue.type,SocketType::Vector))
			{
				auto *nodeVec = static_cast<ccl::VectorMathNode*>(AddNode(NODE_VECTOR_MATH));
				assert(nodeVec);
				nodeVec->type = ccl::NodeVectorMathType::NODE_VECTOR_MATH_ADD;
				nodeVec->vector1 = {0.f,0.f,0.f};
				nodeVec->vector2 = {0.f,0.f,0.f};

				if(socketDesc.dataValue.value)
				{
					auto v = socketDesc.dataValue.ToValue<Vector3>();
					if(v.has_value())
						nodeVec->vector1 = {v->x,v->y,v->z};
				}
				socketTranslation.input = {nodeVec,nodes::vector_math::IN_VECTOR1};
				socketTranslation.output = {nodeVec,nodes::vector_math::OUT_VECTOR};
			}
			else if(socketDesc.dataValue.type == raytracing::SocketType::Closure)
			{
				auto *mix = static_cast<ccl::MixClosureNode*>(AddNode(NODE_MIX_CLOSURE));
				assert(mix);
				mix->fac = 0.f;

				socketTranslation.input = {mix,nodes::mix_closure::IN_CLOSURE1};
				socketTranslation.output = {mix,nodes::mix_closure::OUT_CLOSURE};
			}
			else
			{
				// m_scene.HandleError("Group node has socket of type '" +to_string(socketDesc.dataValue.type) +"', but only float and vector types are allowed!");
				continue;
			}
			outGroupIoSockets[socket] = socketTranslation;
		}
	};
	convertGroupSocketsToNodes(groupDesc.GetInputs(),false);
	convertGroupSocketsToNodes(groupDesc.GetProperties(),false);
	convertGroupSocketsToNodes(groupDesc.GetOutputs(),true);

	for(auto &node : groupDesc.GetChildNodes())
	{
		if(node->IsGroupNode() == false)
			continue;
		ConvertGroupSocketsToNodes(static_cast<GroupNodeDesc&>(*node),outGroupIoSockets);
	}
}

void raytracing::CCLShader::InitializeNodeGraph(const GroupNodeDesc &desc)
{
	GroupSocketTranslationTable groupIoSockets;
	ConvertGroupSocketsToNodes(desc,groupIoSockets);

	std::unordered_map<const NodeDesc*,ccl::ShaderNode*> nodeToCclNode;
	InitializeNode(desc,nodeToCclNode,groupIoSockets);
}

const ccl::SocketType *raytracing::CCLShader::FindProperty(ccl::ShaderNode &node,const std::string &inputName) const
{
	auto it = std::find_if(node.type->inputs.begin(),node.type->inputs.end(),[&inputName](const ccl::SocketType &socketType) {
		return ccl::string_iequals(socketType.name.string(),inputName);
	});
	return (it != node.type->inputs.end()) ? &*it : nullptr;
}
ccl::ShaderInput *raytracing::CCLShader::FindInput(ccl::ShaderNode &node,const std::string &inputName) const
{
	// return node.input(ccl::ustring{inputName}); // Doesn't work in some cases for some reason
	auto it = std::find_if(node.inputs.begin(),node.inputs.end(),[&inputName](const ccl::ShaderInput *shInput) {
		return ccl::string_iequals(shInput->socket_type.name.string(),inputName);
	});
	return (it != node.inputs.end()) ? *it : nullptr;
}
ccl::ShaderOutput *raytracing::CCLShader::FindOutput(ccl::ShaderNode &node,const std::string &outputName) const
{
	// return node.output(ccl::ustring{outputName}); // Doesn't work in some cases for some reason
	auto it = std::find_if(node.outputs.begin(),node.outputs.end(),[&outputName](const ccl::ShaderOutput *shOutput) {
		return ccl::string_iequals(shOutput->socket_type.name.string(),outputName);
	});
	return (it != node.outputs.end()) ? *it : nullptr;
}

std::string raytracing::CCLShader::GetCurrentInternalNodeName() const {return "internal_" +std::to_string(m_cclGraph.nodes.size());}

//////////////////////

void raytracing::NodeDescLink::Serialize(DataStream &dsOut,const std::unordered_map<const NodeDesc*,uint64_t> &nodeIndexTable) const
{
	fromSocket.Serialize(dsOut,nodeIndexTable);
	toSocket.Serialize(dsOut,nodeIndexTable);
}
void raytracing::NodeDescLink::Deserialize(GroupNodeDesc &groupNode,DataStream &dsIn,const std::vector<const NodeDesc*> &nodeIndexTable)
{
	fromSocket.Deserialize(groupNode,dsIn,nodeIndexTable);
	toSocket.Deserialize(groupNode,dsIn,nodeIndexTable);
}

//////////////////////

raytracing::NodeSocketDesc raytracing::NodeSocketDesc::Deserialize(DataStream &dsIn)
{
	NodeSocketDesc desc {};
	desc.io = dsIn->Read<decltype(desc.io)>();
	desc.dataValue = DataValue::Deserialize(dsIn);
	return desc;
}
void raytracing::NodeSocketDesc::Serialize(DataStream &dsOut) const
{
	dsOut->Write(io);
	dataValue.Serialize(dsOut);
}

//////////////////////

template<class TNodeDesc>
	std::shared_ptr<TNodeDesc> raytracing::NodeDesc::Create(GroupNodeDesc *parent)
{
	auto node = std::shared_ptr<TNodeDesc>{new TNodeDesc{}};
	node->SetParent(parent);
	return node;
}
std::shared_ptr<raytracing::NodeDesc> raytracing::NodeDesc::Create(GroupNodeDesc *parent) {return Create<NodeDesc>(parent);}
raytracing::NodeDesc::NodeDesc()
{}
std::string raytracing::NodeDesc::GetName() const {return m_name;}
const std::string &raytracing::NodeDesc::GetTypeName() const {return m_typeName;}
std::string raytracing::NodeDesc::ToString() const {return "Node[" +GetName() +"][" +GetTypeName() +"]";}
void raytracing::NodeDesc::SetTypeName(const std::string &typeName) {m_typeName = typeName;}
raytracing::NodeIndex raytracing::NodeDesc::GetIndex() const
{
	auto *parent = GetParent();
	if(parent == nullptr)
		return std::numeric_limits<NodeIndex>::max();
	auto &nodes = parent->GetChildNodes();
	auto it = std::find_if(nodes.begin(),nodes.end(),[this](const std::shared_ptr<raytracing::NodeDesc> &node) {
		return node.get() == this;
	});
	if(it == nodes.end())
		throw Exception{"Node references parent which it doesn't belong to"};
	return it -nodes.begin();
}

void raytracing::NodeDesc::RegisterPrimaryOutputSocket(const std::string &name) {m_primaryOutputSocket = name;}

raytracing::NodeDesc::operator raytracing::Socket() const
{
	return *GetPrimaryOutputSocket();
}

raytracing::Socket raytracing::NodeDesc::RegisterSocket(const std::string &name,const DataValue &value,SocketIO io)
{
	NodeSocketDesc socketDesc {};
	socketDesc.io = io;
	socketDesc.dataValue = value;
	switch(io)
	{
	case SocketIO::In:
		m_inputs.insert(std::make_pair(name,socketDesc));
		return GetInputSocket(name);
	case SocketIO::Out:
		m_outputs.insert(std::make_pair(name,socketDesc));
		return GetOutputSocket(name);
	default:
		m_properties.insert(std::make_pair(name,socketDesc));
		return GetProperty(name);
	}
}

const std::unordered_map<std::string,raytracing::NodeSocketDesc> &raytracing::NodeDesc::GetInputs() const {return m_inputs;}
const std::unordered_map<std::string,raytracing::NodeSocketDesc> &raytracing::NodeDesc::GetOutputs() const {return m_outputs;}
const std::unordered_map<std::string,raytracing::NodeSocketDesc> &raytracing::NodeDesc::GetProperties() const {return m_properties;}

raytracing::Socket raytracing::NodeDesc::GetInputSocket(const std::string &name)
{
	auto socket = FindInputSocket(name);
	assert(socket.has_value());
	if(socket.has_value() == false)
		throw Exception{ToString() +" has no input socket named '" +name +"'!"};
	return *socket;
}
raytracing::Socket raytracing::NodeDesc::GetOutputSocket(const std::string &name)
{
	auto socket = FindOutputSocket(name);
	assert(socket.has_value());
	if(socket.has_value() == false)
		throw Exception{ToString() +" has no output socket named '" +name +"'!"};
	return *socket;
}
raytracing::Socket raytracing::NodeDesc::GetProperty(const std::string &name)
{
	auto socket = FindProperty(name);
	assert(socket.has_value());
	if(socket.has_value() == false)
		throw Exception{ToString() +" has no property named '" +name +"'!"};
	return *socket;
}
std::optional<raytracing::Socket> raytracing::NodeDesc::GetPrimaryOutputSocket() const {return m_primaryOutputSocket ? const_cast<NodeDesc*>(this)->FindOutputSocket(*m_primaryOutputSocket) : std::optional<raytracing::Socket>{};}
raytracing::NodeSocketDesc *raytracing::NodeDesc::FindInputSocketDesc(const std::string &name)
{
	auto it = m_inputs.find(name);
	if(it == m_inputs.end())
		return nullptr;
	return &it->second;
}
raytracing::NodeSocketDesc *raytracing::NodeDesc::FindOutputSocketDesc(const std::string &name)
{
	auto it = m_outputs.find(name);
	if(it == m_outputs.end())
		return nullptr;
	return &it->second;
}
raytracing::NodeSocketDesc *raytracing::NodeDesc::FindPropertyDesc(const std::string &name)
{
	auto it = m_properties.find(name);
	if(it == m_properties.end())
		return nullptr;
	return &it->second;
}
raytracing::NodeSocketDesc *raytracing::NodeDesc::FindSocketDesc(const Socket &socket)
{
	if(socket.IsConcreteValue())
		return nullptr;
	std::string socketName;
	socket.GetNode(socketName);
	if(socket.IsOutputSocket())
		return FindOutputSocketDesc(socketName);
	return FindInputSocketDesc(socketName);
}
raytracing::GroupNodeDesc *raytracing::NodeDesc::GetParent() const {return m_parent.lock().get();}
void raytracing::NodeDesc::SetParent(GroupNodeDesc *parent) {m_parent = parent ? std::static_pointer_cast<GroupNodeDesc>(parent->shared_from_this()) : std::weak_ptr<GroupNodeDesc>{};}
std::optional<raytracing::Socket> raytracing::NodeDesc::FindInputSocket(const std::string &name)
{
	auto *desc = FindInputSocketDesc(name);
	return desc ? Socket{*this,name,false} : std::optional<raytracing::Socket>{};
}
std::optional<raytracing::Socket> raytracing::NodeDesc::FindOutputSocket(const std::string &name)
{
	auto *desc = FindOutputSocketDesc(name);
	return desc ? Socket{*this,name,true} : std::optional<raytracing::Socket>{};
}
std::optional<raytracing::Socket> raytracing::NodeDesc::FindProperty(const std::string &name)
{
	auto *desc = FindPropertyDesc(name);
	return desc ? Socket{*this,name,false} : std::optional<raytracing::Socket>{};
}
void raytracing::NodeDesc::SerializeNodes(DataStream &dsOut) const
{
	dsOut->WriteString(m_typeName);
	dsOut->WriteString(m_name);
	auto fWriteProperties = [&dsOut](const std::unordered_map<std::string,NodeSocketDesc> &props) {
		dsOut->Write<uint32_t>(props.size());
		for(auto &pair : props)
		{
			dsOut->WriteString(pair.first);
			pair.second.Serialize(dsOut);
		}
	};
	fWriteProperties(m_inputs);
	fWriteProperties(m_properties);
	fWriteProperties(m_outputs);

	dsOut->Write<bool>(m_primaryOutputSocket.has_value());
	if(m_primaryOutputSocket.has_value())
		dsOut->WriteString(*m_primaryOutputSocket);
}
void raytracing::NodeDesc::DeserializeNodes(DataStream &dsIn)
{
	m_typeName = dsIn->ReadString();
	m_name = dsIn->ReadString();
	auto fReadProperties = [&dsIn](std::unordered_map<std::string,NodeSocketDesc> &props) {
		auto n = dsIn->Read<uint32_t>();
		props.reserve(n);
		for(auto i=decltype(n){0u};i<n;++i)
		{
			auto key = dsIn->ReadString();
			props[key] = NodeSocketDesc::Deserialize(dsIn);
		}
	};
	fReadProperties(m_inputs);
	fReadProperties(m_properties);
	fReadProperties(m_outputs);

	auto hasPrimaryOutputSocket = dsIn->Read<bool>();
	if(hasPrimaryOutputSocket)
		m_primaryOutputSocket = dsIn->ReadString();
}

//////////////////////

std::shared_ptr<raytracing::GroupNodeDesc> raytracing::GroupNodeDesc::Create(NodeManager &nodeManager,GroupNodeDesc *parent)
{
	auto node = std::shared_ptr<GroupNodeDesc>{new GroupNodeDesc{nodeManager}};
	node->SetParent(parent);
	return node;
}
raytracing::GroupNodeDesc::GroupNodeDesc(NodeManager &nodeManager)
	: NodeDesc{},m_nodeManager{nodeManager}
{}
const std::vector<std::shared_ptr<raytracing::NodeDesc>> &raytracing::GroupNodeDesc::GetChildNodes() const {return m_nodes;}
const std::vector<raytracing::NodeDescLink> &raytracing::GroupNodeDesc::GetLinks() const {return m_links;}
raytracing::NodeDesc *raytracing::GroupNodeDesc::FindNode(const std::string &name)
{
	auto it = std::find_if(m_nodes.begin(),m_nodes.end(),[&name](const std::shared_ptr<NodeDesc> &desc) {
		return desc->GetName() == name;
	});
	return (it != m_nodes.end()) ? it->get() : nullptr;
}
raytracing::NodeDesc *raytracing::GroupNodeDesc::FindNodeByType(const std::string &type)
{
	auto it = std::find_if(m_nodes.begin(),m_nodes.end(),[&type](const std::shared_ptr<NodeDesc> &desc) {
		return desc->GetTypeName() == type;
	});
	return (it != m_nodes.end()) ? it->get() : nullptr;
}
raytracing::NodeDesc *raytracing::GroupNodeDesc::GetNodeByIndex(NodeIndex idx) const
{
	if(idx >= m_nodes.size())
		return nullptr;
	return m_nodes.at(idx).get();
}
raytracing::NodeDesc &raytracing::GroupNodeDesc::AddNode(const std::string &typeName)
{
	if(m_nodes.size() == m_nodes.capacity())
		m_nodes.reserve(m_nodes.size() *1.5 +10);
	auto node = m_nodeManager.CreateNode(typeName,this);
	if(node == nullptr)
		throw Exception{"Invalid node type '" +typeName +"'!"};
	m_nodes.push_back(node);

	if(typeName == "normal_map")
	{
		//node->SetProperty(nodes::normal_map::IN_ATTRIBUTE,"test");

	}

	return *node;
}
raytracing::NodeDesc &raytracing::GroupNodeDesc::AddNode(NodeTypeId id)
{
	if(m_nodes.size() == m_nodes.capacity())
		m_nodes.reserve(m_nodes.size() *1.5 +10);
	auto node = m_nodeManager.CreateNode(id,this);
	if(node == nullptr)
		throw Exception{"Invalid node type '" +std::to_string(id) +"'!"};
	m_nodes.push_back(node);
	return *node;
}
raytracing::Socket raytracing::GroupNodeDesc::AddMathNode(const Socket &socket0,const Socket &socket1,ccl::NodeMathType mathOp)
{
	auto &node = AddNode(NODE_MATH);
	node.SetProperty(nodes::math::IN_TYPE,mathOp);
	Link(socket0,node.GetInputSocket(nodes::math::IN_VALUE1));
	Link(socket1,node.GetInputSocket(nodes::math::IN_VALUE2));
	return node;
}
raytracing::NodeDesc &raytracing::GroupNodeDesc::AddVectorMathNode(const Socket &socket0,const Socket &socket1,ccl::NodeVectorMathType mathOp)
{
	auto &node = AddNode(NODE_VECTOR_MATH);
	node.SetProperty(nodes::vector_math::IN_TYPE,mathOp);
	Link(socket0,node.GetInputSocket(nodes::vector_math::IN_VECTOR1));
	if(socket1.IsValid())
		Link(socket1,node.GetInputSocket(nodes::vector_math::IN_VECTOR2));
	return node;
}
raytracing::Socket raytracing::GroupNodeDesc::AddNormalMapNode(const std::optional<std::string> &fileName,const std::optional<Socket> &fileNameSocket,float strength)
{
	return AddNormalMapNodeDesc(fileName,fileNameSocket,strength);
}
raytracing::NodeDesc &raytracing::GroupNodeDesc::AddNormalMapNodeDesc(const std::optional<std::string> &fileName,const std::optional<Socket> &fileNameSocket,float strength)
{
	auto &node = AddImageTextureNode(fileName,fileNameSocket,TextureType::NonColorImage);
	auto &nmap = AddNode(NODE_NORMAL_MAP);
	nmap.SetProperty(nodes::normal_map::IN_SPACE,ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_TANGENT);
	Link(*node.GetPrimaryOutputSocket(),nmap.GetInputSocket(nodes::normal_map::IN_COLOR));
	nmap.SetProperty(nodes::normal_map::IN_STRENGTH,strength);

#if 0
	// The y-component has to be inverted to match the behavior in Blender, but I'm not sure why
	auto &nComponents = SeparateRGB(nmap.GetOutputSocket(nodes::normal_map::OUT_NORMAL));
	auto &convertedNormal = AddNode(NODE_COMBINE_RGB);
	Link(nComponents.GetOutputSocket(nodes::separate_rgb::OUT_R),convertedNormal.GetInputSocket(nodes::combine_rgb::IN_R));
	Link(-nComponents.GetOutputSocket(nodes::separate_rgb::OUT_G),convertedNormal.GetInputSocket(nodes::combine_rgb::IN_G));
	Link(nComponents.GetOutputSocket(nodes::separate_rgb::OUT_G),convertedNormal.GetInputSocket(nodes::combine_rgb::IN_G));
	Link(nComponents.GetOutputSocket(nodes::separate_rgb::OUT_B),convertedNormal.GetInputSocket(nodes::combine_rgb::IN_B));
	return convertedNormal;
#endif
	return nmap;
}
raytracing::NodeDesc &raytracing::GroupNodeDesc::AddImageTextureNode(const std::optional<std::string> &fileName,const std::optional<Socket> &fileNameSocket,TextureType type)
{
	raytracing::NodeDesc *desc = nullptr;
	switch(type)
	{
	case TextureType::ColorImage:
	{
		auto &node = AddNode(NODE_IMAGE_TEXTURE);
		node.SetProperty(nodes::image_texture::IN_COLORSPACE,ccl::u_colorspace_srgb.c_str());
		desc = &node;
		break;
	}
	case TextureType::NonColorImage:
	{
		auto &node = AddNode(NODE_IMAGE_TEXTURE);
		node.SetProperty(nodes::image_texture::IN_COLORSPACE,ccl::u_colorspace_raw.c_str());
		desc = &node;
		break;
	}
	case TextureType::EquirectangularImage:
	{
		auto &node = AddNode(NODE_ENVIRONMENT_TEXTURE);
		node.SetProperty(nodes::environment_texture::IN_COLORSPACE,ccl::u_colorspace_raw.c_str());
		node.SetProperty(nodes::environment_texture::IN_PROJECTION,ccl::NodeEnvironmentProjection::NODE_ENVIRONMENT_EQUIRECTANGULAR);
		desc = &node;
		break;
	}
	case TextureType::NormalMap:
		return AddNormalMapNodeDesc(fileName,fileNameSocket);
	}
	static_assert(umath::to_integral(TextureType::Count) == 4);
	assert(nodes::image_texture::IN_FILENAME == nodes::environment_texture::IN_FILENAME);
	if(fileName.has_value())
		desc->SetProperty(nodes::image_texture::IN_FILENAME,*fileName);
	else
	{
		assert(fileNameSocket.has_value());
		auto inFilename = desc->FindProperty(nodes::image_texture::IN_FILENAME);
		assert(inFilename.has_value());
		Link(*fileNameSocket,*inFilename);
	}
	return *desc;
}
raytracing::NodeDesc &raytracing::GroupNodeDesc::AddImageTextureNode(const std::string &fileName,TextureType type)
{
	return AddImageTextureNode(fileName,{},type);
}
raytracing::NodeDesc &raytracing::GroupNodeDesc::AddImageTextureNode(const Socket &fileNameSocket,TextureType type)
{
	return AddImageTextureNode({},fileNameSocket,type);
}
raytracing::Socket raytracing::GroupNodeDesc::AddConstantNode(const Vector3 &v)
{
	auto &node = AddNode(NODE_VECTOR_MATH);
	node.SetProperty(nodes::vector_math::IN_VECTOR1,v);
	node.SetProperty(nodes::vector_math::IN_VECTOR2,Vector3{});
	node.SetProperty(nodes::vector_math::IN_TYPE,ccl::NodeVectorMathType::NODE_VECTOR_MATH_ADD);
	return node;
}
raytracing::Socket raytracing::GroupNodeDesc::Mix(const Socket &socket0,const Socket &socket1,const Socket &fac)
{
	auto type0 = socket0.GetType();
	auto type1 = socket1.GetType();
	if(type0 != SocketType::Closure && type1 != SocketType::Closure)
		return Mix(socket0,socket1,fac,ccl::NodeMix::NODE_MIX_BLEND);
	auto &node = AddNode(NODE_MIX_CLOSURE);
	Link(socket0,node.GetInputSocket(nodes::mix_closure::IN_CLOSURE1));
	Link(socket1,node.GetInputSocket(nodes::mix_closure::IN_CLOSURE2));
	Link(fac,node.GetInputSocket(nodes::mix_closure::IN_FAC));
	return node;
}
raytracing::Socket raytracing::GroupNodeDesc::Mix(const Socket &socket0,const Socket &socket1,const Socket &fac,ccl::NodeMix type)
{
	auto &node = AddNode(NODE_MIX);
	Link(socket0,node.GetInputSocket(nodes::mix::IN_COLOR1));
	Link(socket1,node.GetInputSocket(nodes::mix::IN_COLOR2));
	Link(fac,node.GetInputSocket(nodes::mix::IN_FAC));
	node.SetProperty(nodes::mix::IN_TYPE,type);
	return node;
}
raytracing::Socket raytracing::GroupNodeDesc::Invert(const Socket &socket,const std::optional<Socket> &fac)
{
	auto &node = AddNode(NODE_INVERT);
	Link(socket,node.GetInputSocket(nodes::invert::IN_COLOR));
	if(fac.has_value())
		Link(*fac,node.GetInputSocket(nodes::invert::IN_FAC));
	return node;
}
raytracing::Socket raytracing::GroupNodeDesc::ToGrayScale(const Socket &socket)
{
	auto &node = AddNode(NODE_RGB_TO_BW);
	Link(socket,node.GetInputSocket(nodes::rgb_to_bw::IN_COLOR));
	return CombineRGB(node,node,node);
}
raytracing::Socket raytracing::GroupNodeDesc::AddConstantNode(float f)
{
	auto &node = AddNode(NODE_MATH);
	node.SetProperty(nodes::math::IN_VALUE1,f);
	node.SetProperty(nodes::math::IN_VALUE2,0.f);
	node.SetProperty(nodes::math::IN_TYPE,ccl::NodeMathType::NODE_MATH_ADD);
	return node;
}
raytracing::Socket raytracing::GroupNodeDesc::CombineRGB(const Socket &r,const Socket &g,const Socket &b)
{
	auto &node = AddNode(NODE_COMBINE_RGB);
	Link(r,node.GetInputSocket(nodes::combine_rgb::IN_R));
	Link(g,node.GetInputSocket(nodes::combine_rgb::IN_G));
	Link(b,node.GetInputSocket(nodes::combine_rgb::IN_B));
	return node;
}
raytracing::NodeDesc &raytracing::GroupNodeDesc::SeparateRGB(const Socket &rgb)
{
	auto &node = AddNode(NODE_SEPARATE_RGB);
	Link(rgb,node.GetInputSocket(nodes::separate_rgb::IN_COLOR));
	return node;
}
void raytracing::GroupNodeDesc::Serialize(DataStream &dsOut)
{
	// Root node; Build index list
	std::unordered_map<const NodeDesc*,uint64_t> rootNodeIndexTable;
	uint64_t idx = 0u;

	std::function<void(const NodeDesc&)> fBuildIndexTable = nullptr;
	fBuildIndexTable = [&fBuildIndexTable,&rootNodeIndexTable,&idx](const NodeDesc &node) {
		rootNodeIndexTable[&node] = idx++;
		if(node.IsGroupNode() == false)
			return;
		auto &nodeGroup = static_cast<const GroupNodeDesc&>(node);
		for(auto &child : nodeGroup.GetChildNodes())
			fBuildIndexTable(*child);
	};
	fBuildIndexTable(*this);
	SerializeNodes(dsOut);
	SerializeLinks(dsOut,rootNodeIndexTable);
}
void raytracing::GroupNodeDesc::SerializeNodes(DataStream &dsOut) const
{
	NodeDesc::SerializeNodes(dsOut);
	dsOut->Write<uint32_t>(m_nodes.size());
	for(auto &node : m_nodes)
	{
		dsOut->Write<bool>(node->IsGroupNode());
		node->SerializeNodes(dsOut);
	}
}
void raytracing::GroupNodeDesc::SerializeLinks(DataStream &dsOut,const std::unordered_map<const NodeDesc*,uint64_t> &nodeIndexTable)
{
	auto fWriteNodeLinks = [&dsOut,&nodeIndexTable](const GroupNodeDesc &node) {
		dsOut->Write<uint32_t>(node.m_links.size());
		for(auto &link : node.m_links)
			link.Serialize(dsOut,nodeIndexTable);
	};

	std::function<void(const GroupNodeDesc&)> fWriteLinks = nullptr;
	fWriteLinks = [&fWriteLinks,&fWriteNodeLinks](const GroupNodeDesc &node) {
		if(node.IsGroupNode() == false)
			return;
		fWriteNodeLinks(node);
		for(auto &child : node.m_nodes)
		{
			if(child->IsGroupNode() == false)
				continue;
			fWriteLinks(static_cast<GroupNodeDesc&>(*child));
		}
	};
	fWriteLinks(*this);
}
void raytracing::GroupNodeDesc::Deserialize(DataStream &dsIn)
{
	DeserializeNodes(dsIn);

	std::vector<const NodeDesc*> rootNodeIndexTable;
	// Root node; Build index list
	std::function<void(const NodeDesc&)> fBuildIndexTable = nullptr;
	fBuildIndexTable = [&fBuildIndexTable,&rootNodeIndexTable](const NodeDesc &node) {
		if(rootNodeIndexTable.size() == rootNodeIndexTable.capacity())
			rootNodeIndexTable.reserve(rootNodeIndexTable.size() *1.5 +100);
		rootNodeIndexTable.push_back(&node);
		if(node.IsGroupNode() == false)
			return;
		auto &nodeGroup = static_cast<const GroupNodeDesc&>(node);
		for(auto &child : nodeGroup.GetChildNodes())
			fBuildIndexTable(*child);
	};
	fBuildIndexTable(*this);

	DeserializeLinks(dsIn,rootNodeIndexTable);
}
void raytracing::GroupNodeDesc::DeserializeNodes(DataStream &dsIn)
{
	NodeDesc::DeserializeNodes(dsIn);
	auto numNodes = dsIn->Read<uint32_t>();
	m_nodes.reserve(numNodes);
	for(auto i=decltype(numNodes){0u};i<numNodes;++i)
	{
		auto isGroupNode = dsIn->Read<bool>();
		auto node = isGroupNode ? GroupNodeDesc::Create(m_nodeManager,this) : NodeDesc::Create(this);
		node->DeserializeNodes(dsIn);
		m_nodes.push_back(node);
	}
}
void raytracing::GroupNodeDesc::DeserializeLinks(DataStream &dsIn,const std::vector<const NodeDesc*> &nodeIndexTable)
{
	auto fReadNodeLinks = [&dsIn,&nodeIndexTable](GroupNodeDesc &node) {
		auto numLinks = dsIn->Read<uint32_t>();
		node.m_links.resize(numLinks);
		for(auto &link : node.m_links)
			link.Deserialize(node,dsIn,nodeIndexTable);
	};

	std::function<void(GroupNodeDesc&)> fReadLinks = nullptr;
	fReadLinks = [&fReadLinks,&fReadNodeLinks](GroupNodeDesc &node) {
		if(node.IsGroupNode() == false)
			return;
		fReadNodeLinks(node);
		for(auto &child : node.m_nodes)
		{
			if(child->IsGroupNode() == false)
				continue;
			fReadLinks(static_cast<GroupNodeDesc&>(*child));
		}
	};
	fReadLinks(*this);
}
void raytracing::GroupNodeDesc::Link(NodeDesc &fromNode,const std::string &fromSocket,NodeDesc &toNode,const std::string &toSocket)
{
	Link(fromNode.GetOutputSocket(fromSocket),toNode.GetInputSocket(toSocket));
}
void raytracing::GroupNodeDesc::Link(const Socket &fromSocket,const Socket &toSocket)
{
	if(toSocket.IsConcreteValue())
	{
		throw Exception{"To-Socket " +toSocket.ToString() +" is a concrete type, which cannot be linked to!"};
		return; // Can't link to a concrete type
	}
	std::string toSocketName;
	auto *pToNode = toSocket.GetNode(toSocketName);
	if(pToNode == nullptr)
	{
		throw Exception{"To-Socket " +toSocket.ToString() +" references non-existing node!"};
		return;
	}
	NodeSocketDesc *toDesc = nullptr;
	if(toSocket.IsOutputSocket())
	{
		if(pToNode->IsGroupNode() == false)
		{
			throw Exception{"To-Socket is an output socket, which is only allowed for group nodes!"};
			return;
		}
		toDesc = pToNode->FindOutputSocketDesc(toSocketName);
	}
	else
		toDesc = pToNode->FindInputSocketDesc(toSocketName);
	if(toDesc == nullptr)
		toDesc = pToNode->FindPropertyDesc(toSocketName);
	if(toDesc == nullptr)
	{
		throw Exception{"To-Socket " +toSocket.ToString() +" references invalid socket '" +toSocketName +"' of node " +pToNode->ToString() +"!"};
		return;
	}
	if(fromSocket.IsConcreteValue())
	{
		auto fromValue = fromSocket.GetValue();
		if(fromValue.has_value())
		{
			auto toValue = convert(fromValue->value.get(),fromValue->type,toDesc->dataValue.type);
			if(toValue.has_value() == false)
			{
				throw Exception{"From-Socket " +fromSocket.ToString() +" is concrete type, but value type is not compatible with to-Socket " +toSocket.ToString() +"!"};
				return;
			}
			else
				toDesc->dataValue = *toValue;
		}
		return;
	}
	std::string fromSocketName;
	auto *pFromNode = fromSocket.GetNode(fromSocketName);
	if(pFromNode == nullptr)
	{
		throw Exception{"From-Socket " +fromSocket.ToString() +" references non-existing node!"};
		return;
	}
	auto *fromDesc = pFromNode->FindOutputSocketDesc(fromSocketName);
	if(fromDesc == nullptr)
	{
		if(pFromNode->IsGroupNode() == false)
		{
			throw Exception{"From-Socket is an input socket, which is only allowed for group nodes!"};
			return;
		}
		fromDesc = pFromNode->FindInputSocketDesc(fromSocketName);
		if(fromDesc == nullptr)
			fromDesc = pFromNode->FindPropertyDesc(fromSocketName);
	}

	// If there is already a link to the to-socket, break it up
	auto itLink = std::find_if(m_links.begin(),m_links.end(),[&toSocket](const NodeDescLink &link) {
		return link.toSocket == toSocket;
	});
	if(itLink != m_links.end())
		m_links.erase(itLink);
	if(m_links.size() == m_links.capacity())
		m_links.reserve(m_links.size() *1.5 +20);
	m_links.push_back({});
	auto &link = m_links.back();
	link.fromSocket = fromSocket;
	link.toSocket = toSocket;
	return;
}

//////////////////////

void raytracing::Shader::SetActivePass(Pass pass) {m_activePass = pass;}
std::shared_ptr<raytracing::GroupNodeDesc> raytracing::Shader::GetActivePassNode() const
{
	switch(m_activePass)
	{
	case Pass::Combined:
		return combinedPass;
	case Pass::Albedo:
		return albedoPass;
	case Pass::Normal:
		return normalPass;
	case Pass::Depth:
		return depthPass;
	}
	return nullptr;
}

void raytracing::Shader::Serialize(DataStream &dsOut) const
{
	std::array<std::shared_ptr<raytracing::GroupNodeDesc>,4> passes = {combinedPass,albedoPass,normalPass,depthPass};
	uint32_t flags = 0;
	for(auto i=decltype(passes.size()){0u};i<passes.size();++i)
	{
		if(passes.at(i))
			flags |= 1<<i;
	}
	dsOut->Write<uint32_t>(flags);
	for(auto &pass : passes)
	{
		if(pass == nullptr)
			continue;
		pass->Serialize(dsOut);
	}
}
void raytracing::Shader::Deserialize(DataStream &dsIn,NodeManager &nodeManager)
{
	std::array<std::reference_wrapper<std::shared_ptr<raytracing::GroupNodeDesc>>,4> passes = {combinedPass,albedoPass,normalPass,depthPass};
	auto flags = dsIn->Read<uint32_t>();
	for(auto i=decltype(passes.size()){0u};i<passes.size();++i)
	{
		if((flags &(1<<i)) == 0)
			continue;
		passes.at(i).get() = GroupNodeDesc::Create(nodeManager);
		passes.at(i).get()->Deserialize(dsIn);
	}
}
raytracing::Shader::Shader()
{}
void raytracing::Shader::Initialize() {}
void raytracing::Shader::Finalize() {}

//////////////////////

std::shared_ptr<raytracing::NodeManager> raytracing::NodeManager::Create()
{
	auto nm = std::shared_ptr<NodeManager>{new NodeManager{}};
	nm->RegisterNodeTypes();
	return nm;
}
raytracing::NodeTypeId raytracing::NodeManager::RegisterNodeType(const std::string &typeName,const std::function<std::shared_ptr<NodeDesc>(GroupNodeDesc*)> &factory)
{
	auto lTypeName = typeName;
	ustring::to_lower(lTypeName);
	auto it = std::find_if(m_nodeTypes.begin(),m_nodeTypes.end(),[&lTypeName](const NodeType &nt) {
		return nt.typeName == lTypeName;
	});
	if(it == m_nodeTypes.end())
	{
		if(m_nodeTypes.size() == m_nodeTypes.capacity())
			m_nodeTypes.reserve(m_nodeTypes.size() *1.5 +50);
		m_nodeTypes.push_back({});
		it = m_nodeTypes.end() -1;
	}
	auto &desc = *it;
	desc.typeName = lTypeName;
	desc.factory = factory;
	return it -m_nodeTypes.begin();
}

std::optional<raytracing::NodeTypeId> raytracing::NodeManager::FindNodeTypeId(const std::string &typeName) const
{
	auto lTypeName = typeName;
	ustring::to_lower(lTypeName);
	auto it = std::find_if(m_nodeTypes.begin(),m_nodeTypes.end(),[&lTypeName](const NodeType &nodeType) {
		return nodeType.typeName == lTypeName;
	});
	if(it == m_nodeTypes.end())
		return {};
	return it -m_nodeTypes.begin();
}

std::shared_ptr<raytracing::NodeDesc> raytracing::NodeManager::CreateNode(const std::string &typeName,GroupNodeDesc *parent) const
{
	auto typeId = FindNodeTypeId(typeName);
	if(typeId.has_value() == false)
		return nullptr;
	return CreateNode(*typeId,parent);
}
std::shared_ptr<raytracing::NodeDesc> raytracing::NodeManager::CreateNode(NodeTypeId id,GroupNodeDesc *parent) const
{
	if(id >= m_nodeTypes.size())
		return nullptr;
	auto node = m_nodeTypes.at(id).factory(parent);
	if(node == nullptr)
		return nullptr;
	node->SetTypeName(m_nodeTypes.at(id).typeName);
	return node;
}

void raytracing::NodeManager::RegisterNodeTypes()
{
	RegisterNodeType(NODE_MATH,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::math::IN_TYPE,ccl::NodeMathType::NODE_MATH_ADD);
		desc->RegisterSocket<raytracing::SocketType::Bool>(nodes::math::IN_USE_CLAMP,false);

		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::math::IN_VALUE1,0.5f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::math::IN_VALUE2,0.5f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::math::IN_VALUE3,0.f,SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::math::OUT_VALUE,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::math::OUT_VALUE);
		return desc;
	});
	RegisterNodeType(NODE_HSV,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::hsv::IN_HUE,0.5f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::hsv::IN_SATURATION,1.0f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::hsv::IN_VALUE,1.0f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::hsv::IN_FAC,1.0f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::hsv::IN_COLOR,STColor{},SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::hsv::OUT_COLOR,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::hsv::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_SEPARATE_XYZ,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::separate_xyz::IN_VECTOR,STColor{},SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::separate_xyz::OUT_X,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::separate_xyz::OUT_Y,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::separate_xyz::OUT_Z,SocketIO::Out);
		return desc;
	});
	RegisterNodeType(NODE_COMBINE_XYZ,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::combine_xyz::IN_X,0.0f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::combine_xyz::IN_Y,0.0f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::combine_xyz::IN_Z,0.0f,SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::combine_xyz::OUT_VECTOR,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::combine_xyz::OUT_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_SEPARATE_RGB,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::separate_rgb::IN_COLOR,STColor{},SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::separate_rgb::OUT_R,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::separate_rgb::OUT_G,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::separate_rgb::OUT_B,SocketIO::Out);
		return desc;
	});
	RegisterNodeType(NODE_COMBINE_RGB,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::combine_rgb::IN_R,0.f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::combine_rgb::IN_G,0.f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::combine_rgb::IN_B,0.f,SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::combine_rgb::OUT_IMAGE,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::combine_rgb::OUT_IMAGE);
		return desc;
	});
	RegisterNodeType(NODE_GEOMETRY,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::geometry::IN_NORMAL_OSL,STNormal{},SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::geometry::OUT_POSITION,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::geometry::OUT_NORMAL,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::geometry::OUT_TANGENT,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::geometry::OUT_TRUE_NORMAL,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::geometry::OUT_INCOMING,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::geometry::OUT_PARAMETRIC,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::geometry::OUT_BACKFACING,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::geometry::OUT_POINTINESS,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::geometry::OUT_RANDOM_PER_ISLAND,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::geometry::OUT_POSITION);
		return desc;
	});
	RegisterNodeType(NODE_CAMERA_INFO,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::camera_info::OUT_VIEW_VECTOR,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::camera_info::OUT_VIEW_Z_DEPTH,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::camera_info::OUT_VIEW_DISTANCE,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::camera_info::OUT_VIEW_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_IMAGE_TEXTURE,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::String>(nodes::image_texture::IN_FILENAME,STString{});
		desc->RegisterSocket<raytracing::SocketType::String>(nodes::image_texture::IN_COLORSPACE,STString{ccl::u_colorspace_auto});
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::image_texture::IN_ALPHA_TYPE,ccl::ImageAlphaType::IMAGE_ALPHA_AUTO);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::image_texture::IN_INTERPOLATION,ccl::InterpolationType::INTERPOLATION_LINEAR);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::image_texture::IN_EXTENSION,ccl::ExtensionType::EXTENSION_REPEAT);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::image_texture::IN_PROJECTION,ccl::NodeImageProjection::NODE_IMAGE_PROJ_FLAT);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::image_texture::IN_PROJECTION_BLEND,0.0f);

		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::image_texture::IN_VECTOR,STPoint{},SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::image_texture::OUT_COLOR,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::image_texture::OUT_ALPHA,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::image_texture::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_ENVIRONMENT_TEXTURE,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::String>(nodes::environment_texture::IN_FILENAME,STString{});
		desc->RegisterSocket<raytracing::SocketType::String>(nodes::environment_texture::IN_COLORSPACE,STString{ccl::u_colorspace_auto});
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::environment_texture::IN_ALPHA_TYPE,ccl::ImageAlphaType::IMAGE_ALPHA_AUTO);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::environment_texture::IN_INTERPOLATION,ccl::InterpolationType::INTERPOLATION_LINEAR);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::environment_texture::IN_PROJECTION,ccl::NodeEnvironmentProjection::NODE_ENVIRONMENT_EQUIRECTANGULAR);
		
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::environment_texture::IN_VECTOR,STVector{},raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::environment_texture::OUT_COLOR,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::environment_texture::OUT_ALPHA,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::environment_texture::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_MIX_CLOSURE,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::mix_closure::IN_FAC,0.5f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::mix_closure::IN_CLOSURE1,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::mix_closure::IN_CLOSURE2,SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::mix_closure::OUT_CLOSURE,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::mix_closure::OUT_CLOSURE);
		return desc;
	});
	RegisterNodeType(NODE_ADD_CLOSURE,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::add_closure::IN_CLOSURE1,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::add_closure::IN_CLOSURE2,SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::add_closure::OUT_CLOSURE,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::add_closure::OUT_CLOSURE);
		return desc;
	});
	RegisterNodeType(NODE_BACKGROUND_SHADER,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::background_shader::IN_COLOR,STColor{0.8f,0.8f,0.8f},SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::background_shader::IN_STRENGTH,1.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::background_shader::IN_SURFACE_MIX_WEIGHT,0.0f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::background_shader::OUT_BACKGROUND,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::background_shader::OUT_BACKGROUND);
		return desc;
	});
	RegisterNodeType(NODE_TEXTURE_COORDINATE,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Bool>(nodes::texture_coordinate::IN_FROM_DUPLI,false);
		desc->RegisterSocket<raytracing::SocketType::Bool>(nodes::texture_coordinate::IN_USE_TRANSFORM,false);
		desc->RegisterSocket<raytracing::SocketType::Transform>(nodes::texture_coordinate::IN_OB_TFM,STTransform{1.f,0,0,0,0,1.f,0,0,0,0,1.f,0});

		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::texture_coordinate::IN_NORMAL_OSL,STVector{},SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::texture_coordinate::OUT_GENERATED,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::texture_coordinate::OUT_NORMAL,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::texture_coordinate::OUT_UV,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::texture_coordinate::OUT_OBJECT,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::texture_coordinate::OUT_CAMERA,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::texture_coordinate::OUT_WINDOW,SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::texture_coordinate::OUT_REFLECTION,SocketIO::Out);
		return desc;
	});
	RegisterNodeType(NODE_MAPPING,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::mapping::IN_TYPE,ccl::NodeMappingType::NODE_MAPPING_TYPE_POINT);

		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::mapping::IN_VECTOR,STPoint{},SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::mapping::IN_LOCATION,STPoint{},SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Point,Vector3>(nodes::mapping::IN_ROTATION,STPoint{},SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Point>(nodes::mapping::IN_SCALE,STPoint{1.f,1.f,1.f},SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::mapping::OUT_VECTOR,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::mapping::OUT_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_SCATTER_VOLUME,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::scatter_volume::IN_COLOR,STColor{0.8f,0.8f,0.8f},SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::scatter_volume::IN_DENSITY,1.0f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::scatter_volume::IN_ANISOTROPY,0.0f,SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::scatter_volume::IN_VOLUME_MIX_WEIGHT,0.0f,SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::scatter_volume::OUT_VOLUME,SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::scatter_volume::OUT_VOLUME);
		return desc;
	});
	RegisterNodeType(NODE_EMISSION,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::emission::IN_COLOR,STColor{0.8f,0.8f,0.8f},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::emission::IN_STRENGTH,1.0f,raytracing::SocketIO::In); // Default in Cycles is 10, which is a little excessive for our purposes
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::emission::IN_SURFACE_MIX_WEIGHT,0.0f,raytracing::SocketIO::In);
		
		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::emission::OUT_EMISSION,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::emission::OUT_EMISSION);
		return desc;
	});
	RegisterNodeType(NODE_COLOR,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::color::IN_VALUE,STColor{});

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::color::OUT_COLOR,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::color::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_ATTRIBUTE,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::String>(nodes::attribute::IN_ATTRIBUTE,STString{});

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::attribute::OUT_COLOR,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::attribute::OUT_VECTOR,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::attribute::OUT_FAC,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::attribute::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_LIGHT_PATH,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_IS_CAMERA_RAY,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_IS_SHADOW_RAY,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_IS_DIFFUSE_RAY,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_IS_GLOSSY_RAY,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_IS_SINGULAR_RAY,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_IS_REFLECTION_RAY,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_IS_TRANSMISSION_RAY,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_IS_VOLUME_SCATTER_RAY,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_RAY_LENGTH,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_RAY_DEPTH,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_DIFFUSE_DEPTH,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_GLOSSY_DEPTH,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_TRANSPARENT_DEPTH,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::light_path::OUT_TRANSMISSION_DEPTH,raytracing::SocketIO::Out);
		return desc;
	});
	RegisterNodeType(NODE_TRANSPARENT_BSDF,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::transparent_bsdf::IN_COLOR,STColor{1.f,1.f,1.f},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::transparent_bsdf::IN_SURFACE_MIX_WEIGHT,0.f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::transparent_bsdf::OUT_BSDF,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::transparent_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_TRANSLUCENT_BSDF,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::translucent_bsdf::IN_COLOR,STColor{0.8f,0.8f,0.8f},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::translucent_bsdf::IN_NORMAL,STNormal{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::translucent_bsdf::IN_SURFACE_MIX_WEIGHT,0.f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::translucent_bsdf::OUT_BSDF,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::translucent_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_DIFFUSE_BSDF,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::diffuse_bsdf::IN_COLOR,STColor{0.8f,0.8f,0.8f},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::diffuse_bsdf::IN_NORMAL,STNormal{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::diffuse_bsdf::IN_SURFACE_MIX_WEIGHT,0.f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::diffuse_bsdf::IN_ROUGHNESS,0.f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::diffuse_bsdf::OUT_BSDF,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::diffuse_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_NORMAL_MAP,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::normal_map::IN_SPACE,ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_TANGENT);
		desc->RegisterSocket<raytracing::SocketType::String>(nodes::normal_map::IN_ATTRIBUTE,STString{});

		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::normal_map::IN_NORMAL_OSL,STNormal{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::normal_map::IN_STRENGTH,1.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::normal_map::IN_COLOR,STColor{0.5f,0.5f,1.0f},raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::normal_map::OUT_NORMAL,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::normal_map::OUT_NORMAL);
		return desc;
	});
	RegisterNodeType(NODE_PRINCIPLED_BSDF,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::principled_bsdf::IN_DISTRIBUTION,ccl::ClosureType::CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::principled_bsdf::IN_SUBSURFACE_METHOD,ccl::ClosureType::CLOSURE_BSSRDF_PRINCIPLED_ID);

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::principled_bsdf::IN_BASE_COLOR,STColor{0.8f,0.8f,0.8f},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::principled_bsdf::IN_SUBSURFACE_COLOR,STColor{0.8f,0.8f,0.8f},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_METALLIC,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_SUBSURFACE,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::principled_bsdf::IN_SUBSURFACE_RADIUS,STVector{0.1f,0.1f,0.1f},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_SPECULAR,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_ROUGHNESS,0.5f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_SPECULAR_TINT,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_ANISOTROPIC,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_SHEEN,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_SHEEN_TINT,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_CLEARCOAT,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_CLEARCOAT_ROUGHNESS,0.03f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_IOR,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_TRANSMISSION,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_TRANSMISSION_ROUGHNESS,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_ANISOTROPIC_ROTATION,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::principled_bsdf::IN_EMISSION,STColor{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_ALPHA,1.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::principled_bsdf::IN_NORMAL,STNormal{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::principled_bsdf::IN_CLEARCOAT_NORMAL,STNormal{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::principled_bsdf::IN_TANGENT,STNormal{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::principled_bsdf::IN_SURFACE_MIX_WEIGHT,0.0f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::principled_bsdf::OUT_BSDF,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::principled_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_TOON_BSDF,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::toon_bsdf::IN_COMPONENT,ccl::ClosureType::CLOSURE_BSDF_DIFFUSE_TOON_ID);

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::toon_bsdf::IN_COLOR,STColor{0.8f,0.8f,0.8f},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::toon_bsdf::IN_NORMAL,STNormal{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::toon_bsdf::IN_SURFACE_MIX_WEIGHT,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::toon_bsdf::IN_SIZE,0.5f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::toon_bsdf::IN_SMOOTH,0.0f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::toon_bsdf::OUT_BSDF,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::toon_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_GLASS_BSDF,[](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::glass_bsdf::IN_DISTRIBUTION,ccl::ClosureType::CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::glass_bsdf::IN_COLOR,STColor{0.8f,0.8f,0.8f},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::glass_bsdf::IN_NORMAL,STNormal{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::glass_bsdf::IN_SURFACE_MIX_WEIGHT,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::glass_bsdf::IN_ROUGHNESS,0.0f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::glass_bsdf::IN_IOR,0.3f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::glass_bsdf::OUT_BSDF,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::glass_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_OUTPUT,[](GroupNodeDesc *parent) {
		auto desc =  NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::output::IN_SURFACE,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Closure>(nodes::output::IN_VOLUME,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::output::IN_DISPLACEMENT,STVector{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::output::IN_NORMAL,STNormal{},raytracing::SocketIO::In);
		return desc;
	});
	RegisterNodeType(NODE_VECTOR_MATH,[](GroupNodeDesc *parent) {
		auto desc =  NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::vector_math::IN_TYPE,ccl::NodeVectorMathType::NODE_VECTOR_MATH_ADD);
		
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::vector_math::IN_VECTOR1,STVector{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::vector_math::IN_VECTOR2,STVector{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::vector_math::IN_SCALE,1.f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::vector_math::OUT_VALUE,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::vector_math::OUT_VECTOR,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::vector_math::OUT_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_MIX,[](GroupNodeDesc *parent) {
		auto desc =  NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::mix::IN_TYPE,ccl::NodeMix::NODE_MIX_BLEND);
		desc->RegisterSocket<raytracing::SocketType::Bool>(nodes::mix::IN_USE_CLAMP,false);
		
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::mix::IN_FAC,0.5f,raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::mix::IN_COLOR1,STColor{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::mix::IN_COLOR2,STColor{},raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::mix::OUT_COLOR,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::mix::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_RGB_TO_BW,[](GroupNodeDesc *parent) {
		auto desc =  NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::rgb_to_bw::IN_COLOR,STColor{},raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::rgb_to_bw::OUT_VAL,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::rgb_to_bw::OUT_VAL);
		return desc;
	});
	RegisterNodeType(NODE_INVERT,[](GroupNodeDesc *parent) {
		auto desc =  NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::invert::IN_COLOR,STColor{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::invert::IN_FAC,STFloat{1.f},raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::invert::OUT_COLOR,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::invert::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_VECTOR_TRANSFORM,[](GroupNodeDesc *parent) {
		auto desc =  NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::vector_transform::IN_TYPE,ccl::NodeVectorTransformType::NODE_VECTOR_TRANSFORM_TYPE_VECTOR);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::vector_transform::IN_CONVERT_FROM,ccl::NodeVectorTransformConvertSpace::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_WORLD);
		desc->RegisterSocket<raytracing::SocketType::Enum>(nodes::vector_transform::IN_CONVERT_TO,ccl::NodeVectorTransformConvertSpace::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_OBJECT);
		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::vector_transform::IN_VECTOR,STVector{},raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Vector>(nodes::vector_transform::OUT_VECTOR,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::vector_transform::OUT_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_RGB_RAMP,[](GroupNodeDesc *parent) {
		auto desc =  NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::ColorArray>(nodes::rgb_ramp::IN_RAMP,STColorArray{});
		desc->RegisterSocket<raytracing::SocketType::FloatArray>(nodes::rgb_ramp::IN_RAMP_ALPHA,STFloatArray{});
		desc->RegisterSocket<raytracing::SocketType::Bool>(nodes::rgb_ramp::IN_INTERPOLATE,true);
		
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::rgb_ramp::IN_FAC,0.f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Color>(nodes::rgb_ramp::OUT_COLOR,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::rgb_ramp::OUT_ALPHA,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::rgb_ramp::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_LAYER_WEIGHT,[](GroupNodeDesc *parent) {
		auto desc =  NodeDesc::Create(parent);
		desc->RegisterSocket<raytracing::SocketType::Normal>(nodes::layer_weight::IN_NORMAL,STNormal{},raytracing::SocketIO::In);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::layer_weight::IN_BLEND,0.5f,raytracing::SocketIO::In);

		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::layer_weight::OUT_FRESNEL,raytracing::SocketIO::Out);
		desc->RegisterSocket<raytracing::SocketType::Float>(nodes::layer_weight::OUT_FACING,raytracing::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::layer_weight::OUT_FRESNEL);
		return desc;
	});
	static_assert(NODE_COUNT == 35,"Increase this number if new node types are added!");
}

std::ostream& operator<<(std::ostream &os,const raytracing::NodeDesc &desc) {os<<desc.ToString(); return os;}
std::ostream& operator<<(std::ostream &os,const raytracing::GroupNodeDesc &desc) {os<<desc.ToString(); return os;}
#pragma optimize("",on)
