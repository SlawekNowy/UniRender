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
#include <render/shader.h>
#include <render/graph.h>
#include <render/scene.h>
#include <render/nodes.h>
#include <OpenImageIO/ustring.h>

std::shared_ptr<raytracing::CCLShader> raytracing::CCLShader::Create(Shader &shader,ccl::Shader &cclShader)
{
	cclShader.volume_sampling_method = ccl::VOLUME_SAMPLING_MULTIPLE_IMPORTANCE;

	ccl::ShaderGraph *graph = new ccl::ShaderGraph();
	auto pShader = std::shared_ptr<CCLShader>{new CCLShader{shader,cclShader,*graph}};
	pShader->m_bDeleteGraphIfUnused = true;

	for(auto *pNode : graph->nodes)
	{
		auto node = ShaderNode::Create(*pShader,*pNode);
		pShader->m_nodes.push_back(node);
	}
	shader.GetScene().AddShader(*pShader);
	return pShader;
}
std::shared_ptr<raytracing::CCLShader> raytracing::CCLShader::Create(Shader &shader)
{
	auto *cclShader = new ccl::Shader{}; // Object will be removed automatically by cycles
	cclShader->name = shader.GetName();
	shader.GetScene()->shaders.push_back(cclShader);
	return Create(shader,*cclShader);
}

raytracing::CCLShader::CCLShader(Shader &shader,ccl::Shader &cclShader,ccl::ShaderGraph &cclShaderGraph)
	: m_shader{shader},m_cclShader{cclShader},m_cclGraph{cclShaderGraph}
{}

raytracing::CCLShader::~CCLShader()
{
	if(m_bDeleteGraphIfUnused)
		delete &m_cclGraph;
}

ccl::Shader *raytracing::CCLShader::operator->() {return &m_cclShader;}
ccl::Shader *raytracing::CCLShader::operator*() {return &m_cclShader;}

void raytracing::CCLShader::Finalize()
{
	m_bDeleteGraphIfUnused = false; // Graph will be deleted by Cycles
	m_cclShader.set_graph(&m_cclGraph);
	m_cclShader.tag_update(*GetShader().GetScene());
}

raytracing::PShaderNode raytracing::CCLShader::AddNode(const std::string &type,const std::string &name)
{
	auto *nodeType = ccl::NodeType::find(ccl::ustring{type});
	auto *snode = nodeType ? static_cast<ccl::ShaderNode*>(nodeType->create(nodeType)) : nullptr;
	if(snode == nullptr)
	{
		std::cerr<<"ERROR: Unable to create node of type '"<<type<<"': Invalid type!"<<std::endl;
		return nullptr;
	}
	snode->name = name;
	m_cclGraph.add(snode);

	auto node = ShaderNode::Create(*this,*snode);
	m_nodes.push_back(node);
	return node;
}

raytracing::PShaderNode raytracing::CCLShader::FindNode(const std::string &name) const
{
	auto it = std::find_if(m_nodes.begin(),m_nodes.end(),[&name](const PShaderNode &node) {
		return (*node)->name == name;
		});
	return (it != m_nodes.end()) ? *it : nullptr;
}

bool raytracing::CCLShader::ValidateSocket(const std::string &nodeName,const std::string &socketName,bool output) const
{
	auto node = FindNode(nodeName);
	if(node == nullptr)
	{
		std::string msg = "Validation failure: Shader '" +std::string{m_cclShader.name} +"' has no node of name '" +nodeName +"'!";
		std::cerr<<msg<<std::endl;
		throw std::invalid_argument{msg};
		return false;
	}
	if(output)
	{
		auto *output = node->FindOutput(socketName);
		if(output == nullptr)
		{
			std::string msg = "Validation failure: Node '" +nodeName +"' (" +std::string{(*node)->type->name} +") of shader '" +std::string{m_cclShader.name} +"' has no output of name '" +socketName +"'!";
			std::cerr<<msg<<std::endl;
			throw std::invalid_argument{msg};
			return false;
		}
	}
	else
	{
		auto *input = node->FindInput(socketName);
		if(input == nullptr)
		{
			std::string msg = "Validation failure: Node '" +nodeName +"' (" +std::string{(*node)->type->name} +") of shader '" +std::string{m_cclShader.name} +"' has no input of name '" +socketName +"'!";
			std::cerr<<msg<<std::endl;
			throw std::invalid_argument{msg};
			return false;
		}
	}
	return true;
}

