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

raytracing::Socket::Socket()
	: m_shader{nullptr}
{}
raytracing::Socket::Socket(CCLShader &shader,const std::string &nodeName,const std::string &socketName,bool output)
	: m_shader{&shader},nodeName{nodeName},socketName{socketName},m_bOutput{output}
{
	shader.ValidateSocket(nodeName,socketName,output);
}
raytracing::CCLShader &raytracing::Socket::GetShader() const {return *m_shader;}

bool raytracing::Socket::IsOutput() const {return m_bOutput;}
bool raytracing::Socket::IsInput() const {return !IsOutput();}

raytracing::MathNode raytracing::Socket::operator+(float value) const {return m_shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_ADD);}
raytracing::MathNode raytracing::Socket::operator+(const Socket &socket) const {return m_shader->AddMathNode(*this,socket,ccl::NodeMathType::NODE_MATH_ADD);}

raytracing::MathNode raytracing::Socket::operator-(float value) const {return m_shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_SUBTRACT);}
raytracing::MathNode raytracing::Socket::operator-(const Socket &socket) const {return m_shader->AddMathNode(*this,socket,ccl::NodeMathType::NODE_MATH_SUBTRACT);}

raytracing::MathNode raytracing::Socket::operator*(float value) const {return m_shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_MULTIPLY);}
raytracing::MathNode raytracing::Socket::operator*(const Socket &socket) const {return m_shader->AddMathNode(*this,socket,ccl::NodeMathType::NODE_MATH_MULTIPLY);}

raytracing::MathNode raytracing::Socket::operator/(float value) const {return m_shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_DIVIDE);}
raytracing::MathNode raytracing::Socket::operator/(const Socket &socket) const {return m_shader->AddMathNode(*this,socket,ccl::NodeMathType::NODE_MATH_DIVIDE);}

raytracing::Node::Node(CCLShader &shader)
	: m_shader{&shader}
{}

raytracing::CCLShader &raytracing::Node::GetShader() const {return *m_shader;}

raytracing::MathNode::MathNode(CCLShader &shader,const std::string &nodeName,ccl::MathNode &node)
	: Node{shader},NumberSocket{0.f},inValue1{shader,nodeName,"value1",false},inValue2{shader,nodeName,"value2",false},
	outValue{Socket{shader,nodeName,"value"}},m_node{&node}
{
	NumberSocket::m_socket = *outValue.m_socket;
	NumberSocket::m_shader = &shader;
}

raytracing::HSVNode::HSVNode(CCLShader &shader,const std::string &nodeName,ccl::HSVNode &hsvNode)
	: Node{shader},inHue{Socket{shader,nodeName,"hue",false}},inSaturation{Socket{shader,nodeName,"saturation",false}},
	inValue{Socket{shader,nodeName,"value",false}},inFac{Socket{shader,nodeName,"fac",false}},inColor{shader,nodeName,"color",false},
	outColor{Socket{shader,nodeName,"color"}}
{}
raytracing::HSVNode::operator const raytracing::Socket&() const {return outColor;}

raytracing::NumberSocket::NumberSocket()
	: NumberSocket{0.f}
{}
raytracing::NumberSocket::NumberSocket(const Socket &socket)
	: m_socket{socket}
{
	m_shader = &socket.GetShader();
}
raytracing::NumberSocket::NumberSocket(float value)
	: m_value{value}
{}

