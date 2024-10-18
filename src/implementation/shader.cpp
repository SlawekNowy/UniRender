/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include <iostream>
#include <sharedutils/datastream.h>
#include <sharedutils/util_pragma.hpp>
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>
#include <mathutil/uvec.h>
#include <queue>
#include <cassert>

module pragma.scenekit;

import :shader;

const std::string pragma::scenekit::COLORSPACE_AUTO = "";
const std::string pragma::scenekit::COLORSPACE_RAW = "__builtin_raw";
const std::string pragma::scenekit::COLORSPACE_SRGB = "__builtin_srgb";

//////////////////////

void pragma::scenekit::NodeDescLink::Serialize(DataStream &dsOut, const std::unordered_map<const NodeDesc *, uint64_t> &nodeIndexTable) const
{
	fromSocket.Serialize(dsOut, nodeIndexTable);
	toSocket.Serialize(dsOut, nodeIndexTable);
}
void pragma::scenekit::NodeDescLink::Deserialize(GroupNodeDesc &groupNode, DataStream &dsIn, const std::vector<const NodeDesc *> &nodeIndexTable)
{
	fromSocket.Deserialize(groupNode, dsIn, nodeIndexTable);
	toSocket.Deserialize(groupNode, dsIn, nodeIndexTable);
}

//////////////////////

pragma::scenekit::NodeSocketDesc pragma::scenekit::NodeSocketDesc::Deserialize(DataStream &dsIn)
{
	NodeSocketDesc desc {};
	desc.io = dsIn->Read<decltype(desc.io)>();
	desc.dataValue = DataValue::Deserialize(dsIn);
	return desc;
}
void pragma::scenekit::NodeSocketDesc::Serialize(DataStream &dsOut) const
{
	dsOut->Write(io);
	dataValue.Serialize(dsOut);
}

//////////////////////

template<class TNodeDesc>
std::shared_ptr<TNodeDesc> pragma::scenekit::NodeDesc::Create(GroupNodeDesc *parent)
{
	auto node = std::shared_ptr<TNodeDesc> {new TNodeDesc {}};
	node->SetParent(parent);
	return node;
}
std::shared_ptr<pragma::scenekit::NodeDesc> pragma::scenekit::NodeDesc::Create(GroupNodeDesc *parent) { return Create<NodeDesc>(parent); }
pragma::scenekit::NodeDesc::NodeDesc() {}
std::string pragma::scenekit::NodeDesc::GetName() const { return m_name; }
const std::string &pragma::scenekit::NodeDesc::GetTypeName() const { return m_typeName; }
std::string pragma::scenekit::NodeDesc::ToString() const { return "Node[" + GetName() + "][" + GetTypeName() + "]"; }
void pragma::scenekit::NodeDesc::SetTypeName(const std::string &typeName) { m_typeName = typeName; }
pragma::scenekit::NodeIndex pragma::scenekit::NodeDesc::GetIndex() const
{
	auto *parent = GetParent();
	if(parent == nullptr)
		return std::numeric_limits<NodeIndex>::max();
	auto &nodes = parent->GetChildNodes();
	auto it = std::find_if(nodes.begin(), nodes.end(), [this](const std::shared_ptr<pragma::scenekit::NodeDesc> &node) { return node.get() == this; });
	if(it == nodes.end())
		throw Exception {"Node references parent which it doesn't belong to"};
	return it - nodes.begin();
}

void pragma::scenekit::NodeDesc::RegisterPrimaryOutputSocket(const std::string &name) { m_primaryOutputSocket = name; }

pragma::scenekit::NodeDesc::operator pragma::scenekit::Socket() const { return *GetPrimaryOutputSocket(); }

pragma::scenekit::Socket pragma::scenekit::NodeDesc::RegisterSocket(const std::string &name, const DataValue &value, SocketIO io)
{
	NodeSocketDesc socketDesc {};
	socketDesc.io = io;
	socketDesc.dataValue = value;
	switch(io) {
	case SocketIO::In:
		m_inputs.insert(std::make_pair(name, socketDesc));
		return GetInputSocket(name);
	case SocketIO::Out:
		m_outputs.insert(std::make_pair(name, socketDesc));
		return GetOutputSocket(name);
	default:
		m_properties.insert(std::make_pair(name, socketDesc));
		return GetProperty(name);
	}
}

const std::unordered_map<std::string, pragma::scenekit::NodeSocketDesc> &pragma::scenekit::NodeDesc::GetInputs() const { return m_inputs; }
const std::unordered_map<std::string, pragma::scenekit::NodeSocketDesc> &pragma::scenekit::NodeDesc::GetOutputs() const { return m_outputs; }
const std::unordered_map<std::string, pragma::scenekit::NodeSocketDesc> &pragma::scenekit::NodeDesc::GetProperties() const { return m_properties; }