static ccl::ShaderInput *find_link(ccl::ShaderOutput &output,ccl::ShaderInput &input,std::unordered_set<ccl::ShaderOutput*> &iteratedOutputs)
{
	auto it = iteratedOutputs.find(&output);
	if(it != iteratedOutputs.end())
		return nullptr; // Prevent potential infinite recursion
	iteratedOutputs.insert(&output);
	for(auto *link : output.links)
	{
		if(link == &input)
			return link;
		if(link->parent == nullptr)
			continue;
		for(auto *output : link->parent->outputs)
		{
			auto *linkChld = find_link(*output,input,iteratedOutputs);
			if(linkChld)
				return link;
		}
	}
	return false;
}
// Finds the link from output to input, regardless of whether they're linked directly or through a chain
static ccl::ShaderInput *find_link(ccl::ShaderOutput &output,ccl::ShaderInput &input)
{
	std::unordered_set<ccl::ShaderOutput*> iteratedOutputs {};
	return find_link(output,input,iteratedOutputs);
}

void raytracing::CCLShader::Disconnect(const Socket &socket)
{
	if(ValidateSocket(socket.nodeName,socket.socketName,socket.IsOutput()) == false)
		return;
	auto node = FindNode(socket.nodeName);
	if(socket.IsOutput())
	{
		auto *output = node->FindOutput(socket.socketName);
		m_cclGraph.disconnect(output);
	}
	else
	{
		auto *input = node->FindInput(socket.socketName);
		m_cclGraph.disconnect(input);
	}
}
bool raytracing::CCLShader::Link(
	const std::string &fromNodeName,const std::string &fromSocketName,
	const std::string &toNodeName,const std::string &toSocketName,
	bool breakExistingLinks
)
{
	if(ValidateSocket(fromNodeName,fromSocketName,true) == false)
		return false;
	if(ValidateSocket(toNodeName,toSocketName,false) == false)
		return false;
	auto srcNode = FindNode(fromNodeName);
	auto dstNode = FindNode(toNodeName);
	auto *output = srcNode->FindOutput(fromSocketName);
	auto *input = dstNode->FindInput(toSocketName);
	if(breakExistingLinks)
	{
		// Break the link if it already exists
		auto *lnk = find_link(*output,*input);
		if(lnk)
		{
			auto it = std::find(output->links.begin(),output->links.end(),lnk);
			if(it != output->links.end())
				output->links.erase(it);
		}
		input->link = nullptr;
	}
	m_cclGraph.connect(output,input);
	return true;
}
bool raytracing::CCLShader::Link(const Socket &fromSocket,const Socket &toSocket,bool breakExistingLinks)
{
	return Link(fromSocket.nodeName,fromSocket.socketName,toSocket.nodeName,toSocket.socketName,breakExistingLinks);
}
bool raytracing::CCLShader::Link(const NumberSocket &fromSocket,const Socket &toSocket)
{
	return Link(
		fromSocket.m_socket.has_value() ? *fromSocket.m_socket : *AddConstantNode(fromSocket.m_value).outValue.m_socket,
		toSocket
	);
}
bool raytracing::CCLShader::Link(const Socket &fromSocket,const NumberSocket &toSocket)
{
	return Link(
		fromSocket,
		toSocket.m_socket.has_value() ? *toSocket.m_socket : *AddConstantNode(toSocket.m_value).outValue.m_socket
	);
}
bool raytracing::CCLShader::Link(const NumberSocket &fromSocket,const NumberSocket &toSocket)
{
	return Link(
		fromSocket.m_socket.has_value() ? *fromSocket.m_socket : *AddConstantNode(fromSocket.m_value).outValue.m_socket,
		toSocket.m_socket.has_value() ? *toSocket.m_socket : *AddConstantNode(toSocket.m_value).outValue.m_socket
	);
}