raytracing::CCLShader &raytracing::NumberSocket::GetShader() const {return *m_shader;}
raytracing::NumberSocket raytracing::NumberSocket::operator+(const NumberSocket &value) const
{
	auto *shader = m_shader ? m_shader : value.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return m_value +value.m_value;
	}
	return shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_ADD);
}
raytracing::NumberSocket raytracing::NumberSocket::operator-(const NumberSocket &value) const
{
	auto *shader = m_shader ? m_shader : value.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return m_value -value.m_value;
	}
	return shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_SUBTRACT);
}
raytracing::NumberSocket raytracing::NumberSocket::operator*(const NumberSocket &value) const
{
	auto *shader = m_shader ? m_shader : value.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return m_value *value.m_value;
	}
	return shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_MULTIPLY);
}
raytracing::NumberSocket raytracing::NumberSocket::operator/(const NumberSocket &value) const
{
	auto *shader = m_shader ? m_shader : value.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return m_value /value.m_value;
	}
	return shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_DIVIDE);
}
raytracing::NumberSocket raytracing::NumberSocket::operator-() const {return *this *-1.f;}
raytracing::NumberSocket raytracing::NumberSocket::pow(const NumberSocket &exponent) const
{
	auto *shader = m_shader ? m_shader : exponent.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return umath::pow(m_value,exponent.m_value);
	}
	return shader->AddMathNode(*this,exponent,ccl::NodeMathType::NODE_MATH_POWER);
}
raytracing::NumberSocket raytracing::NumberSocket::sqrt() const
{
	if(m_shader == nullptr)
		return umath::sqrt(m_value);
	return m_shader->AddMathNode(*this,0.f,ccl::NodeMathType::NODE_MATH_SQRT);
}
raytracing::NumberSocket raytracing::NumberSocket::clamp(const NumberSocket &min,const NumberSocket &max) const
{
	auto *shader = m_shader ? m_shader : min.m_shader ? min.m_shader : max.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return umath::clamp(m_value,min.m_value,max.m_value);
	}
	return shader->AddMathNode(
		shader->AddMathNode(*this,min,ccl::NodeMathType::NODE_MATH_MAXIMUM),
		max,
		ccl::NodeMathType::NODE_MATH_MINIMUM
	);
}
raytracing::NumberSocket raytracing::NumberSocket::lerp(const NumberSocket &to,const NumberSocket &by) const
{
	return *this *(1.f -by) +to *by;
}
raytracing::NumberSocket raytracing::NumberSocket::min(const NumberSocket &other) const
{
	auto *shader = m_shader ? m_shader : other.m_shader;
	return shader->AddMathNode(*this,other,ccl::NodeMathType::NODE_MATH_MINIMUM);
}
raytracing::NumberSocket raytracing::NumberSocket::max(const NumberSocket &other) const
{
	auto *shader = m_shader ? m_shader : other.m_shader;
	return shader->AddMathNode(*this,other,ccl::NodeMathType::NODE_MATH_MAXIMUM);
}
raytracing::NumberSocket raytracing::NumberSocket::len(const std::array<const NumberSocket,2> &v)
{
	return (v.at(0) *v.at(0) +v.at(1) *v.at(1)).sqrt();
}
raytracing::NumberSocket raytracing::NumberSocket::dot(
	const std::array<const NumberSocket,4> &v0,
	const std::array<const NumberSocket,4> &v1
)
{
	return v0.at(0) *v1.at(0) +
		v0.at(1) *v1.at(1) +
		v0.at(2) *v1.at(2) +
		v0.at(3) *v1.at(3);
}

void raytracing::MathNode::SetValue1(float value) {m_node->value1 = value;}
void raytracing::MathNode::SetValue2(float value) {m_node->value2 = value;}
void raytracing::MathNode::SetType(ccl::NodeMathType type) {m_node->type = type;}
raytracing::MathNode::operator const raytracing::NumberSocket&() const {return outValue;}
raytracing::NumberSocket operator+(float value,const raytracing::NumberSocket &socket) {return socket +value;}
raytracing::NumberSocket operator-(float value,const raytracing::NumberSocket &socket)
{
	if(&socket.GetShader() == nullptr)
		return value -socket.m_value;
	return socket.GetShader().AddMathNode(value,socket,ccl::NodeMathType::NODE_MATH_SUBTRACT);
}
raytracing::NumberSocket operator*(float value,const raytracing::NumberSocket &socket) {return socket *value;}
raytracing::NumberSocket operator/(float value,const raytracing::NumberSocket &socket)
{
	if(&socket.GetShader() == nullptr)
		return value /socket.m_value;
	return socket.GetShader().AddMathNode(value,socket,ccl::NodeMathType::NODE_MATH_DIVIDE);
}

raytracing::SeparateXYZNode::SeparateXYZNode(CCLShader &shader,const std::string &nodeName,ccl::SeparateXYZNode &node)
	: Node{shader},inVector{shader,nodeName,"vector",false},outX{Socket{shader,nodeName,"x"}},outY{Socket{shader,nodeName,"y"}},outZ{Socket{shader,nodeName,"z"}},
	m_node{&node}
{}

void raytracing::SeparateXYZNode::SetVector(const Vector3 &v) {m_node->vector = {v.x,v.y,v.z};}