pragma::scenekit::Socket pragma::scenekit::NodeDesc::GetInputSocket(const std::string &name)
{
	auto socket = FindInputSocket(name);
	assert(socket.has_value());
	if(socket.has_value() == false)
		throw Exception {ToString() + " has no input socket named '" + name + "'!"};
	return *socket;
}
pragma::scenekit::Socket pragma::scenekit::NodeDesc::GetOutputSocket(const std::string &name)
{
	auto socket = FindOutputSocket(name);
	assert(socket.has_value());
	if(socket.has_value() == false)
		throw Exception {ToString() + " has no output socket named '" + name + "'!"};
	return *socket;
}
pragma::scenekit::Socket pragma::scenekit::NodeDesc::GetProperty(const std::string &name)
{
	auto socket = FindProperty(name);
	assert(socket.has_value());
	if(socket.has_value() == false)
		throw Exception {ToString() + " has no property named '" + name + "'!"};
	return *socket;
}
pragma::scenekit::Socket pragma::scenekit::NodeDesc::GetInputOrProperty(const std::string &name)
{
	auto socket = FindInputSocket(name);
	if(socket.has_value())
		return *socket;
	return GetProperty(name);
}
std::optional<pragma::scenekit::Socket> pragma::scenekit::NodeDesc::GetPrimaryOutputSocket() const { return m_primaryOutputSocket ? const_cast<NodeDesc *>(this)->FindOutputSocket(*m_primaryOutputSocket) : std::optional<pragma::scenekit::Socket> {}; }
pragma::scenekit::NodeSocketDesc *pragma::scenekit::NodeDesc::FindInputSocketDesc(const std::string &name)
{
	auto it = m_inputs.find(name);
	if(it == m_inputs.end())
		return nullptr;
	return &it->second;
}
pragma::scenekit::NodeSocketDesc *pragma::scenekit::NodeDesc::FindOutputSocketDesc(const std::string &name)
{
	auto it = m_outputs.find(name);
	if(it == m_outputs.end())
		return nullptr;
	return &it->second;
}
pragma::scenekit::NodeSocketDesc *pragma::scenekit::NodeDesc::FindPropertyDesc(const std::string &name)
{
	auto it = m_properties.find(name);
	if(it == m_properties.end())
		return nullptr;
	return &it->second;
}
pragma::scenekit::NodeSocketDesc *pragma::scenekit::NodeDesc::FindSocketDesc(const Socket &socket)
{
	if(socket.IsConcreteValue())
		return nullptr;
	std::string socketName;
	socket.GetNode(socketName);
	if(socket.IsOutputSocket())
		return FindOutputSocketDesc(socketName);
	return FindInputSocketDesc(socketName);
}
pragma::scenekit::NodeSocketDesc *pragma::scenekit::NodeDesc::FindInputOrPropertyDesc(const std::string &name)
{
	auto *inputDesc = FindInputSocketDesc(name);
	if(inputDesc == nullptr)
		inputDesc = FindPropertyDesc(name);
	return inputDesc;
}
pragma::scenekit::GroupNodeDesc *pragma::scenekit::NodeDesc::GetParent() const { return m_parent.lock().get(); }
void pragma::scenekit::NodeDesc::SetParent(GroupNodeDesc *parent) { m_parent = parent ? std::static_pointer_cast<GroupNodeDesc>(parent->shared_from_this()) : std::weak_ptr<GroupNodeDesc> {}; }
std::optional<pragma::scenekit::Socket> pragma::scenekit::NodeDesc::FindInputSocket(const std::string &name)
{
	auto *desc = FindInputSocketDesc(name);
	return desc ? Socket {*this, name, false} : std::optional<pragma::scenekit::Socket> {};
}
std::optional<pragma::scenekit::Socket> pragma::scenekit::NodeDesc::FindOutputSocket(const std::string &name)
{
	auto *desc = FindOutputSocketDesc(name);
	return desc ? Socket {*this, name, true} : std::optional<pragma::scenekit::Socket> {};
}
std::optional<pragma::scenekit::Socket> pragma::scenekit::NodeDesc::FindProperty(const std::string &name)
{
	auto *desc = FindPropertyDesc(name);
	return desc ? Socket {*this, name, false} : std::optional<pragma::scenekit::Socket> {};
}
void pragma::scenekit::NodeDesc::SerializeNodes(DataStream &dsOut) const
{
	dsOut->WriteString(m_typeName);
	dsOut->WriteString(m_name);
	auto fWriteProperties = [&dsOut](const std::unordered_map<std::string, NodeSocketDesc> &props) {
		dsOut->Write<uint32_t>(props.size());
		for(auto &pair : props) {
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
void pragma::scenekit::NodeDesc::DeserializeNodes(DataStream &dsIn)
{
	m_typeName = dsIn->ReadString();
	m_name = dsIn->ReadString();
	auto fReadProperties = [&dsIn](std::unordered_map<std::string, NodeSocketDesc> &props) {
		auto n = dsIn->Read<uint32_t>();
		props.reserve(n);
		for(auto i = decltype(n) {0u}; i < n; ++i) {
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

std::shared_ptr<pragma::scenekit::GroupNodeDesc> pragma::scenekit::GroupNodeDesc::Create(NodeManager &nodeManager, GroupNodeDesc *parent)
{
	auto node = std::shared_ptr<GroupNodeDesc> {new GroupNodeDesc {nodeManager}};
	node->SetParent(parent);
	return node;
}
pragma::scenekit::GroupNodeDesc::GroupNodeDesc(NodeManager &nodeManager) : NodeDesc {}, m_nodeManager {nodeManager} {}
const std::vector<std::shared_ptr<pragma::scenekit::NodeDesc>> &pragma::scenekit::GroupNodeDesc::GetChildNodes() const { return m_nodes; }
const std::vector<pragma::scenekit::NodeDescLink> &pragma::scenekit::GroupNodeDesc::GetLinks() const { return m_links; }
pragma::scenekit::NodeDesc *pragma::scenekit::GroupNodeDesc::FindNode(const std::string &name)
{
	auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&name](const std::shared_ptr<NodeDesc> &desc) { return desc->GetName() == name; });
	return (it != m_nodes.end()) ? it->get() : nullptr;
}
pragma::scenekit::NodeDesc *pragma::scenekit::GroupNodeDesc::FindNodeByType(const std::string &type)
{
	auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&type](const std::shared_ptr<NodeDesc> &desc) { return desc->GetTypeName() == type; });
	return (it != m_nodes.end()) ? it->get() : nullptr;
}
pragma::scenekit::NodeDesc *pragma::scenekit::GroupNodeDesc::GetNodeByIndex(NodeIndex idx) const
{
	if(idx >= m_nodes.size())
		return nullptr;
	return m_nodes.at(idx).get();
}
std::vector<std::shared_ptr<pragma::scenekit::NodeDesc>>::iterator pragma::scenekit::GroupNodeDesc::ResolveGroupNodes(std::vector<std::shared_ptr<pragma::scenekit::NodeDesc>>::iterator itParent)
{
	auto pnode = shared_from_this(); // Unused, but we need to ensure the node stays valid for the remainder of this function
	auto &children = const_cast<std::vector<std::shared_ptr<pragma::scenekit::NodeDesc>> &>(GetChildNodes());
	for(auto it = children.begin(); it != children.end();) {
		auto &child = *it;
		if(child->IsGroupNode() == false) {
			++it;
			continue;
		}
		it = static_cast<pragma::scenekit::GroupNodeDesc &>(*child).ResolveGroupNodes(it);
	}

	auto *parent = GetParent();
	if(parent == nullptr)
		return {};

	auto &links = GetLinks();
	auto &parentLinks = const_cast<std::vector<pragma::scenekit::NodeDescLink> &>(parent->GetLinks());
	parentLinks.reserve(links.size() + GetInputs().size() + GetOutputs().size());

	// We need to re-direct the incoming and outgoing links for the group node, which all reside in the parent node.
	// There are several cases we have to consider:
	// 1) Input socket does *not* have an incoming link: Its default value has to be applied to whatever node it's linked to in the group node.
	// 1.1) Socket is not linked to anything: We can just ignore the socket entirely
	// 1.2) The node the socket is linked to is an output socket of this group node, or an input-socket of a non-group node within this group node: We'll assign the default value from the input socket to the output socket. In this case it'll be resolved when we resolve the output sockets.
	// 2) Input socket *does* have an incoming link:
	// 2.1) Socket is not linked to anything: We don't need to do anything
	// 2.2) The node the socket is linked to is an output socket of this group node, or an input-socket of a non-group node within this group node: We'll redirect the incoming socket directly to the linked socket. In this case it'll be resolved when we resolve the output sockets.
	std::unordered_map<pragma::scenekit::Socket, pragma::scenekit::NodeDescLink *> incomingLinks;
	std::unordered_map<pragma::scenekit::Socket, std::vector<pragma::scenekit::NodeDescLink *>> outgoingLinks;
	for(auto &link : parentLinks) {
		if(link.toSocket.GetNode() == this) {
			assert(!link.toSocket.IsOutputSocket());
			incomingLinks[link.toSocket] = &link;
		}
		else if(link.fromSocket.GetNode() == this) {
			assert(link.fromSocket.IsOutputSocket());
			auto it = outgoingLinks.find(link.fromSocket);
			if(it == outgoingLinks.end())
				it = outgoingLinks.insert(std::make_pair(link.fromSocket, std::vector<pragma::scenekit::NodeDescLink *> {})).first;
			it->second.push_back(&link);
		}
	}

	std::unordered_map<pragma::scenekit::Socket, std::vector<pragma::scenekit::NodeDescLink *>> internalLinksFromInputs;
	std::unordered_map<pragma::scenekit::Socket, pragma::scenekit::NodeDescLink *> internalLinksToOutputs;
	for(auto &link : links) {
		if(link.fromSocket.GetNode() == this) {
			assert(!link.fromSocket.IsOutputSocket());
			auto it = internalLinksFromInputs.find(link.fromSocket);
			if(it == internalLinksFromInputs.end())
				it = internalLinksFromInputs.insert(std::make_pair(link.fromSocket, std::vector<pragma::scenekit::NodeDescLink *> {})).first;
			it->second.push_back(const_cast<pragma::scenekit::NodeDescLink *>(&link));
		}
		if(link.toSocket.GetNode() == this) {
			assert(link.toSocket.IsOutputSocket());
			internalLinksToOutputs[link.toSocket] = const_cast<pragma::scenekit::NodeDescLink *>(&link);
		}
	}

	std::queue<pragma::scenekit::Socket> clearParentLinks {};
	std::queue<pragma::scenekit::NodeDescLink> newParentLinks {};
	auto fResolveInput = [this, &incomingLinks, &parentLinks, &clearParentLinks, &newParentLinks, &internalLinksFromInputs, &internalLinksToOutputs](const std::string &socketName) {
		pragma::scenekit::Socket socket {*this, socketName, false /* output */};
		auto it = incomingLinks.find(socket);
		if(it == incomingLinks.end()) {
			// Case 1)
			auto itInput = internalLinksFromInputs.find(socket);
			if(itInput == internalLinksFromInputs.end()) {
				// Case 1.1)
				return;
			}
			// Case 1.2)
			std::string inputSocketName, outputSocketName;
			socket.GetNode(inputSocketName);
			auto &links = itInput->second;
			for(auto *link : links) {
				auto *outputNode = link->toSocket.GetNode(outputSocketName);
				if(link->toSocket.IsOutputSocket()) {
					outputNode->FindOutputSocketDesc(outputSocketName)->dataValue = FindInputOrPropertyDesc(inputSocketName)->dataValue;
					auto it = internalLinksToOutputs.find(link->toSocket);
					assert(it != internalLinksToOutputs.end());
					internalLinksToOutputs.erase(it);
				}
				else
					outputNode->FindInputOrPropertyDesc(outputSocketName)->dataValue = FindInputOrPropertyDesc(inputSocketName)->dataValue;
			}
			return;
		}
		// Case 2)
		auto itInput = internalLinksFromInputs.find(socket);
		if(itInput == internalLinksFromInputs.end()) {
			// Case 2.1)
			return;
		}
		// Case 2.2)
		// Note: We need to update the parent links, but we can't do so directly, because that would invalidate the
		// links in incomingLinks and outgoingLinks, so we'll queue the updates here and do them further below.
		clearParentLinks.push(it->second->toSocket);
		auto &links = itInput->second;
		for(auto *link : links) {
			newParentLinks.push({it->second->fromSocket, link->toSocket});
			if(link->toSocket.IsOutputSocket()) {
				auto itOutput = internalLinksToOutputs.find(link->toSocket);
				assert(itOutput != internalLinksToOutputs.end());
				itOutput->second->fromSocket = it->second->fromSocket;
			}
		}
	};

	// Resolve properties
	for(auto &prop : GetProperties())
		fResolveInput(prop.first);

	// Resolve inputs
	for(auto &input : GetInputs())
		fResolveInput(input.first);

	// Now we also have to handle the outputs:
	// 1) Output socket does *not* have an outgoing link: We can ignore the socket entirely
	// 2) Output socket *does* have an outgoing link:
	// 2.1) Socket has no input link: Just assign default value
	// 2.2) Socket has input from non-group node within this group node: Re-direct the from-socket of the outgoing link to the from-socket of the output socket

	// Resolve outputs
	for(auto &output : GetOutputs()) {
		pragma::scenekit::Socket socket {*this, output.first, true /* output */};
		auto it = outgoingLinks.find(socket);
		if(it == outgoingLinks.end()) {
			// Case 1)
			continue;
		}
		auto itOutput = internalLinksToOutputs.find(socket);
		if(itOutput == internalLinksToOutputs.end()) {
			// Case 2.1)
			std::string toSocketName;
			for(auto *link : it->second) {
				auto *toSocketNode = link->toSocket.GetNode(toSocketName);
				std::string outputSocketName;
				socket.GetNode(outputSocketName);
				toSocketNode->FindInputOrPropertyDesc(toSocketName)->dataValue = FindOutputSocketDesc(outputSocketName)->dataValue;
			}
			auto itParentLink = std::find_if(parentLinks.begin(), parentLinks.end(), [&socket](const pragma::scenekit::NodeDescLink &link) { return link.fromSocket == socket; });
			assert(itParentLink != parentLinks.end());
			parentLinks.erase(itParentLink);
			continue;
		}
		// Case 2.2
		for(auto *link : it->second)
			link->fromSocket = itOutput->second->fromSocket;
	}

	// Update input/output links
	while(clearParentLinks.empty() == false) {
		auto &sock = clearParentLinks.front();

		auto it = std::find_if(parentLinks.begin(), parentLinks.end(), [&sock](const pragma::scenekit::NodeDescLink &link) { return link.toSocket == sock; });
		assert(it != parentLinks.end());
		parentLinks.erase(it);

		clearParentLinks.pop();
	}

	parentLinks.reserve(parentLinks.size() + newParentLinks.size());
	while(newParentLinks.empty() == false) {
		auto &link = newParentLinks.front();
		parentLinks.push_back(link);
		newParentLinks.pop();
	}
	//

	// Move non-group nodes and links to parent
	auto &parentChildren = const_cast<std::vector<std::shared_ptr<pragma::scenekit::NodeDesc>> &>(parent->GetChildNodes());
	itParent = parentChildren.erase(itParent);
	auto iParent = itParent - parentChildren.begin();
	parentChildren.reserve(parentChildren.size() + children.size());
	for(auto &child : children) {
		assert(!child->IsGroupNode());
		if(child->IsGroupNode())
			throw std::runtime_error {"Unresolved child group node"};
		parentChildren.push_back(child);
	}

	parentLinks.reserve(parentLinks.size() + links.size());
	for(auto &link : links) {
		if(link.fromSocket.GetNode() == this || link.toSocket.GetNode() == this)
			continue;
		parentLinks.push_back(link);
	}
	return parentChildren.begin() + iParent;
}
void pragma::scenekit::GroupNodeDesc::ResolveGroupNodes() { ResolveGroupNodes({}); }
pragma::scenekit::NodeDesc &pragma::scenekit::GroupNodeDesc::AddNode(const std::string &typeName)
{
	if(m_nodes.size() == m_nodes.capacity())
		m_nodes.reserve(m_nodes.size() * 1.5 + 10);
	auto node = m_nodeManager.CreateNode(typeName, this);
	if(node == nullptr)
		throw Exception {"Invalid node type '" + typeName + "'!"};
	m_nodes.push_back(node);
	return *node;
}
pragma::scenekit::NodeDesc &pragma::scenekit::GroupNodeDesc::AddNode(NodeTypeId id)
{
	if(m_nodes.size() == m_nodes.capacity())
		m_nodes.reserve(m_nodes.size() * 1.5 + 10);
	auto node = m_nodeManager.CreateNode(id, this);
	if(node == nullptr)
		throw Exception {"Invalid node type '" + std::to_string(id) + "'!"};
	m_nodes.push_back(node);
	return *node;
}
pragma::scenekit::Socket pragma::scenekit::GroupNodeDesc::AddMathNode(const Socket &socket0, const Socket &socket1, nodes::math::MathType mathOp)
{
	auto &node = AddNode(NODE_MATH);
	node.SetProperty(nodes::math::IN_TYPE, mathOp);
	Link(socket0, node.GetInputSocket(nodes::math::IN_VALUE1));
	Link(socket1, node.GetInputSocket(nodes::math::IN_VALUE2));
	return node;
}
pragma::scenekit::NodeDesc &pragma::scenekit::GroupNodeDesc::AddVectorMathNode(const Socket &socket0, const Socket &socket1, nodes::vector_math::MathType mathOp)
{
	auto &node = AddNode(NODE_VECTOR_MATH);
	node.SetProperty(nodes::vector_math::IN_TYPE, mathOp);
	Link(socket0, node.GetInputSocket(nodes::vector_math::IN_VECTOR1));
	if(socket1.IsValid())
		Link(socket1, node.GetInputSocket(nodes::vector_math::IN_VECTOR2));
	return node;
}
pragma::scenekit::Socket pragma::scenekit::GroupNodeDesc::AddNormalMapNode(const std::optional<std::string> &fileName, const std::optional<Socket> &fileNameSocket, float strength) { return AddNormalMapNodeDesc(fileName, fileNameSocket, strength); }
pragma::scenekit::NodeDesc &pragma::scenekit::GroupNodeDesc::AddNormalMapNodeDesc(const std::optional<std::string> &fileName, const std::optional<Socket> &fileNameSocket, float strength)
{
	auto &node = AddImageTextureNode(fileName, fileNameSocket, TextureType::NonColorImage);
	auto &nmap = AddNode(NODE_NORMAL_MAP);
	nmap.SetProperty(nodes::normal_map::IN_SPACE, nodes::normal_map::Space::Tangent);
	Link(*node.GetPrimaryOutputSocket(), nmap.GetInputSocket(nodes::normal_map::IN_COLOR));
	nmap.SetProperty(nodes::normal_map::IN_STRENGTH, strength);

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
pragma::scenekit::NodeDesc &pragma::scenekit::GroupNodeDesc::AddImageTextureNode(const std::optional<std::string> &fileName, const std::optional<Socket> &fileNameSocket, TextureType type)
{
	pragma::scenekit::NodeDesc *desc = nullptr;
	switch(type) {
	case TextureType::ColorImage:
		{
			auto &node = AddNode(NODE_IMAGE_TEXTURE);
			node.SetProperty(nodes::image_texture::IN_COLORSPACE, COLORSPACE_SRGB.c_str());
			// This is required, otherwise the alpha channel will affect the color channels even if alpha translucency is disabled (tested with cycles)
			node.SetProperty(nodes::image_texture::IN_ALPHA_TYPE, pragma::scenekit::nodes::image_texture::AlphaType::ChannelPacked);
			desc = &node;
			break;
		}
	case TextureType::NonColorImage:
		{
			auto &node = AddNode(NODE_IMAGE_TEXTURE);
			node.SetProperty(nodes::image_texture::IN_COLORSPACE, COLORSPACE_RAW.c_str());
			desc = &node;
			break;
		}
	case TextureType::EquirectangularImage:
		{
			auto &node = AddNode(NODE_ENVIRONMENT_TEXTURE);
			node.SetProperty(nodes::environment_texture::IN_COLORSPACE, COLORSPACE_RAW.c_str());
			node.SetProperty(nodes::environment_texture::IN_PROJECTION, pragma::scenekit::EnvironmentProjection::Equirectangular);
			desc = &node;
			break;
		}
	case TextureType::NormalMap:
		return AddNormalMapNodeDesc(fileName, fileNameSocket);
	}
	static_assert(umath::to_integral(TextureType::Count) == 4);
	assert(nodes::image_texture::IN_FILENAME == nodes::environment_texture::IN_FILENAME);
	if(fileName.has_value())
		desc->SetProperty(nodes::image_texture::IN_FILENAME, *fileName);
	else {
		assert(fileNameSocket.has_value());
		auto inFilename = desc->FindProperty(nodes::image_texture::IN_FILENAME);
		assert(inFilename.has_value());
		Link(*fileNameSocket, *inFilename);
	}
	return *desc;
}
pragma::scenekit::NodeDesc &pragma::scenekit::GroupNodeDesc::AddImageTextureNode(const std::string &fileName, TextureType type) { return AddImageTextureNode(fileName, {}, type); }
pragma::scenekit::NodeDesc &pragma::scenekit::GroupNodeDesc::AddImageTextureNode(const Socket &fileNameSocket, TextureType type) { return AddImageTextureNode({}, fileNameSocket, type); }
pragma::scenekit::Socket pragma::scenekit::GroupNodeDesc::AddConstantNode(const Vector3 &v)
{
	auto &node = AddNode(NODE_VECTOR_MATH);
	node.SetProperty(nodes::vector_math::IN_VECTOR1, v);
	node.SetProperty(nodes::vector_math::IN_VECTOR2, Vector3 {});
	node.SetProperty(nodes::vector_math::IN_TYPE, nodes::vector_math::MathType::Add);
	return node;
}
pragma::scenekit::Socket pragma::scenekit::GroupNodeDesc::Mix(const Socket &socket0, const Socket &socket1, const Socket &fac)
{
	auto type0 = socket0.GetType();
	auto type1 = socket1.GetType();
	if(type0 != SocketType::Closure && type1 != SocketType::Closure)
		return Mix(socket0, socket1, fac, nodes::mix::Mix::Blend);
	auto &node = AddNode(NODE_MIX_CLOSURE);
	Link(socket0, node.GetInputSocket(nodes::mix_closure::IN_CLOSURE1));
	Link(socket1, node.GetInputSocket(nodes::mix_closure::IN_CLOSURE2));
	Link(fac, node.GetInputSocket(nodes::mix_closure::IN_FAC));
	return node;
}
pragma::scenekit::Socket pragma::scenekit::GroupNodeDesc::Mix(const Socket &socket0, const Socket &socket1, const Socket &fac, nodes::mix::Mix type)
{
	auto &node = AddNode(NODE_MIX);
	Link(socket0, node.GetInputSocket(nodes::mix::IN_COLOR1));
	Link(socket1, node.GetInputSocket(nodes::mix::IN_COLOR2));
	Link(fac, node.GetInputSocket(nodes::mix::IN_FAC));
	node.SetProperty(nodes::mix::IN_TYPE, type);
	return node;
}
pragma::scenekit::Socket pragma::scenekit::GroupNodeDesc::Invert(const Socket &socket, const std::optional<Socket> &fac)
{
	auto &node = AddNode(NODE_INVERT);
	Link(socket, node.GetInputSocket(nodes::invert::IN_COLOR));
	if(fac.has_value())
		Link(*fac, node.GetInputSocket(nodes::invert::IN_FAC));
	return node;
}
pragma::scenekit::Socket pragma::scenekit::GroupNodeDesc::ToGrayScale(const Socket &socket)
{
	auto &node = AddNode(NODE_RGB_TO_BW);
	Link(socket, node.GetInputSocket(nodes::rgb_to_bw::IN_COLOR));
	return CombineRGB(node, node, node);
}
pragma::scenekit::Socket pragma::scenekit::GroupNodeDesc::AddConstantNode(float f)
{
	auto &node = AddNode(NODE_MATH);
	node.SetProperty(nodes::math::IN_VALUE1, f);
	node.SetProperty(nodes::math::IN_VALUE2, 0.f);
	node.SetProperty(nodes::math::IN_TYPE, nodes::math::MathType::Add);
	return node;
}
pragma::scenekit::Socket pragma::scenekit::GroupNodeDesc::CombineRGB(const Socket &r, const Socket &g, const Socket &b)
{
	auto &node = AddNode(NODE_COMBINE_RGB);
	Link(r, node.GetInputSocket(nodes::combine_rgb::IN_R));
	Link(g, node.GetInputSocket(nodes::combine_rgb::IN_G));
	Link(b, node.GetInputSocket(nodes::combine_rgb::IN_B));
	return node;
}
pragma::scenekit::NodeDesc &pragma::scenekit::GroupNodeDesc::SeparateRGB(const Socket &rgb)
{
	auto &node = AddNode(NODE_SEPARATE_RGB);
	Link(rgb, node.GetInputSocket(nodes::separate_rgb::IN_COLOR));
	return node;
}
void pragma::scenekit::GroupNodeDesc::Serialize(DataStream &dsOut)
{
	// Root node; Build index list
	std::unordered_map<const NodeDesc *, uint64_t> rootNodeIndexTable;
	uint64_t idx = 0u;

	std::function<void(const NodeDesc &)> fBuildIndexTable = nullptr;
	fBuildIndexTable = [&fBuildIndexTable, &rootNodeIndexTable, &idx](const NodeDesc &node) {
		rootNodeIndexTable[&node] = idx++;
		if(node.IsGroupNode() == false)
			return;
		auto &nodeGroup = static_cast<const GroupNodeDesc &>(node);
		for(auto &child : nodeGroup.GetChildNodes())
			fBuildIndexTable(*child);
	};
	fBuildIndexTable(*this);
	SerializeNodes(dsOut);
	SerializeLinks(dsOut, rootNodeIndexTable);
}
void pragma::scenekit::GroupNodeDesc::SerializeNodes(DataStream &dsOut) const
{
	NodeDesc::SerializeNodes(dsOut);
	dsOut->Write<uint32_t>(m_nodes.size());
	for(auto &node : m_nodes) {
		dsOut->Write<bool>(node->IsGroupNode());
		node->SerializeNodes(dsOut);
	}
}
void pragma::scenekit::GroupNodeDesc::SerializeLinks(DataStream &dsOut, const std::unordered_map<const NodeDesc *, uint64_t> &nodeIndexTable)
{
	auto fWriteNodeLinks = [&dsOut, &nodeIndexTable](const GroupNodeDesc &node) {
		dsOut->Write<uint32_t>(node.m_links.size());
		for(auto &link : node.m_links)
			link.Serialize(dsOut, nodeIndexTable);
	};

	std::function<void(const GroupNodeDesc &)> fWriteLinks = nullptr;
	fWriteLinks = [&fWriteLinks, &fWriteNodeLinks](const GroupNodeDesc &node) {
		if(node.IsGroupNode() == false)
			return;
		fWriteNodeLinks(node);
		for(auto &child : node.m_nodes) {
			if(child->IsGroupNode() == false)
				continue;
			fWriteLinks(static_cast<GroupNodeDesc &>(*child));
		}
	};
	fWriteLinks(*this);
}
void pragma::scenekit::GroupNodeDesc::Deserialize(DataStream &dsIn)
{
	DeserializeNodes(dsIn);

	std::vector<const NodeDesc *> rootNodeIndexTable;
	// Root node; Build index list
	std::function<void(const NodeDesc &)> fBuildIndexTable = nullptr;
	fBuildIndexTable = [&fBuildIndexTable, &rootNodeIndexTable](const NodeDesc &node) {
		if(rootNodeIndexTable.size() == rootNodeIndexTable.capacity())
			rootNodeIndexTable.reserve(rootNodeIndexTable.size() * 1.5 + 100);
		rootNodeIndexTable.push_back(&node);
		if(node.IsGroupNode() == false)
			return;
		auto &nodeGroup = static_cast<const GroupNodeDesc &>(node);
		for(auto &child : nodeGroup.GetChildNodes())
			fBuildIndexTable(*child);
	};
	fBuildIndexTable(*this);

	DeserializeLinks(dsIn, rootNodeIndexTable);
}
void pragma::scenekit::GroupNodeDesc::DeserializeNodes(DataStream &dsIn)
{
	NodeDesc::DeserializeNodes(dsIn);
	auto numNodes = dsIn->Read<uint32_t>();
	m_nodes.reserve(numNodes);
	for(auto i = decltype(numNodes) {0u}; i < numNodes; ++i) {
		auto isGroupNode = dsIn->Read<bool>();
		auto node = isGroupNode ? GroupNodeDesc::Create(m_nodeManager, this) : NodeDesc::Create(this);
		node->DeserializeNodes(dsIn);
		m_nodes.push_back(node);
	}
}
void pragma::scenekit::GroupNodeDesc::DeserializeLinks(DataStream &dsIn, const std::vector<const NodeDesc *> &nodeIndexTable)
{
	auto fReadNodeLinks = [&dsIn, &nodeIndexTable](GroupNodeDesc &node) {
		auto numLinks = dsIn->Read<uint32_t>();
		node.m_links.resize(numLinks);
		for(auto &link : node.m_links)
			link.Deserialize(node, dsIn, nodeIndexTable);
	};

	std::function<void(GroupNodeDesc &)> fReadLinks = nullptr;
	fReadLinks = [&fReadLinks, &fReadNodeLinks](GroupNodeDesc &node) {
		if(node.IsGroupNode() == false)
			return;
		fReadNodeLinks(node);
		for(auto &child : node.m_nodes) {
			if(child->IsGroupNode() == false)
				continue;
			fReadLinks(static_cast<GroupNodeDesc &>(*child));
		}
	};
	fReadLinks(*this);
}
void pragma::scenekit::GroupNodeDesc::Link(NodeDesc &fromNode, const std::string &fromSocket, NodeDesc &toNode, const std::string &toSocket) { Link(fromNode.GetOutputSocket(fromSocket), toNode.GetInputSocket(toSocket)); }
void pragma::scenekit::GroupNodeDesc::Link(const Socket &fromSocket, const Socket &toSocket)
{
	if(toSocket.IsConcreteValue()) {
		throw Exception {"To-Socket " + toSocket.ToString() + " is a concrete type, which cannot be linked to!"};
		return; // Can't link to a concrete type
	}
	std::string toSocketName;
	auto *pToNode = toSocket.GetNode(toSocketName);
	if(pToNode == nullptr) {
		throw Exception {"To-Socket " + toSocket.ToString() + " references non-existing node!"};
		return;
	}
	NodeSocketDesc *toDesc = nullptr;
	if(toSocket.IsOutputSocket()) {
		if(pToNode->IsGroupNode() == false) {
			throw Exception {"To-Socket is an output socket, which is only allowed for group nodes!"};
			return;
		}
		toDesc = pToNode->FindOutputSocketDesc(toSocketName);
	}
	else
		toDesc = pToNode->FindInputSocketDesc(toSocketName);
	if(toDesc == nullptr)
		toDesc = pToNode->FindPropertyDesc(toSocketName);
	if(toDesc == nullptr) {
		throw Exception {"To-Socket " + toSocket.ToString() + " references invalid socket '" + toSocketName + "' of node " + pToNode->ToString() + "!"};
		return;
	}
	if(fromSocket.IsConcreteValue()) {
		auto fromValue = fromSocket.GetValue();
		if(fromValue.has_value()) {
			auto toValue = convert(fromValue->value.get(), fromValue->type, toDesc->dataValue.type);
			if(toValue.has_value() == false) {
				throw Exception {"From-Socket " + fromSocket.ToString() + " is concrete type, but value type is not compatible with to-Socket " + toSocket.ToString() + "!"};
				return;
			}
			else
				toDesc->dataValue = *toValue;
		}
		return;
	}
	std::string fromSocketName;
	auto *pFromNode = fromSocket.GetNode(fromSocketName);
	if(pFromNode == nullptr) {
		throw Exception {"From-Socket " + fromSocket.ToString() + " references non-existing node!"};
		return;
	}
	auto *fromDesc = pFromNode->FindOutputSocketDesc(fromSocketName);
	if(fromDesc == nullptr) {
		if(pFromNode->IsGroupNode() == false) {
			throw Exception {"From-Socket is an input socket, which is only allowed for group nodes!"};
			return;
		}
		fromDesc = pFromNode->FindInputSocketDesc(fromSocketName);
		if(fromDesc == nullptr)
			fromDesc = pFromNode->FindPropertyDesc(fromSocketName);
	}

	// If there is already a link to the to-socket, break it up
	auto itLink = std::find_if(m_links.begin(), m_links.end(), [&toSocket](const NodeDescLink &link) { return link.toSocket == toSocket; });
	if(itLink != m_links.end())
		m_links.erase(itLink);
	if(m_links.size() == m_links.capacity())
		m_links.reserve(m_links.size() * 1.5 + 20);
	m_links.push_back({});
	auto &link = m_links.back();
	link.fromSocket = fromSocket;
	link.toSocket = toSocket;
	return;
}

//////////////////////

void pragma::scenekit::Shader::SetActivePass(Pass pass) { m_activePass = pass; }
std::shared_ptr<pragma::scenekit::GroupNodeDesc> pragma::scenekit::Shader::GetActivePassNode() const
{
	switch(m_activePass) {
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

void pragma::scenekit::Shader::Serialize(DataStream &dsOut) const
{
	std::array<std::shared_ptr<pragma::scenekit::GroupNodeDesc>, 4> passes = {combinedPass, albedoPass, normalPass, depthPass};
	uint32_t flags = 0;
	for(auto i = decltype(passes.size()) {0u}; i < passes.size(); ++i) {
		if(passes.at(i))
			flags |= 1 << i;
	}

	dsOut->Write<bool>(m_hairConfig.has_value());
	if(m_hairConfig.has_value())
		dsOut->Write(*m_hairConfig);

	dsOut->Write<bool>(m_subdivisionSettings.has_value());
	if(m_subdivisionSettings.has_value())
		dsOut->Write(*m_subdivisionSettings);

	dsOut->Write<uint32_t>(flags);
	for(auto &pass : passes) {
		if(pass == nullptr)
			continue;
		pass->Serialize(dsOut);
	}
}
void pragma::scenekit::Shader::Deserialize(DataStream &dsIn, NodeManager &nodeManager)
{
	std::array<std::reference_wrapper<std::shared_ptr<pragma::scenekit::GroupNodeDesc>>, 4> passes = {combinedPass, albedoPass, normalPass, depthPass};

	auto hasHairConfig = dsIn->Read<bool>();
	if(hasHairConfig)
		m_hairConfig = dsIn->Read<util::HairConfig>();
	else
		m_hairConfig = {};

	auto hasSubdivSettings = dsIn->Read<bool>();
	if(hasSubdivSettings)
		m_subdivisionSettings = dsIn->Read<SubdivisionSettings>();
	else
		m_subdivisionSettings = {};

	auto flags = dsIn->Read<uint32_t>();
	for(auto i = decltype(passes.size()) {0u}; i < passes.size(); ++i) {
		if((flags & (1 << i)) == 0)
			continue;
		passes.at(i).get() = GroupNodeDesc::Create(nodeManager);
		passes.at(i).get()->Deserialize(dsIn);
	}
}
pragma::scenekit::Shader::Shader() {}
void pragma::scenekit::Shader::Initialize() {}
void pragma::scenekit::Shader::Finalize() {}

//////////////////////

std::shared_ptr<pragma::scenekit::NodeManager> pragma::scenekit::NodeManager::Create()
{
	auto nm = std::shared_ptr<NodeManager> {new NodeManager {}};
	nm->RegisterNodeTypes();
	return nm;
}
pragma::scenekit::NodeTypeId pragma::scenekit::NodeManager::RegisterNodeType(const std::string &typeName, const std::function<std::shared_ptr<NodeDesc>(GroupNodeDesc *)> &factory)
{
	auto lTypeName = typeName;
	ustring::to_lower(lTypeName);
	auto it = std::find_if(m_nodeTypes.begin(), m_nodeTypes.end(), [&lTypeName](const NodeType &nt) { return nt.typeName == lTypeName; });
	if(it == m_nodeTypes.end()) {
		if(m_nodeTypes.size() == m_nodeTypes.capacity())
			m_nodeTypes.reserve(m_nodeTypes.size() * 1.5 + 50);
		m_nodeTypes.push_back({});
		it = m_nodeTypes.end() - 1;
	}
	auto &desc = *it;
	desc.typeName = lTypeName;
	desc.factory = factory;
	return it - m_nodeTypes.begin();
}

std::optional<pragma::scenekit::NodeTypeId> pragma::scenekit::NodeManager::FindNodeTypeId(const std::string &typeName) const
{
	auto lTypeName = typeName;
	ustring::to_lower(lTypeName);
	auto it = std::find_if(m_nodeTypes.begin(), m_nodeTypes.end(), [&lTypeName](const NodeType &nodeType) { return nodeType.typeName == lTypeName; });
	if(it == m_nodeTypes.end())
		return {};
	return it - m_nodeTypes.begin();
}

std::shared_ptr<pragma::scenekit::NodeDesc> pragma::scenekit::NodeManager::CreateNode(const std::string &typeName, GroupNodeDesc *parent) const
{
	auto typeId = FindNodeTypeId(typeName);
	if(typeId.has_value() == false)
		return nullptr;
	return CreateNode(*typeId, parent);
}
std::shared_ptr<pragma::scenekit::NodeDesc> pragma::scenekit::NodeManager::CreateNode(NodeTypeId id, GroupNodeDesc *parent) const
{
	if(id >= m_nodeTypes.size())
		return nullptr;
	auto node = m_nodeTypes.at(id).factory(parent);
	if(node == nullptr)
		return nullptr;
	node->SetTypeName(m_nodeTypes.at(id).typeName);
	return node;
}

void pragma::scenekit::NodeManager::RegisterNodeTypes()
{
	RegisterNodeType(NODE_MATH, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::math::IN_TYPE, nodes::math::MathType::Add);
		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::math::IN_USE_CLAMP, false);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::math::IN_VALUE1, 0.5f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::math::IN_VALUE2, 0.5f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::math::IN_VALUE3, 0.f, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::math::OUT_VALUE, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::math::OUT_VALUE);
		return desc;
	});
	RegisterNodeType(NODE_HSV, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::hsv::IN_HUE, 0.5f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::hsv::IN_SATURATION, 1.0f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::hsv::IN_VALUE, 1.0f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::hsv::IN_FAC, 1.0f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::hsv::IN_COLOR, STColor {}, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::hsv::OUT_COLOR, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::hsv::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_SEPARATE_XYZ, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::separate_xyz::IN_VECTOR, STColor {}, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::separate_xyz::OUT_X, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::separate_xyz::OUT_Y, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::separate_xyz::OUT_Z, SocketIO::Out);
		return desc;
	});
	RegisterNodeType(NODE_COMBINE_XYZ, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::combine_xyz::IN_X, 0.0f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::combine_xyz::IN_Y, 0.0f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::combine_xyz::IN_Z, 0.0f, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::combine_xyz::OUT_VECTOR, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::combine_xyz::OUT_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_SEPARATE_RGB, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::separate_rgb::IN_COLOR, STColor {}, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::separate_rgb::OUT_R, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::separate_rgb::OUT_G, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::separate_rgb::OUT_B, SocketIO::Out);
		return desc;
	});
	RegisterNodeType(NODE_COMBINE_RGB, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::combine_rgb::IN_R, 0.f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::combine_rgb::IN_G, 0.f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::combine_rgb::IN_B, 0.f, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::combine_rgb::OUT_IMAGE, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::combine_rgb::OUT_IMAGE);
		return desc;
	});
	RegisterNodeType(NODE_GEOMETRY, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);

		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::geometry::OUT_POSITION, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::geometry::OUT_NORMAL, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::geometry::OUT_TANGENT, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::geometry::OUT_TRUE_NORMAL, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::geometry::OUT_INCOMING, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::geometry::OUT_PARAMETRIC, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::geometry::OUT_BACKFACING, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::geometry::OUT_POINTINESS, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::geometry::OUT_RANDOM_PER_ISLAND, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::geometry::OUT_POSITION);
		return desc;
	});
	RegisterNodeType(NODE_CAMERA_INFO, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::camera_info::OUT_VIEW_VECTOR, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::camera_info::OUT_VIEW_Z_DEPTH, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::camera_info::OUT_VIEW_DISTANCE, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::camera_info::OUT_VIEW_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_IMAGE_TEXTURE, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::String>(nodes::image_texture::IN_FILENAME, STString {});
		desc->RegisterSocket<pragma::scenekit::SocketType::String>(nodes::image_texture::IN_COLORSPACE, STString {COLORSPACE_AUTO});
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::image_texture::IN_ALPHA_TYPE, nodes::image_texture::AlphaType::Auto);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::image_texture::IN_INTERPOLATION, nodes::image_texture::InterpolationType::Linear);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::image_texture::IN_EXTENSION, nodes::image_texture::ExtensionType::Repeat);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::image_texture::IN_PROJECTION, nodes::image_texture::Projection::Flat);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::image_texture::IN_PROJECTION_BLEND, 0.0f);

		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::image_texture::IN_VECTOR, STPoint {}, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::image_texture::OUT_COLOR, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::image_texture::OUT_ALPHA, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::image_texture::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_NORMAL_TEXTURE, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::String>(nodes::normal_texture::IN_FILENAME, STString {});
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::normal_texture::IN_STRENGTH, 1.f);

		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::normal_texture::OUT_NORMAL, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::normal_texture::OUT_NORMAL);
		return desc;
	});
	RegisterNodeType(NODE_ENVIRONMENT_TEXTURE, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::String>(nodes::environment_texture::IN_FILENAME, STString {});
		desc->RegisterSocket<pragma::scenekit::SocketType::String>(nodes::environment_texture::IN_COLORSPACE, STString {COLORSPACE_AUTO});
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::environment_texture::IN_ALPHA_TYPE, nodes::image_texture::AlphaType::Auto);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::environment_texture::IN_INTERPOLATION, nodes::image_texture::InterpolationType::Linear);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::environment_texture::IN_PROJECTION, pragma::scenekit::EnvironmentProjection::Equirectangular);

		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::environment_texture::IN_VECTOR, STVector {}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::environment_texture::OUT_COLOR, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::environment_texture::OUT_ALPHA, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::environment_texture::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_MIX_CLOSURE, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::mix_closure::IN_FAC, 0.5f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::mix_closure::IN_CLOSURE1, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::mix_closure::IN_CLOSURE2, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::mix_closure::OUT_CLOSURE, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::mix_closure::OUT_CLOSURE);
		return desc;
	});
	RegisterNodeType(NODE_ADD_CLOSURE, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::add_closure::IN_CLOSURE1, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::add_closure::IN_CLOSURE2, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::add_closure::OUT_CLOSURE, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::add_closure::OUT_CLOSURE);
		return desc;
	});
	RegisterNodeType(NODE_BACKGROUND_SHADER, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::background_shader::IN_COLOR, STColor {0.8f, 0.8f, 0.8f}, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::background_shader::IN_STRENGTH, 1.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::background_shader::IN_SURFACE_MIX_WEIGHT, 0.0f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::background_shader::OUT_BACKGROUND, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::background_shader::OUT_BACKGROUND);
		return desc;
	});
	RegisterNodeType(NODE_TEXTURE_COORDINATE, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::texture_coordinate::IN_FROM_DUPLI, false);
		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::texture_coordinate::IN_USE_TRANSFORM, false);
		desc->RegisterSocket<pragma::scenekit::SocketType::Transform>(nodes::texture_coordinate::IN_OB_TFM, STTransform {1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f, 0});

		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::texture_coordinate::OUT_GENERATED, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::texture_coordinate::OUT_NORMAL, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::texture_coordinate::OUT_UV, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::texture_coordinate::OUT_OBJECT, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::texture_coordinate::OUT_CAMERA, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::texture_coordinate::OUT_WINDOW, SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::texture_coordinate::OUT_REFLECTION, SocketIO::Out);
		return desc;
	});
	RegisterNodeType(NODE_UVMAP, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::texture_coordinate::OUT_UV, SocketIO::Out);
		return desc;
	});
	RegisterNodeType(NODE_MAPPING, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::mapping::IN_TYPE, nodes::mapping::Type::Point);

		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::mapping::IN_VECTOR, STPoint {}, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::mapping::IN_LOCATION, STPoint {}, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Point, Vector3>(nodes::mapping::IN_ROTATION, STPoint {}, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Point>(nodes::mapping::IN_SCALE, STPoint {1.f, 1.f, 1.f}, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::mapping::OUT_VECTOR, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::mapping::OUT_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_SCATTER_VOLUME, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::scatter_volume::IN_COLOR, STColor {0.8f, 0.8f, 0.8f}, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::scatter_volume::IN_DENSITY, 1.0f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::scatter_volume::IN_ANISOTROPY, 0.0f, SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::scatter_volume::IN_VOLUME_MIX_WEIGHT, 0.0f, SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::scatter_volume::OUT_VOLUME, SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::scatter_volume::OUT_VOLUME);
		return desc;
	});
	RegisterNodeType(NODE_EMISSION, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::emission::IN_COLOR, STColor {0.8f, 0.8f, 0.8f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::emission::IN_STRENGTH, 1.0f, pragma::scenekit::SocketIO::In); // Default in Cycles is 10, which is a little excessive for our purposes
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::emission::IN_SURFACE_MIX_WEIGHT, 0.0f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::emission::OUT_EMISSION, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::emission::OUT_EMISSION);
		return desc;
	});
	RegisterNodeType(NODE_COLOR, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::color::IN_VALUE, STColor {});

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::color::OUT_COLOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::color::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_ATTRIBUTE, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::String>(nodes::attribute::IN_ATTRIBUTE, STString {});

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::attribute::OUT_COLOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::attribute::OUT_VECTOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::attribute::OUT_FAC, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::attribute::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_LIGHT_PATH, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_IS_CAMERA_RAY, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_IS_SHADOW_RAY, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_IS_DIFFUSE_RAY, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_IS_GLOSSY_RAY, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_IS_SINGULAR_RAY, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_IS_REFLECTION_RAY, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_IS_TRANSMISSION_RAY, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_IS_VOLUME_SCATTER_RAY, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_RAY_LENGTH, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_RAY_DEPTH, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_DIFFUSE_DEPTH, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_GLOSSY_DEPTH, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_TRANSPARENT_DEPTH, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::light_path::OUT_TRANSMISSION_DEPTH, pragma::scenekit::SocketIO::Out);
		return desc;
	});
	RegisterNodeType(NODE_TRANSPARENT_BSDF, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::transparent_bsdf::IN_COLOR, STColor {1.f, 1.f, 1.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::transparent_bsdf::IN_SURFACE_MIX_WEIGHT, 0.f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::transparent_bsdf::OUT_BSDF, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::transparent_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_TRANSLUCENT_BSDF, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::translucent_bsdf::IN_COLOR, STColor {0.8f, 0.8f, 0.8f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::translucent_bsdf::IN_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::translucent_bsdf::IN_SURFACE_MIX_WEIGHT, 0.f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::translucent_bsdf::OUT_BSDF, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::translucent_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_DIFFUSE_BSDF, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::diffuse_bsdf::IN_COLOR, STColor {0.8f, 0.8f, 0.8f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::diffuse_bsdf::IN_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::diffuse_bsdf::IN_SURFACE_MIX_WEIGHT, 0.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::diffuse_bsdf::IN_ROUGHNESS, 0.f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::diffuse_bsdf::OUT_BSDF, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::diffuse_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_NORMAL_MAP, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::normal_map::IN_SPACE, nodes::normal_map::Space::Tangent);
		desc->RegisterSocket<pragma::scenekit::SocketType::String>(nodes::normal_map::IN_ATTRIBUTE, STString {});

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::normal_map::IN_STRENGTH, 1.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::normal_map::IN_COLOR, STColor {0.5f, 0.5f, 1.0f}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::normal_map::OUT_NORMAL, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::normal_map::OUT_NORMAL);
		return desc;
	});
	RegisterNodeType(NODE_PRINCIPLED_BSDF, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);

		// See kernel/svm/svm_types.h in Cycles source code
		constexpr uint32_t CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID = 30;
		constexpr uint32_t CLOSURE_BSSRDF_PRINCIPLED_ID = 42;

		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::principled_bsdf::IN_DISTRIBUTION, CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::principled_bsdf::IN_SUBSURFACE_METHOD, CLOSURE_BSSRDF_PRINCIPLED_ID);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::principled_bsdf::IN_BASE_COLOR, STColor {0.8f, 0.8f, 0.8f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::principled_bsdf::IN_SUBSURFACE_COLOR, STColor {0.8f, 0.8f, 0.8f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_METALLIC, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_SUBSURFACE, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::principled_bsdf::IN_SUBSURFACE_RADIUS, STVector {0.1f, 0.1f, 0.1f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_SPECULAR, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_ROUGHNESS, 0.5f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_SPECULAR_TINT, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_ANISOTROPIC, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_SHEEN, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_SHEEN_TINT, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_CLEARCOAT, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_CLEARCOAT_ROUGHNESS, 0.03f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_IOR, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_TRANSMISSION, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_TRANSMISSION_ROUGHNESS, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_ANISOTROPIC_ROTATION, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::principled_bsdf::IN_EMISSION, STColor {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_ALPHA, 1.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::principled_bsdf::IN_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::principled_bsdf::IN_CLEARCOAT_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::principled_bsdf::IN_TANGENT, STNormal {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_bsdf::IN_SURFACE_MIX_WEIGHT, 0.0f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::principled_bsdf::OUT_BSDF, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::principled_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_PRINCIPLED_VOLUME, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::principled_volume::IN_COLOR, STColor {0.5f, 0.5f, 0.5f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_volume::IN_DENSITY, 1.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_volume::IN_ANISOTROPY, 0.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::principled_volume::IN_ABSORPTION_COLOR, STColor {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_volume::IN_EMISSION_STRENGTH, 0.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::principled_volume::IN_EMISSION_COLOR, STColor {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_volume::IN_BLACKBODY_INTENSITY, 0.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::principled_volume::IN_BLACKBODY_TINT, STColor {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_volume::IN_TEMPERATURE, 1000.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::principled_volume::IN_VOLUME_MIX_WEIGHT, 0.f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::principled_volume::OUT_VOLUME, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::principled_volume::OUT_VOLUME);
		return desc;
	});
	RegisterNodeType(NODE_TOON_BSDF, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);

		// See kernel/svm/svm_types.h in Cycles source code
		constexpr uint32_t CLOSURE_BSDF_DIFFUSE_TOON_ID = 7;

		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::toon_bsdf::IN_COMPONENT, CLOSURE_BSDF_DIFFUSE_TOON_ID);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::toon_bsdf::IN_COLOR, STColor {0.8f, 0.8f, 0.8f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::toon_bsdf::IN_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::toon_bsdf::IN_SURFACE_MIX_WEIGHT, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::toon_bsdf::IN_SIZE, 0.5f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::toon_bsdf::IN_SMOOTH, 0.0f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::toon_bsdf::OUT_BSDF, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::toon_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_GLOSSY_BSDF, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);

		// See kernel/svm/svm_types.h in Cycles source code
		constexpr uint32_t CLOSURE_BSDF_MICROFACET_GGX_ID = 9;

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::glossy_bsdf::IN_COLOR, STColor {0.8f, 0.8f, 0.8f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::glossy_bsdf::IN_ALPHA, 1.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::glossy_bsdf::IN_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::glossy_bsdf::IN_SURFACE_MIX_WEIGHT, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::glass_bsdf::IN_DISTRIBUTION, CLOSURE_BSDF_MICROFACET_GGX_ID);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::glass_bsdf::IN_ROUGHNESS, 0.5f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::glass_bsdf::OUT_BSDF, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::glass_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_GLASS_BSDF, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);

		// See kernel/svm/svm_types.h in Cycles source code
		constexpr uint32_t CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID = 32;

		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::glass_bsdf::IN_DISTRIBUTION, CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::glass_bsdf::IN_COLOR, STColor {0.8f, 0.8f, 0.8f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::glass_bsdf::IN_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::glass_bsdf::IN_SURFACE_MIX_WEIGHT, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::glass_bsdf::IN_ROUGHNESS, 0.0f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::glass_bsdf::IN_IOR, 0.3f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::glass_bsdf::OUT_BSDF, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::glass_bsdf::OUT_BSDF);
		return desc;
	});
	RegisterNodeType(NODE_VOLUME_CLEAR, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);

		desc->RegisterSocket<pragma::scenekit::SocketType::Int>(nodes::volume_clear::IN_PRIORITY, 0, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_clear::IN_IOR, STVector {0.3f, 0.3f, 0.3f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_clear::IN_ABSORPTION, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_clear::IN_EMISSION, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::volume_clear::IN_DEFAULT_WORLD_VOLUME, false);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::volume_clear::OUT_VOLUME, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::volume_clear::OUT_VOLUME);
		return desc;
	});
	RegisterNodeType(NODE_VOLUME_HOMOGENEOUS, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);

		desc->RegisterSocket<pragma::scenekit::SocketType::Int>(nodes::volume_homogeneous::IN_PRIORITY, 0, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_homogeneous::IN_IOR, STVector {0.3f, 0.3f, 0.3f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_homogeneous::IN_ABSORPTION, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_homogeneous::IN_EMISSION, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_homogeneous::IN_SCATTERING, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_homogeneous::IN_ASYMMETRY, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::volume_homogeneous::IN_MULTI_SCATTERING, false, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::volume_homogeneous::IN_ABSORPTION_DEPTH, 0.01f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::volume_homogeneous::IN_DEFAULT_WORLD_VOLUME, false);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::volume_homogeneous::OUT_VOLUME, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::volume_homogeneous::OUT_VOLUME);
		return desc;
	});
	RegisterNodeType(NODE_VOLUME_HETEROGENEOUS, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);

		desc->RegisterSocket<pragma::scenekit::SocketType::Int>(nodes::volume_heterogeneous::IN_PRIORITY, 0, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_heterogeneous::IN_IOR, STVector {0.3f, 0.3f, 0.3f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_heterogeneous::IN_ABSORPTION, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_heterogeneous::IN_EMISSION, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_heterogeneous::IN_SCATTERING, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::volume_heterogeneous::IN_ASYMMETRY, STVector {0.f, 0.f, 0.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::volume_heterogeneous::IN_MULTI_SCATTERING, false, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::volume_heterogeneous::IN_STEP_SIZE, 0.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Int>(nodes::volume_heterogeneous::IN_STEP_MAX_COUNT, 0, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::volume_heterogeneous::IN_DEFAULT_WORLD_VOLUME, false);

		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::volume_heterogeneous::OUT_VOLUME, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::volume_heterogeneous::OUT_VOLUME);
		return desc;
	});
	RegisterNodeType(NODE_OUTPUT, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::output::IN_SURFACE, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Closure>(nodes::output::IN_VOLUME, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::output::IN_DISPLACEMENT, STVector {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::output::IN_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);
		return desc;
	});
	RegisterNodeType(NODE_VECTOR_MATH, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::vector_math::IN_TYPE, nodes::vector_math::MathType::Add);

		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::vector_math::IN_VECTOR1, STVector {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::vector_math::IN_VECTOR2, STVector {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::vector_math::IN_SCALE, 1.f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::vector_math::OUT_VALUE, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::vector_math::OUT_VECTOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::vector_math::OUT_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_MIX, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::mix::IN_TYPE, nodes::mix::Mix::Blend);
		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::mix::IN_USE_CLAMP, false);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::mix::IN_FAC, 0.5f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::mix::IN_COLOR1, STColor {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::mix::IN_COLOR2, STColor {}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::mix::OUT_COLOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::mix::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_NOISE_TEXTURE, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::noise_texture::IN_VECTOR, STVector {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::noise_texture::IN_W, 0.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::noise_texture::IN_SCALE, 1.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::noise_texture::IN_DETAIL, 2.f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::noise_texture::IN_ROUGHNESS, 0.5f, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::noise_texture::IN_DISTORTION, 0.f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::noise_texture::OUT_FAC, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::noise_texture::OUT_COLOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::noise_texture::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_RGB_TO_BW, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::rgb_to_bw::IN_COLOR, STColor {}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::rgb_to_bw::OUT_VAL, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::rgb_to_bw::OUT_VAL);
		return desc;
	});
	RegisterNodeType(NODE_INVERT, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::invert::IN_COLOR, STColor {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::invert::IN_FAC, STFloat {1.f}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::invert::OUT_COLOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::invert::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_VECTOR_TRANSFORM, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::vector_transform::IN_TYPE, nodes::vector_transform::Type::Vector);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::vector_transform::IN_CONVERT_FROM, pragma::scenekit::nodes::vector_transform::ConvertSpace::World);
		desc->RegisterSocket<pragma::scenekit::SocketType::Enum>(nodes::vector_transform::IN_CONVERT_TO, pragma::scenekit::nodes::vector_transform::ConvertSpace::Object);
		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::vector_transform::IN_VECTOR, STVector {}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Vector>(nodes::vector_transform::OUT_VECTOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::vector_transform::OUT_VECTOR);
		return desc;
	});
	RegisterNodeType(NODE_RGB_RAMP, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::ColorArray>(nodes::rgb_ramp::IN_RAMP, STColorArray {});
		desc->RegisterSocket<pragma::scenekit::SocketType::FloatArray>(nodes::rgb_ramp::IN_RAMP_ALPHA, STFloatArray {});
		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::rgb_ramp::IN_INTERPOLATE, true);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::rgb_ramp::IN_FAC, 0.f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::rgb_ramp::OUT_COLOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::rgb_ramp::OUT_ALPHA, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::rgb_ramp::OUT_COLOR);
		return desc;
	});
	RegisterNodeType(NODE_LAYER_WEIGHT, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::layer_weight::IN_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::layer_weight::IN_BLEND, 0.5f, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::layer_weight::OUT_FRESNEL, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::layer_weight::OUT_FACING, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::layer_weight::OUT_FRESNEL);
		return desc;
	});
	RegisterNodeType(NODE_AMBIENT_OCCLUSION, [](GroupNodeDesc *parent) {
		auto desc = NodeDesc::Create(parent);
		desc->RegisterSocket<pragma::scenekit::SocketType::Int>(nodes::ambient_occlusion::IN_SAMPLES, STInt {16});
		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::ambient_occlusion::IN_COLOR, STColor {}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::ambient_occlusion::IN_DISTANCE, STFloat {1.f}, pragma::scenekit::SocketIO::In);
		desc->RegisterSocket<pragma::scenekit::SocketType::Normal>(nodes::ambient_occlusion::IN_NORMAL, STNormal {}, pragma::scenekit::SocketIO::In);

		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::ambient_occlusion::IN_INSIDE, STBool {false});
		desc->RegisterSocket<pragma::scenekit::SocketType::Bool>(nodes::ambient_occlusion::IN_ONLY_LOCAL, STBool {false});

		desc->RegisterSocket<pragma::scenekit::SocketType::Color>(nodes::ambient_occlusion::OUT_COLOR, pragma::scenekit::SocketIO::Out);
		desc->RegisterSocket<pragma::scenekit::SocketType::Float>(nodes::ambient_occlusion::OUT_AO, pragma::scenekit::SocketIO::Out);
		desc->RegisterPrimaryOutputSocket(nodes::layer_weight::OUT_FRESNEL);
		return desc;
	});
	static_assert(NODE_COUNT == 44, "Increase this number if new node types are added!");
}

std::ostream &operator<<(std::ostream &os, const pragma::scenekit::NodeDesc &desc)
{
	os << desc.ToString();
	return os;
}
std::ostream &operator<<(std::ostream &os, const pragma::scenekit::GroupNodeDesc &desc)
{
	os << desc.ToString();
	return os;
}