std::string raytracing::CCLShader::GetCurrentInternalNodeName() const {return "internal_" +std::to_string(m_nodes.size());}

raytracing::OutputNode raytracing::CCLShader::GetOutputNode() const {return {*const_cast<CCLShader*>(this),"output"};}
raytracing::MathNode raytracing::CCLShader::AddMathNode()
{
	// Add a dummy math node
	auto name = GetCurrentInternalNodeName();
	auto &nodeV0 = *static_cast<ccl::MathNode*>(**AddNode("math",name));
	nodeV0.type = ccl::NodeMathType::NODE_MATH_ADD;
	nodeV0.value1 = 0.f;
	nodeV0.value2 = 0.f;
	return {*this,name,nodeV0};
}
raytracing::MathNode raytracing::CCLShader::AddMathNode(const NumberSocket &socket0,const NumberSocket &socket1,ccl::NodeMathType mathOp)
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeV0 = *static_cast<ccl::MathNode*>(**AddNode("math",name));
	MathNode nodeMath {*this,name,nodeV0};
	nodeV0.type = mathOp;

	if(socket0.m_socket.has_value())
		Link(*socket0.m_socket,nodeMath.inValue1);
	else
		nodeV0.value1 = socket0.m_value;

	if(socket1.m_socket.has_value())
		Link(*socket1.m_socket,nodeMath.inValue2);
	else
		nodeV0.value2 = socket1.m_value;
	return nodeMath;
}
raytracing::MathNode raytracing::CCLShader::AddConstantNode(float f)
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeV0 = *static_cast<ccl::MathNode*>(**AddNode("math",name));
	MathNode nodeMath {*this,name,nodeV0};
	nodeV0.type = ccl::NodeMathType::NODE_MATH_ADD;
	nodeMath.SetValue1(f);
	nodeMath.SetValue2(0.f);
	return nodeMath;
}
raytracing::ImageTextureNode raytracing::CCLShader::AddImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket,bool color)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::ImageTextureNode*>(**AddNode("image_texture",name));
	cclNode.filename = fileName;
	cclNode.colorspace = color ? ccl::u_colorspace_srgb : ccl::u_colorspace_raw;
	raytracing::ImageTextureNode nodeImageTexture {*this,name};
	if(uvSocket.has_value())
		Link(*uvSocket,nodeImageTexture.inUVW);
	return {*this,name};
}
raytracing::ImageTextureNode raytracing::CCLShader::AddColorImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket) {return AddImageTextureNode(fileName,uvSocket,true);}
raytracing::ImageTextureNode raytracing::CCLShader::AddGradientImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket) {return AddImageTextureNode(fileName,uvSocket,false);}
raytracing::NormalMapNode raytracing::CCLShader::AddNormalMapImageTextureNode(const std::string &fileName,const std::string &meshName,const std::optional<Socket> &uvSocket,NormalMapNode::Space space)
{
	auto nodeImgNormal = AddGradientImageTextureNode(fileName,uvSocket);
	auto nodeNormalMap = AddNormalMapNode();
	nodeNormalMap.SetSpace(space);
	nodeNormalMap.SetAttribute(meshName);

	constexpr auto flipYAxis = false;
	if(flipYAxis)
	{
		// We need to invert the y-axis for cycles, so we separate the rgb components, invert the g channel and put them back together
		// Separate rgb components of input image
		auto nodeNormalRGB = AddSeparateRGBNode(nodeImgNormal);

		// Invert y-axis of normal
		auto nodeInvertY = 1.f -nodeNormalRGB.outG;

		// Re-combine rgb components
		auto nodeNormalInverted = AddCombineRGBNode(nodeNormalRGB.outR,nodeInvertY,nodeNormalRGB.outB);
		Link(nodeNormalInverted,nodeNormalMap.inColor);
	}
	else
		Link(nodeImgNormal,nodeNormalMap.inColor);
	return nodeNormalMap;
}
raytracing::EnvironmentTextureNode raytracing::CCLShader::AddEnvironmentTextureNode(const std::string &fileName)
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeImageTexture = *static_cast<ccl::EnvironmentTextureNode*>(**AddNode("environment_texture",name));
	nodeImageTexture.filename = fileName;
	nodeImageTexture.colorspace = ccl::u_colorspace_srgb;
	nodeImageTexture.projection = ccl::NodeEnvironmentProjection::NODE_ENVIRONMENT_EQUIRECTANGULAR;
	return {*this,name};
}
raytracing::SeparateXYZNode raytracing::CCLShader::AddSeparateXYZNode(const Socket &srcSocket)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::SeparateXYZNode*>(**AddNode("separate_xyz",name));
	raytracing::SeparateXYZNode nodeSeparateXYZ {*this,name,cclNode};
	Link(srcSocket,nodeSeparateXYZ.inVector);
	return nodeSeparateXYZ;
}
raytracing::CombineXYZNode raytracing::CCLShader::AddCombineXYZNode(const std::optional<const NumberSocket> &x,const std::optional<const NumberSocket> &y,const std::optional<const NumberSocket> &z)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::CombineXYZNode*>(**AddNode("combine_xyz",name));
	cclNode.x = 0.f;
	cclNode.y = 0.f;
	cclNode.z = 0.f;
	raytracing::CombineXYZNode node {*this,name,cclNode};
	if(x.has_value())
		Link(*x,node.inX);
	if(y.has_value())
		Link(*y,node.inY);
	if(z.has_value())
		Link(*z,node.inZ);
	return node;
}
raytracing::SeparateRGBNode raytracing::CCLShader::AddSeparateRGBNode(const Socket &srcSocket)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::SeparateRGBNode*>(**AddNode("separate_rgb",name));
	raytracing::SeparateRGBNode nodeSeparateRGB {*this,name,cclNode};
	Link(srcSocket,nodeSeparateRGB.inColor);
	return nodeSeparateRGB;
}
raytracing::CombineRGBNode raytracing::CCLShader::AddCombineRGBNode(const std::optional<const NumberSocket> &x,const std::optional<const NumberSocket> &y,const std::optional<const NumberSocket> &z)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::CombineRGBNode*>(**AddNode("combine_rgb",name));
	cclNode.r = 0.f;
	cclNode.g = 0.f;
	cclNode.b = 0.f;
	raytracing::CombineRGBNode node {*this,name,cclNode};
	if(x.has_value())
		Link(*x,node.inR);
	if(y.has_value())
		Link(*y,node.inG);
	if(z.has_value())
		Link(*z,node.inB);
	return node;
}
raytracing::GeometryNode raytracing::CCLShader::AddGeometryNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeGeometry = *static_cast<ccl::GeometryNode*>(**AddNode("geometry",name));
	return {*this,name};
}
raytracing::CameraDataNode raytracing::CCLShader::AddCameraDataNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeCamera = *static_cast<ccl::CameraNode*>(**AddNode("camera_info",name));
	return {*this,name,nodeCamera};
}