raytracing::CombineXYZNode::CombineXYZNode(CCLShader &shader,const std::string &nodeName,ccl::CombineXYZNode &node)
	: Node{shader},outVector{shader,nodeName,"vector"},inX{shader,nodeName,"x",false},inY{shader,nodeName,"y",false},inZ{shader,nodeName,"z",false},
	m_node{&node}
{}

void raytracing::CombineXYZNode::SetX(float x) {m_node->x = x;}
void raytracing::CombineXYZNode::SetY(float y) {m_node->y = y;}
void raytracing::CombineXYZNode::SetZ(float z) {m_node->z = z;}

raytracing::CombineXYZNode::operator const raytracing::Socket&() const {return outVector;}

raytracing::SeparateRGBNode::SeparateRGBNode(CCLShader &shader,const std::string &nodeName,ccl::SeparateRGBNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},outR{Socket{shader,nodeName,"r"}},outG{Socket{shader,nodeName,"g"}},outB{Socket{shader,nodeName,"b"}},
	m_node{&node}
{}

void raytracing::SeparateRGBNode::SetColor(const Vector3 &c) {m_node->color = {c.r,c.g,c.b};}

raytracing::CombineRGBNode::CombineRGBNode(CCLShader &shader,const std::string &nodeName,ccl::CombineRGBNode &node)
	: Node{shader},outColor{shader,nodeName,"image"},inR{shader,nodeName,"r",false},inG{shader,nodeName,"g",false},inB{shader,nodeName,"b",false},
	m_node{&node}
{}

raytracing::CombineRGBNode::operator const raytracing::Socket&() const {return outColor;}

void raytracing::CombineRGBNode::SetR(float r) {m_node->r = r;}
void raytracing::CombineRGBNode::SetG(float g) {m_node->g = g;}
void raytracing::CombineRGBNode::SetB(float b) {m_node->b = b;}

raytracing::GeometryNode::GeometryNode(CCLShader &shader,const std::string &nodeName)
	: Node{shader},inNormal{shader,nodeName,"normal_osl",false},
	outPosition{shader,nodeName,"position"},
	outNormal{shader,nodeName,"normal"},
	outTangent{shader,nodeName,"tangent"},
	outTrueNormal{shader,nodeName,"true_normal"},
	outIncoming{shader,nodeName,"incoming"},
	outParametric{shader,nodeName,"parametric"},
	outBackfacing{Socket{shader,nodeName,"backfacing"}},
	outPointiness{Socket{shader,nodeName,"pointiness"}}
{}

raytracing::CameraDataNode::CameraDataNode(CCLShader &shader,const std::string &nodeName,ccl::CameraNode &node)
	: Node{shader},outViewVector{shader,nodeName,"view_vector"},outViewZDepth{Socket{shader,nodeName,"view_z_depth"}},outViewDistance{Socket{shader,nodeName,"view_distance"}}
{}

raytracing::ImageTextureNode::ImageTextureNode(CCLShader &shader,const std::string &nodeName)
	: Node{shader},inUVW{shader,nodeName,"vector",false},outColor{shader,nodeName,"color"},outAlpha{Socket{shader,nodeName,"alpha"}}
{}

raytracing::ImageTextureNode::operator const raytracing::Socket&() const {return outColor;}

raytracing::EnvironmentTextureNode::EnvironmentTextureNode(CCLShader &shader,const std::string &nodeName)
	: Node{shader},inVector{shader,nodeName,"vector",false},outColor{shader,nodeName,"color"},outAlpha{Socket{shader,nodeName,"alpha"}}
{}

raytracing::EnvironmentTextureNode::operator const raytracing::Socket&() const {return outColor;}

raytracing::MixClosureNode::MixClosureNode(CCLShader &shader,const std::string &nodeName,ccl::MixClosureNode &node)
	: Node{shader},inFac{Socket{shader,nodeName,"fac",false}},inClosure1{shader,nodeName,"closure1",false},inClosure2{shader,nodeName,"closure2",false},outClosure{shader,nodeName,"closure"},
	m_node{&node}
{}

raytracing::MixClosureNode::operator const raytracing::Socket&() const {return outClosure;}

void raytracing::MixClosureNode::SetFactor(float fac) {m_node->fac = fac;}

raytracing::AddClosureNode::AddClosureNode(CCLShader &shader,const std::string &nodeName,ccl::AddClosureNode &node)
	: Node{shader},inClosure1{shader,nodeName,"closure1",false},inClosure2{shader,nodeName,"closure2",false},outClosure{shader,nodeName,"closure"},
	m_node{&node}
{}

raytracing::AddClosureNode::operator const raytracing::Socket&() const {return outClosure;}

raytracing::BackgroundNode::BackgroundNode(CCLShader &shader,const std::string &nodeName,ccl::BackgroundNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},inStrength{Socket{shader,nodeName,"strength",false}},inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	outBackground{shader,nodeName,"background"},m_node{&node}
{}

raytracing::BackgroundNode::operator const raytracing::Socket&() const {return outBackground;}

void raytracing::BackgroundNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void raytracing::BackgroundNode::SetStrength(float strength) {m_node->strength = strength;}
void raytracing::BackgroundNode::SetSurfaceMixWeight(float surfaceMixWeight) {m_node->surface_mix_weight = surfaceMixWeight;}

raytracing::TextureCoordinateNode::TextureCoordinateNode(CCLShader &shader,const std::string &nodeName,ccl::TextureCoordinateNode &node)
	: Node{shader},inNormal{shader,nodeName,"normal_osl",false},outGenerated{shader,nodeName,"generated"},outNormal{shader,nodeName,"normal"},
	outUv{shader,nodeName,"uv"},outObject{shader,nodeName,"object"},outCamera{shader,nodeName,"camera"},outWindow{shader,nodeName,"window"},
	outReflection{shader,nodeName,"reflection"},m_node{&node}
{}

raytracing::MappingNode::MappingNode(CCLShader &shader,const std::string &nodeName,ccl::MappingNode &node)
	: Node{shader},inVector{shader,nodeName,"vector",false},outVector{shader,nodeName,"vector"},m_node{&node}
{}

raytracing::MappingNode::operator const raytracing::Socket&() const {return outVector;}

void raytracing::MappingNode::SetType(Type type)
{
	switch(type)
	{
	case Type::Point:
		m_node->type = ccl::NodeMappingType::NODE_MAPPING_TYPE_POINT;
		break;
	case Type::Texture:
		m_node->type = ccl::NodeMappingType::NODE_MAPPING_TYPE_TEXTURE;
		break;
	case Type::Vector:
		m_node->type = ccl::NodeMappingType::NODE_MAPPING_TYPE_VECTOR;
		break;
	case Type::Normal:
		m_node->type = ccl::NodeMappingType::NODE_MAPPING_TYPE_NORMAL;
		break;
	}
}
void raytracing::MappingNode::SetRotation(const EulerAngles &ang)
{
	auto cclAngles = ang;
	cclAngles.p -= 90.f;
	m_node->rotation = {
		static_cast<float>(umath::deg_to_rad(-cclAngles.p)),
		static_cast<float>(umath::deg_to_rad(cclAngles.r)),
		static_cast<float>(umath::deg_to_rad(-cclAngles.y))
	};
}

raytracing::ScatterVolumeNode::ScatterVolumeNode(CCLShader &shader,const std::string &nodeName,ccl::ScatterVolumeNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},inDensity{Socket{shader,nodeName,"density",false}},inAnisotropy{Socket{shader,nodeName,"anisotropy",false}},
	inVolumeMixWeight{Socket{shader,nodeName,"volume_mix_weight",false}},outVolume{shader,nodeName,"volume"},m_node{&node}
{}

raytracing::ScatterVolumeNode::operator const raytracing::Socket&() const {return outVolume;}

raytracing::EmissionNode::EmissionNode(CCLShader &shader,const std::string &nodeName,ccl::EmissionNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},inStrength{Socket{shader,nodeName,"strength",false}},inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	outEmission{shader,nodeName,"emission"},m_node{&node}
{
	node.strength = 1.f; // Default strength for emission node is 10, which is a bit much for our purposes
}

raytracing::EmissionNode::operator const raytracing::Socket&() const {return outEmission;}
void raytracing::EmissionNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void raytracing::EmissionNode::SetStrength(float strength) {m_node->strength = strength;}

raytracing::ColorNode::ColorNode(CCLShader &shader,const std::string &nodeName,ccl::ColorNode &node)
	: Node{shader},outColor{shader,nodeName,"color"},
	m_node{&node}
{}

raytracing::ColorNode::operator const raytracing::Socket&() const {return outColor;}