raytracing::NormalMapNode raytracing::CCLShader::AddNormalMapNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeNormalMap = *static_cast<ccl::NormalMapNode*>(**AddNode("normal_map",name));
	return {*this,name,nodeNormalMap};
}

raytracing::LightPathNode raytracing::CCLShader::AddLightPathNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &lightPathNode = *static_cast<ccl::LightPathNode*>(**AddNode("light_path",name));
	return {*this,name,lightPathNode};
}

raytracing::MixClosureNode raytracing::CCLShader::AddMixClosureNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeMixClosure = *static_cast<ccl::MixClosureNode*>(**AddNode("mix_closure",name));
	return {*this,name,nodeMixClosure};
}

raytracing::ScatterVolumeNode raytracing::CCLShader::AddScatterVolumeNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeScatterVolume = *static_cast<ccl::ScatterVolumeNode*>(**AddNode("scatter_volume",name));
	return {*this,name,nodeScatterVolume};
}

raytracing::HSVNode raytracing::CCLShader::AddHSVNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeHSV = *static_cast<ccl::HSVNode*>(**AddNode("hsv",name));
	return {*this,name,nodeHSV};
}

raytracing::BackgroundNode raytracing::CCLShader::AddBackgroundNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeBackground = *static_cast<ccl::BackgroundNode*>(**AddNode("background_shader",name));
	return {*this,name,nodeBackground};
}