void raytracing::ColorNode::SetColor(const Vector3 &color) {m_node->value = {color.r,color.g,color.b};}

raytracing::AttributeNode::AttributeNode(CCLShader &shader,const std::string &nodeName,ccl::AttributeNode &node)
	: Node{shader},outColor{shader,nodeName,"color"},outVector{shader,nodeName,"vector"},outFactor{shader,nodeName,"fac"},
	m_node{&node}
{}
void raytracing::AttributeNode::SetAttribute(ccl::AttributeStandard attrType)
{
	m_node->attribute = ccl::Attribute::standard_name(attrType);
}

raytracing::LightPathNode::LightPathNode(CCLShader &shader,const std::string &nodeName,ccl::LightPathNode &node)
	: Node{shader},outIsCameraRay{Socket{shader,nodeName,"is_camera_ray"}},outIsShadowRay{Socket{shader,nodeName,"is_shadow_ray"}},outIsDiffuseRay{Socket{shader,nodeName,"is_diffuse_ray"}},
	outIsGlossyRay{Socket{shader,nodeName,"is_glossy_ray"}},outIsSingularRay{Socket{shader,nodeName,"is_singular_ray"}},outIsReflectionRay{Socket{shader,nodeName,"is_reflection_ray"}},outIsTransmissionRay{Socket{shader,nodeName,"is_transmission_ray"}},
	outIsVolumeScatterRay{Socket{shader,nodeName,"is_volume_scatter_ray"}},outRayLength{Socket{shader,nodeName,"ray_length"}},outRayDepth{Socket{shader,nodeName,"ray_depth"}},outDiffuseDepth{Socket{shader,nodeName,"diffuse_depth"}},
	outGlossyDepth{Socket{shader,nodeName,"glossy_depth"}},outTransparentDepth{Socket{shader,nodeName,"transparent_depth"}},outTransmissionDepth{Socket{shader,nodeName,"transmission_depth"}}
{}

raytracing::MixNode::MixNode(CCLShader &shader,const std::string &nodeName,ccl::MixNode &node)
	: Node{shader},inFac{Socket{shader,nodeName,"fac",false}},inColor1{shader,nodeName,"color1",false},inColor2{shader,nodeName,"color2",false},outColor{shader,nodeName,"color"},
	m_node{&node}
{}

raytracing::MixNode::operator const raytracing::Socket&() const {return outColor;}

void raytracing::MixNode::SetType(Type type)
{
	switch(type)
	{
	case Type::Mix:
		m_node->type = ccl::NODE_MIX_BLEND;
		break;
	case Type::Add:
		m_node->type = ccl::NODE_MIX_ADD;
		break;
	case Type::Multiply:
		m_node->type = ccl::NODE_MIX_MUL;
		break;
	case Type::Screen:
		m_node->type = ccl::NODE_MIX_SCREEN;
		break;
	case Type::Overlay:
		m_node->type = ccl::NODE_MIX_OVERLAY;
		break;
	case Type::Subtract:
		m_node->type = ccl::NODE_MIX_SUB;
		break;
	case Type::Divide:
		m_node->type = ccl::NODE_MIX_DIV;
		break;
	case Type::Difference:
		m_node->type = ccl::NODE_MIX_DIFF;
		break;
	case Type::Darken:
		m_node->type = ccl::NODE_MIX_DARK;
		break;
	case Type::Lighten:
		m_node->type = ccl::NODE_MIX_LIGHT;
		break;
	case Type::Dodge:
		m_node->type = ccl::NODE_MIX_DODGE;
		break;
	case Type::Burn:
		m_node->type = ccl::NODE_MIX_BURN;
		break;
	case Type::Hue:
		m_node->type = ccl::NODE_MIX_HUE;
		break;
	case Type::Saturation:
		m_node->type = ccl::NODE_MIX_SAT;
		break;
	case Type::Value:
		m_node->type = ccl::NODE_MIX_VAL;
		break;
	case Type::Color:
		m_node->type = ccl::NODE_MIX_COLOR;
		break;
	case Type::SoftLight:
		m_node->type = ccl::NODE_MIX_SOFT;
		break;
	case Type::LinearLight:
		m_node->type = ccl::NODE_MIX_LINEAR;
		break;
	}
}
void raytracing::MixNode::SetUseClamp(bool useClamp) {m_node->use_clamp = useClamp;}
void raytracing::MixNode::SetFactor(float fac) {m_node->fac = fac;}
void raytracing::MixNode::SetColor1(const Vector3 &color1) {m_node->color1 = {color1.r,color1.g,color1.b};}
void raytracing::MixNode::SetColor2(const Vector3 &color2) {m_node->color2 = {color2.r,color2.g,color2.b};}