raytracing::TextureCoordinateNode raytracing::CCLShader::AddTextureCoordinateNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeTexCoord = *static_cast<ccl::TextureCoordinateNode*>(**AddNode("texture_coordinate",name));
	return {*this,name,nodeTexCoord};
}

raytracing::MappingNode raytracing::CCLShader::AddMappingNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeMapping = *static_cast<ccl::MappingNode*>(**AddNode("mapping",name));
	return {*this,name,nodeMapping};
}

raytracing::ColorNode raytracing::CCLShader::AddColorNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeMapping = *static_cast<ccl::ColorNode*>(**AddNode("color",name));
	return {*this,name,nodeMapping};
}

raytracing::AttributeNode raytracing::CCLShader::AddAttributeNode(ccl::AttributeStandard attrType)
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeAttr = *static_cast<ccl::AttributeNode*>(**AddNode("attribute",name));
	return {*this,name,nodeAttr};
}

raytracing::NumberSocket raytracing::CCLShader::AddVertexAlphaNode()
{
	static_assert(Mesh::ALPHA_ATTRIBUTE_TYPE == ccl::AttributeStandard::ATTR_STD_POINTINESS);
	return AddGeometryNode().outPointiness;
}
raytracing::NumberSocket raytracing::CCLShader::AddWrinkleFactorNode() {return AddVertexAlphaNode();}

raytracing::EmissionNode raytracing::CCLShader::AddEmissionNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeEmission = *static_cast<ccl::EmissionNode*>(**AddNode("emission",name));
	return {*this,name};
}

raytracing::MixNode raytracing::CCLShader::AddMixNode(MixNode::Type type)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::MixNode*>(**AddNode("mix",name));
	MixNode nodeMix {*this,name,cclNode};
	nodeMix.SetType(type);
	return nodeMix;
}

raytracing::MixNode raytracing::CCLShader::AddMixNode(const Socket &socketColor1,const Socket &socketColor2,MixNode::Type type,const std::optional<const NumberSocket> &fac)
{
	auto nodeMix = AddMixNode(type);
	if(fac.has_value() == false)
		nodeMix.SetFactor(0.5f);
	else
		Link(*fac,nodeMix.inFac);
	Link(socketColor1,nodeMix.inColor1);
	Link(socketColor2,nodeMix.inColor2);
	return nodeMix;
}

raytracing::PrincipledBSDFNode raytracing::CCLShader::AddPrincipledBSDFNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodePrincipledBSDF = *static_cast<ccl::PrincipledBsdfNode*>(**AddNode("principled_bsdf",name));
	return {*this,name,nodePrincipledBSDF};
}

raytracing::ToonBSDFNode raytracing::CCLShader::AddToonBSDFNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeToonBSDF = *static_cast<ccl::ToonBsdfNode*>(**AddNode("toon_bsdf",name));
	return {*this,name,nodeToonBSDF};
}

raytracing::GlassBSDFNode raytracing::CCLShader::AddGlassBSDFNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeGlassBsdf = *static_cast<ccl::GlassBsdfNode*>(**AddNode("glass_bsdf",name));
	return {*this,name,nodeGlassBsdf};
}