raytracing::TransparentBsdfNode::TransparentBsdfNode(CCLShader &shader,const std::string &nodeName,ccl::TransparentBsdfNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},outBsdf{shader,nodeName,"bsdf"},
	m_node{&node}
{}

raytracing::TransparentBsdfNode::operator const raytracing::Socket&() const {return outBsdf;}

void raytracing::TransparentBsdfNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void raytracing::TransparentBsdfNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}

raytracing::TranslucentBsdfNode::TranslucentBsdfNode(CCLShader &shader,const std::string &nodeName,ccl::TranslucentBsdfNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},inNormal{shader,nodeName,"normal",false},inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},outBsdf{shader,nodeName,"bsdf"},
	m_node{&node}
{}

raytracing::TranslucentBsdfNode::operator const raytracing::Socket&() const {return outBsdf;}

void raytracing::TranslucentBsdfNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void raytracing::TranslucentBsdfNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}

raytracing::DiffuseBsdfNode::DiffuseBsdfNode(CCLShader &shader,const std::string &nodeName,ccl::DiffuseBsdfNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},inNormal{shader,nodeName,"normal",false},inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	inRoughness{Socket{shader,nodeName,"roughness",false}},outBsdf{shader,nodeName,"bsdf"}
{}

raytracing::DiffuseBsdfNode::operator const raytracing::Socket&() const {return outBsdf;}

void raytracing::DiffuseBsdfNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void raytracing::DiffuseBsdfNode::SetNormal(const Vector3 &normal) {m_node->normal = {normal.r,normal.g,normal.b};}
void raytracing::DiffuseBsdfNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}
void raytracing::DiffuseBsdfNode::SetRoughness(float roughness) {m_node->roughness = roughness;}

raytracing::NormalMapNode::NormalMapNode(CCLShader &shader,const std::string &nodeName,ccl::NormalMapNode &normalMapNode)
	: Node{shader},inStrength{Socket{shader,nodeName,"strength",false}},inColor{shader,nodeName,"color",false},outNormal{shader,nodeName,"normal"},
	m_node{&normalMapNode}
{}

raytracing::NormalMapNode::operator const raytracing::Socket&() const {return outNormal;}

void raytracing::NormalMapNode::SetStrength(float strength) {m_node->strength = strength;}
void raytracing::NormalMapNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void raytracing::NormalMapNode::SetSpace(Space space)
{
	switch(space)
	{
	case Space::Tangent:
		m_node->space = ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_TANGENT;
		break;
	case Space::Object:
		m_node->space = ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_OBJECT;
		break;
	case Space::World:
		m_node->space = ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_WORLD;
		break;
	}
}
void raytracing::NormalMapNode::SetAttribute(const std::string &attribute) {m_node->attribute = attribute;}

raytracing::PrincipledBSDFNode::PrincipledBSDFNode(CCLShader &shader,const std::string &nodeName,ccl::PrincipledBsdfNode &principledBSDF)
	: Node{shader},
	inBaseColor{shader,nodeName,"base_color",false},
	inSubsurfaceColor{shader,nodeName,"subsurface_color",false},
	inMetallic{Socket{shader,nodeName,"metallic",false}},
	inSubsurface{Socket{shader,nodeName,"subsurface",false}},
	inSubsurfaceRadius{Socket{shader,nodeName,"subsurface_radius",false}},
	inSpecular{Socket{shader,nodeName,"specular",false}},
	inRoughness{Socket{shader,nodeName,"roughness",false}},
	inSpecularTint{Socket{shader,nodeName,"specular_tint",false}},
	inAnisotropic{Socket{shader,nodeName,"anisotropic",false}},
	inSheen{Socket{shader,nodeName,"sheen",false}},
	inSheenTint{Socket{shader,nodeName,"sheen_tint",false}},
	inClearcoat{Socket{shader,nodeName,"clearcoat",false}},
	inClearcoatRoughness{Socket{shader,nodeName,"clearcoat_roughness",false}},
	inIOR{Socket{shader,nodeName,"ior",false}},
	inTransmission{Socket{shader,nodeName,"transmission",false}},
	inTransmissionRoughness{Socket{shader,nodeName,"transmission_roughness",false}},
	inAnisotropicRotation{Socket{shader,nodeName,"anisotropic_rotation",false}},
	inEmission{shader,nodeName,"emission",false},
	inAlpha{Socket{shader,nodeName,"alpha",false}},
	inNormal{shader,nodeName,"normal",false},
	inClearcoatNormal{shader,nodeName,"clearcoat_normal",false},
	inTangent{shader,nodeName,"tangent",false},
	inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	outBSDF{shader,nodeName,"bsdf"},
	m_node{&principledBSDF}
{}

raytracing::PrincipledBSDFNode::operator const raytracing::Socket&() const {return outBSDF;}

void raytracing::PrincipledBSDFNode::SetBaseColor(const Vector3 &color) {m_node->base_color = {color.r,color.g,color.b};}
void raytracing::PrincipledBSDFNode::SetSubsurfaceColor(const Vector3 &color) {m_node->subsurface_color = {color.r,color.g,color.b};}
void raytracing::PrincipledBSDFNode::SetMetallic(float metallic) {m_node->metallic = metallic;}
void raytracing::PrincipledBSDFNode::SetSubsurface(float subsurface) {m_node->subsurface = subsurface;}
void raytracing::PrincipledBSDFNode::SetSubsurfaceRadius(const Vector3 &subsurfaceRadius) {m_node->subsurface_radius = raytracing::Scene::ToCyclesPosition(subsurfaceRadius);}
void raytracing::PrincipledBSDFNode::SetSpecular(float specular) {m_node->specular = specular;}
void raytracing::PrincipledBSDFNode::SetRoughness(float roughness) {m_node->roughness = roughness;}
void raytracing::PrincipledBSDFNode::SetSpecularTint(float specularTint) {m_node->specular_tint = specularTint;}
void raytracing::PrincipledBSDFNode::SetAnisotropic(float anisotropic) {m_node->anisotropic = anisotropic;}
void raytracing::PrincipledBSDFNode::SetSheen(float sheen) {m_node->sheen = sheen;}
void raytracing::PrincipledBSDFNode::SetSheenTint(float sheenTint) {m_node->sheen_tint = sheenTint;}
void raytracing::PrincipledBSDFNode::SetClearcoat(float clearcoat) {m_node->clearcoat = clearcoat;}
void raytracing::PrincipledBSDFNode::SetClearcoatRoughness(float clearcoatRoughness) {m_node->clearcoat_roughness = clearcoatRoughness;}
void raytracing::PrincipledBSDFNode::SetIOR(float ior) {m_node->ior = ior;}
void raytracing::PrincipledBSDFNode::SetTransmission(float transmission) {m_node->transmission = transmission;}
void raytracing::PrincipledBSDFNode::SetTransmissionRoughness(float transmissionRoughness) {m_node->transmission_roughness = transmissionRoughness;}
void raytracing::PrincipledBSDFNode::SetAnisotropicRotation(float anisotropicRotation) {m_node->anisotropic_rotation = anisotropicRotation;}
void raytracing::PrincipledBSDFNode::SetEmission(const Vector3 &emission) {m_node->emission = {emission.r,emission.g,emission.b};}
void raytracing::PrincipledBSDFNode::SetAlpha(float alpha) {m_node->alpha = alpha;}
void raytracing::PrincipledBSDFNode::SetNormal(const Vector3 &normal) {m_node->normal = raytracing::Scene::ToCyclesNormal(normal);}
void raytracing::PrincipledBSDFNode::SetClearcoatNormal(const Vector3 &normal) {m_node->clearcoat_normal = raytracing::Scene::ToCyclesNormal(normal);}
void raytracing::PrincipledBSDFNode::SetTangent(const Vector3 &tangent) {m_node->tangent = raytracing::Scene::ToCyclesNormal(tangent);}
void raytracing::PrincipledBSDFNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}
void raytracing::PrincipledBSDFNode::SetDistribution(Distribution distribution)
{
	switch(distribution)
	{
	case Distribution::GGX:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;
		break;
	case Distribution::MultiscaterGGX:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID;
		break;
	}
}
void raytracing::PrincipledBSDFNode::SetSubsurfaceMethod(SubsurfaceMethod method)
{
	switch(method)
	{
	case SubsurfaceMethod::Cubic:
		m_node->subsurface_method = ccl::CLOSURE_BSSRDF_CUBIC_ID;
		break;
	case SubsurfaceMethod::Gaussian:
		m_node->subsurface_method = ccl::CLOSURE_BSSRDF_GAUSSIAN_ID;
		break;
	case SubsurfaceMethod::Principled:
		m_node->subsurface_method = ccl::CLOSURE_BSDF_BSSRDF_PRINCIPLED_ID;
		break;
	case SubsurfaceMethod::Burley:
		m_node->subsurface_method = ccl::CLOSURE_BSSRDF_BURLEY_ID;
		break;
	case SubsurfaceMethod::RandomWalk:
		m_node->subsurface_method = ccl::CLOSURE_BSSRDF_RANDOM_WALK_ID;
		break;
	case SubsurfaceMethod::PrincipledRandomWalk:
		m_node->subsurface_method = ccl::CLOSURE_BSSRDF_PRINCIPLED_RANDOM_WALK_ID;
		break;
	}
}