raytracing::TransparentBsdfNode raytracing::CCLShader::AddTransparentBSDFNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeTransparentBsdf = *static_cast<ccl::TransparentBsdfNode*>(**AddNode("transparent_bsdf",name));
	return {*this,name,nodeTransparentBsdf};
}

raytracing::DiffuseBsdfNode raytracing::CCLShader::AddDiffuseBSDFNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeDiffuseBsdf = *static_cast<ccl::DiffuseBsdfNode*>(**AddNode("diffuse_bsdf",name));
	return {*this,name,nodeDiffuseBsdf};
}

raytracing::MixClosureNode raytracing::CCLShader::AddTransparencyClosure(const Socket &colorSocket,const NumberSocket &alphaSocket,AlphaMode alphaMode,float alphaCutoff)
{
	auto nodeTransparentBsdf = AddTransparentBSDFNode();
	nodeTransparentBsdf.SetColor(Vector3{1.f,1.f,1.f});

	auto alpha = ApplyAlphaMode(alphaSocket,alphaMode,alphaCutoff);
	auto nodeMixTransparency = AddMixClosureNode();
	Link(alpha,nodeMixTransparency.inFac); // Alpha transparency
	Link(nodeTransparentBsdf.outBsdf,nodeMixTransparency.inClosure1);
	Link(colorSocket,nodeMixTransparency.inClosure2);
	return nodeMixTransparency;
}

raytracing::NumberSocket raytracing::CCLShader::ApplyAlphaMode(const NumberSocket &alphaSocket,AlphaMode alphaMode,float alphaCutoff)
{
	auto alpha = alphaSocket;
	switch(alphaMode)
	{
	case AlphaMode::Opaque:
		alpha = 1.f;
		break;
	case AlphaMode::Mask:
		alpha = 1.f -AddMathNode(alpha,alphaCutoff,ccl::NodeMathType::NODE_MATH_LESS_THAN); // Greater or equal
		break;
	}
	return alpha;
}

raytracing::Shader &raytracing::CCLShader::GetShader() const {return m_shader;}

std::optional<raytracing::Socket> raytracing::CCLShader::GetUVSocket(Shader::TextureType type,ShaderModuleSpriteSheet *shaderModSpriteSheet,SpriteSheetFrame frame)
{
	auto uvSocket = m_uvSockets.at(umath::to_integral(type));
	if(shaderModSpriteSheet == nullptr || shaderModSpriteSheet->GetSpriteSheetData().has_value() == false)
		return uvSocket;
	if(uvSocket.has_value() == false)
		uvSocket = AddTextureCoordinateNode().outUv;
	auto &spriteSheetData = *shaderModSpriteSheet->GetSpriteSheetData();
	auto separateUv = AddSeparateXYZNode(*uvSocket);
	switch(frame)
	{
	case SpriteSheetFrame::First:
	{
		auto uv0Start = Scene::ToCyclesUV(spriteSheetData.uv0.first);
		auto uv0End = Scene::ToCyclesUV(spriteSheetData.uv0.second);
		umath::swap(uv0Start.y,uv0End.y);
		auto x = uv0Start.x +separateUv.outX *(uv0End.x -uv0Start.x);
		auto y = uv0Start.y +separateUv.outY *(uv0End.y -uv0Start.y);
		return AddCombineXYZNode(x,y);
	}
	case SpriteSheetFrame::Second:
	{
		auto uv1Start = Scene::ToCyclesUV(spriteSheetData.uv1.first);
		umath::swap(uv1Start.y,uv1Start.y);
		auto uv1End = Scene::ToCyclesUV(spriteSheetData.uv1.second);
		auto x = uv1Start.x +separateUv.outX *(uv1End.x -uv1Start.x);
		auto y = uv1Start.y +separateUv.outY *(uv1End.y -uv1Start.y);
		return AddCombineXYZNode(x,y);
	}
	}
	return {};
}