raytracing::ToonBSDFNode::ToonBSDFNode(CCLShader &shader,const std::string &nodeName,ccl::ToonBsdfNode &toonBsdf)
	: Node{shader},
	inColor{shader,nodeName,"color",false},
	inNormal{shader,nodeName,"normal",false},
	inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	inSize{Socket{shader,nodeName,"size",false}},
	inSmooth{Socket{shader,nodeName,"smooth",false}},
	outBSDF{shader,nodeName,"bsdf"},
	m_node{&toonBsdf}
{}

raytracing::ToonBSDFNode::operator const raytracing::Socket&() const {return outBSDF;}

void raytracing::ToonBSDFNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void raytracing::ToonBSDFNode::SetNormal(const Vector3 &normal) {m_node->normal = Scene::ToCyclesNormal(normal);}
void raytracing::ToonBSDFNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}
void raytracing::ToonBSDFNode::SetSize(float size) {m_node->size = size;}
void raytracing::ToonBSDFNode::SetSmooth(float smooth) {m_node->smooth = smooth;}
void raytracing::ToonBSDFNode::SetComponent(Component component)
{
	switch(component)
	{
	case Component::Diffuse:
		m_node->component = ccl::CLOSURE_BSDF_DIFFUSE_TOON_ID;
		break;
	case Component::Glossy:
		m_node->component = ccl::CLOSURE_BSDF_GLOSSY_TOON_ID;
		break;
	}
}

raytracing::GlassBSDFNode::GlassBSDFNode(CCLShader &shader,const std::string &nodeName,ccl::GlassBsdfNode &toonBsdf)
	: Node{shader},
	inColor{shader,nodeName,"color",false},
	inNormal{shader,nodeName,"normal",false},
	inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	inRoughness{Socket{shader,nodeName,"roughness",false}},
	inIOR{Socket{shader,nodeName,"ior",false}},
	outBSDF{shader,nodeName,"bsdf"},
	m_node{&toonBsdf}
{}

raytracing::GlassBSDFNode::operator const raytracing::Socket&() const {return outBSDF;}

void raytracing::GlassBSDFNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void raytracing::GlassBSDFNode::SetNormal(const Vector3 &normal) {m_node->normal = Scene::ToCyclesNormal(normal);}
void raytracing::GlassBSDFNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}
void raytracing::GlassBSDFNode::SetRoughness(float roughness) {m_node->roughness = roughness;}
void raytracing::GlassBSDFNode::SetIOR(float ior) {m_node->IOR = ior;}
void raytracing::GlassBSDFNode::SetDistribution(Distribution distribution)
{
	switch(distribution)
	{
	case Distribution::Sharp:
		m_node->distribution = ccl::CLOSURE_BSDF_SHARP_GLASS_ID;
		break;
	case Distribution::Beckmann:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID;
		break;
	case Distribution::GGX:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;
		break;
	case Distribution::MultiscatterGGX:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID;
		break;
	}
}

raytracing::OutputNode::OutputNode(CCLShader &shader,const std::string &nodeName)
	: Node{shader},
	inSurface{shader,nodeName,"surface",false},
	inVolume{shader,nodeName,"volume",false},
	inDisplacement{shader,nodeName,"displacement",false},
	inNormal{shader,nodeName,"normal",false}
{}
