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
#include <sharedutils/datastream.h>
#include <sharedutils/util_pragma.hpp>
#include <OpenImageIO/ustring.h>

#pragma optimize("",off)
static raytracing::NumberSocket get_channel_socket(raytracing::CCLShader &shader,const raytracing::Socket &colorNode,raytracing::Channel channel)
{
	// We only need one channel value, so we'll just grab the red channel
	auto nodeMetalnessRGB = shader.AddSeparateRGBNode(colorNode);
	switch(channel)
	{
	case raytracing::Channel::Red:
		return nodeMetalnessRGB.outR;
	case raytracing::Channel::Green:
		return nodeMetalnessRGB.outG;
	case raytracing::Channel::Blue:
		return nodeMetalnessRGB.outB;
	default:
		throw std::logic_error{"Invalid channel " +std::to_string(umath::to_integral(channel))};
	}
}

///////////////////

template<typename T>
	static void serialize_optional(DataStream &outDs,const std::optional<T> &opt)
{
	outDs->Write<bool>(opt.has_value());
	if(opt.has_value())
	{
		if constexpr(std::is_same_v<T,std::string>)
			outDs->WriteString(*opt);
		else
			outDs->Write<T>(*opt);
	}
}

template<typename T>
	static void deserialize_optional(DataStream &inDs,std::optional<T> &opt)
{
	auto b = inDs->Read<bool>();
	if(b == false)
	{
		opt = {};
		return;
	}
	if constexpr(std::is_same_v<T,std::string>)
		opt = inDs->ReadString();
	else
		opt = inDs->Read<T>();
}

static void serialize_path(DataStream &outDs,const std::optional<std::string> &opt)
{
	serialize_optional(outDs,opt.has_value() ? raytracing::Scene::ToRelativePath(*opt) : std::optional<std::string>{});
}

static void deserialize_path(DataStream &inDs,std::optional<std::string> &opt)
{
	deserialize_optional(inDs,opt);
	if(opt.has_value())
		opt = raytracing::Scene::ToAbsolutePath(*opt);
}

raytracing::Shader::Shader(Scene &scene,const std::string &name)
	: SceneObject{scene},m_scene{scene},m_name{name}
{}

util::WeakHandle<raytracing::Shader> raytracing::Shader::GetHandle()
{
	return util::WeakHandle<raytracing::Shader>{shared_from_this()};
}

raytracing::Scene &raytracing::Shader::GetScene() const {return m_scene;}
const std::string &raytracing::Shader::GetName() const {return m_name;}
const std::string &raytracing::Shader::GetMeshName() const {return m_meshName;}
void raytracing::Shader::SetMeshName(const std::string &meshName) {m_meshName = meshName;}
bool raytracing::Shader::HasFlag(Flags flags) const {return umath::is_flag_set(m_flags,flags);}
void raytracing::Shader::SetFlags(Flags flags,bool enabled) {umath::set_flag(m_flags,flags,enabled);}
raytracing::Shader::Flags raytracing::Shader::GetFlags() const {return m_flags;}
void raytracing::Shader::SetAlphaMode(AlphaMode alphaMode,float alphaCutoff)
{
	m_alphaMode = alphaMode;
	m_alphaCutoff = alphaCutoff;
}
AlphaMode raytracing::Shader::GetAlphaMode() const {return m_alphaMode;}
float raytracing::Shader::GetAlphaCutoff() const {return m_alphaCutoff;}
void raytracing::Shader::SetAlphaFactor(float factor) {m_alphaFactor = factor;}
float raytracing::Shader::GetAlphaFactor() const {return m_alphaFactor;}
void raytracing::Shader::SetUVHandler(TextureType type,const std::shared_ptr<UVHandler> &uvHandler) {m_uvHandlers.at(umath::to_integral(type)) = uvHandler;}
const std::shared_ptr<raytracing::UVHandler> &raytracing::Shader::GetUVHandler(TextureType type) const {return m_uvHandlers.at(umath::to_integral(type));}
void raytracing::Shader::SetUVHandlers(const std::array<std::shared_ptr<UVHandler>,umath::to_integral(TextureType::Count)> &handlers) {m_uvHandlers = handlers;}
const std::array<std::shared_ptr<raytracing::UVHandler>,umath::to_integral(raytracing::Shader::TextureType::Count)> &raytracing::Shader::GetUVHandlers() const {return m_uvHandlers;}

enum class ShaderType : uint32_t
{
	Generic = 0,
	Albedo,
	ColorTest,
	Normal,
	Depth,
	Toon,
	Glass,
	PBR,
	Particle
};
void raytracing::Shader::Serialize(DataStream &dsOut) const
{
	ShaderType type;
	if(typeid(*this) == typeid(ShaderGeneric))
		type = ShaderType::Generic;
	else if(typeid(*this) == typeid(ShaderAlbedo))
		type = ShaderType::Albedo;
	else if(typeid(*this) == typeid(ShaderColorTest))
		type = ShaderType::ColorTest;
	else if(typeid(*this) == typeid(ShaderNormal))
		type = ShaderType::Normal;
	else if(typeid(*this) == typeid(ShaderDepth))
		type = ShaderType::Depth;
	else if(typeid(*this) == typeid(ShaderToon))
		type = ShaderType::Toon;
	else if(typeid(*this) == typeid(ShaderGlass))
		type = ShaderType::Glass;
	else if(typeid(*this) == typeid(ShaderPBR))
		type = ShaderType::PBR;
	else if(typeid(*this) == typeid(ShaderParticle))
		type = ShaderType::Particle;
	else
		throw std::runtime_error{"Invalid shader type"};
	dsOut->Write(type);
	dsOut->WriteString(m_name);
	dsOut->WriteString(m_meshName);
	dsOut->Write(m_flags);
	dsOut->Write(m_alphaMode);
	dsOut->Write(m_alphaCutoff);
	dsOut->Write(m_alphaFactor);

	for(auto &uvHandler : m_uvHandlers)
	{
		if(uvHandler == nullptr)
		{
			dsOut->Write<bool>(false);
			continue;
		}
		dsOut->Write<bool>(true);
		uvHandler->Serialize(dsOut);
	}
	DoSerialize(dsOut);
}
raytracing::PShader raytracing::Shader::Create(Scene &scene,DataStream &dsIn,uint32_t version)
{
	auto type = dsIn->Read<ShaderType>();
	auto name = dsIn->ReadString();
	PShader shader = nullptr;
	switch(type)
	{
	case ShaderType::Generic:
		shader = std::shared_ptr<ShaderGeneric>{new ShaderGeneric{scene,name}};
		break;
	case ShaderType::Albedo:
		shader = std::shared_ptr<ShaderAlbedo>{new ShaderAlbedo{scene,name}};
		break;
	case ShaderType::ColorTest:
		shader = std::shared_ptr<ShaderColorTest>{new ShaderColorTest{scene,name}};
		break;
	case ShaderType::Normal:
		shader = std::shared_ptr<ShaderNormal>{new ShaderNormal{scene,name}};
		break;
	case ShaderType::Depth:
		shader = std::shared_ptr<ShaderDepth>{new ShaderDepth{scene,name}};
		break;
	case ShaderType::Toon:
		shader = std::shared_ptr<ShaderToon>{new ShaderToon{scene,name}};
		break;
	case ShaderType::Glass:
		shader = std::shared_ptr<ShaderGlass>{new ShaderGlass{scene,name}};
		break;
	case ShaderType::PBR:
		shader = std::shared_ptr<ShaderPBR>{new ShaderPBR{scene,name}};
		break;
	case ShaderType::Particle:
		shader = std::shared_ptr<ShaderParticle>{new ShaderParticle{scene,name}};
		break;
	default:
		throw std::runtime_error{"Invalid shader type"};
	}
	shader->Deserialize(dsIn,version);
	scene.m_shaders.push_back(shader);
	return shader;
}
void raytracing::Shader::Deserialize(DataStream &dsIn,uint32_t version)
{
	m_meshName = dsIn->ReadString();
	m_flags = dsIn->Read<decltype(m_flags)>();
	m_alphaMode = dsIn->Read<decltype(m_alphaMode)>();
	m_alphaCutoff = dsIn->Read<decltype(m_alphaCutoff)>();
	if(version >= 2)
		m_alphaFactor = dsIn->Read<decltype(m_alphaFactor)>();

	for(auto &uvHandler : m_uvHandlers)
	{
		auto isValid = dsIn->Read<bool>();
		if(isValid == false)
		{
			uvHandler = nullptr;
			continue;
		}
		uvHandler = UVHandler::Create(dsIn);
	}

	DoDeserialize(dsIn,version);
}

std::shared_ptr<raytracing::CCLShader> raytracing::Shader::GenerateCCLShader()
{
	auto cclShader = CCLShader::Create(*this);
	return SetupCCLShader(*cclShader) ? cclShader : nullptr;
}
std::shared_ptr<raytracing::CCLShader> raytracing::Shader::GenerateCCLShader(ccl::Shader &shader)
{
	auto cclShader = CCLShader::Create(*this,shader);
	return SetupCCLShader(*cclShader) ? cclShader : nullptr;
}
bool raytracing::Shader::SetupCCLShader(CCLShader &cclShader)
{
	for(auto i=decltype(m_uvHandlers.size()){0u};i<m_uvHandlers.size();++i)
	{
		auto &uvHandler = m_uvHandlers.at(i);
		if(uvHandler == nullptr)
			continue;
		cclShader.m_uvSockets.at(i) = uvHandler->InitializeNodes(cclShader);
	}
	if(InitializeCCLShader(cclShader) == false)
		return false;
	return true;
}

void raytracing::Shader::DoFinalize() {}

////////////////

void raytracing::ShaderPBR::SetMetallic(float metallic) {m_metallic = metallic;}
void raytracing::ShaderPBR::SetSpecular(float specular) {m_specular = specular;}
void raytracing::ShaderPBR::SetSpecularTint(float specularTint) {m_specularTint = specularTint;}
void raytracing::ShaderPBR::SetAnisotropic(float anisotropic) {m_anisotropic = anisotropic;}
void raytracing::ShaderPBR::SetAnisotropicRotation(float anisotropicRotation) {m_anisotropicRotation = anisotropicRotation;}
void raytracing::ShaderPBR::SetSheen(float sheen) {m_sheen = sheen;}
void raytracing::ShaderPBR::SetSheenTint(float sheenTint) {m_sheenTint = sheenTint;}
void raytracing::ShaderPBR::SetClearcoat(float clearcoat) {m_clearcoat = clearcoat;}
void raytracing::ShaderPBR::SetClearcoatRoughness(float clearcoatRoughness) {m_clearcoatRoughness = clearcoatRoughness;}
void raytracing::ShaderPBR::SetTransmission(float transmission) {m_transmission = transmission;}
void raytracing::ShaderPBR::SetTransmissionRoughness(float transmissionRoughness) {m_transmissionRoughness = transmissionRoughness;}

void raytracing::ShaderPBR::SetSubsurface(float subsurface) {m_subsurface = subsurface;}
void raytracing::ShaderPBR::SetSubsurfaceColorFactor(const Vector3 &color) {m_subsurfaceColorFactor = color;}
void raytracing::ShaderPBR::SetSubsurfaceMethod(PrincipledBSDFNode::SubsurfaceMethod method) {m_subsurfaceMethod = method;}
void raytracing::ShaderPBR::SetSubsurfaceRadius(const Vector3 &radius) {m_subsurfaceRadius = radius;}

////////////////

void raytracing::ShaderModuleSpriteSheet::SetSpriteSheetData(
	const Vector2 &uv0Min,const Vector2 &uv0Max,
	const std::string &albedoMap2,const Vector2 &uv1Min,const Vector2 &uv1Max,
	float interpFactor
)
{
	auto spriteSheetData = SpriteSheetData{};
	spriteSheetData.uv0 = {uv0Min,uv0Max};
	spriteSheetData.uv1 = {uv1Min,uv1Max};
	spriteSheetData.albedoMap2 = albedoMap2;
	spriteSheetData.interpFactor = interpFactor;
	SetSpriteSheetData(spriteSheetData);
}
void raytracing::ShaderModuleSpriteSheet::SetSpriteSheetData(const SpriteSheetData &spriteSheetData) {m_spriteSheetData = spriteSheetData;}
const std::optional<raytracing::ShaderModuleSpriteSheet::SpriteSheetData> &raytracing::ShaderModuleSpriteSheet::GetSpriteSheetData() const {return m_spriteSheetData;}

void raytracing::ShaderModuleSpriteSheet::Serialize(DataStream &dsOut) const
{
	serialize_optional(dsOut,m_spriteSheetData);
}
void raytracing::ShaderModuleSpriteSheet::Deserialize(DataStream &dsIn)
{
	deserialize_optional(dsIn,m_spriteSheetData);
}

////////////////

void raytracing::ShaderAlbedoSet::SetAlbedoMap(const std::string &albedoMap) {m_albedoMap = albedoMap;}
const std::optional<std::string> &raytracing::ShaderAlbedoSet::GetAlbedoMap() const {return m_albedoMap;}
void raytracing::ShaderAlbedoSet::SetColorFactor(const Vector4 &colorFactor) {m_colorFactor = colorFactor;}
const Vector4 &raytracing::ShaderAlbedoSet::GetColorFactor() const {return m_colorFactor;}
const std::optional<raytracing::ImageTextureNode> &raytracing::ShaderAlbedoSet::GetAlbedoNode() const {return m_albedoNode;}
std::optional<raytracing::ImageTextureNode> raytracing::ShaderAlbedoSet::AddAlbedoMap(ShaderModuleAlbedo &albedoModule,CCLShader &shader)
{
	if(m_albedoNode.has_value())
		return m_albedoNode;
	if(m_albedoMap.has_value() == false)
		return {};
	auto *modSpriteSheet = dynamic_cast<ShaderModuleSpriteSheet*>(&albedoModule);
	auto uvSocket = shader.GetUVSocket(Shader::TextureType::Albedo,modSpriteSheet);
	auto nodeAlbedo = shader.AddColorImageTextureNode(*m_albedoMap,uvSocket);
	if(modSpriteSheet && modSpriteSheet->GetSpriteSheetData().has_value())
	{
		auto &spriteSheetData = *modSpriteSheet->GetSpriteSheetData();
		auto uvSocket2 = shader.GetUVSocket(Shader::TextureType::Albedo,modSpriteSheet,SpriteSheetFrame::Second);
		auto nodeAlbedo2 = shader.AddColorImageTextureNode(spriteSheetData.albedoMap2,uvSocket2);
		nodeAlbedo.outColor = shader.AddMixNode(nodeAlbedo.outColor,nodeAlbedo2.outColor,raytracing::MixNode::Type::Mix,spriteSheetData.interpFactor);
		nodeAlbedo.outAlpha = nodeAlbedo.outAlpha.lerp(nodeAlbedo2.outAlpha,spriteSheetData.interpFactor);
	}
	if(m_colorFactor != Vector4{1.f,1.f,1.f,1.f})
	{
		auto rgb = shader.AddSeparateRGBNode(nodeAlbedo.outColor);
		rgb.outR = rgb.outR *m_colorFactor.r;
		rgb.outG = rgb.outG *m_colorFactor.g;
		rgb.outB = rgb.outB *m_colorFactor.b;
		nodeAlbedo.outColor = shader.AddCombineRGBNode(rgb.outR,rgb.outG,rgb.outB);
		nodeAlbedo.outAlpha = nodeAlbedo.outAlpha *m_colorFactor.a;
	}

	m_albedoNode = nodeAlbedo;
	return nodeAlbedo;
}
void raytracing::ShaderAlbedoSet::Serialize(DataStream &dsOut) const
{
	serialize_path(dsOut,m_albedoMap);
	dsOut->Write(m_colorFactor);
}
void raytracing::ShaderAlbedoSet::Deserialize(DataStream &dsIn)
{
	deserialize_path(dsIn,m_albedoMap);
	m_colorFactor = dsIn->Read<decltype(m_colorFactor)>();
}

////////////////

void raytracing::ShaderModuleAlbedo::SetEmissionFromAlbedoAlpha(Shader &shader,bool b)
{
	shader.SetFlags(Shader::Flags::EmissionFromAlbedoAlpha,b);
}
const raytracing::ShaderAlbedoSet &raytracing::ShaderModuleAlbedo::GetAlbedoSet() const {return const_cast<ShaderModuleAlbedo*>(this)->GetAlbedoSet();}
raytracing::ShaderAlbedoSet &raytracing::ShaderModuleAlbedo::GetAlbedoSet() {return m_albedoSet;}

const raytracing::ShaderAlbedoSet &raytracing::ShaderModuleAlbedo::GetAlbedoSet2() const {return const_cast<ShaderModuleAlbedo*>(this)->GetAlbedoSet2();}
raytracing::ShaderAlbedoSet &raytracing::ShaderModuleAlbedo::GetAlbedoSet2() {return m_albedoSet2;}

void raytracing::ShaderModuleAlbedo::SetUseVertexAlphasForBlending(bool useAlphasForBlending) {m_useVertexAlphasForBlending = useAlphasForBlending;}
bool raytracing::ShaderModuleAlbedo::ShouldUseVertexAlphasForBlending() const {return m_useVertexAlphasForBlending;}

bool raytracing::ShaderModuleAlbedo::SetupAlbedoNodes(CCLShader &shader,Socket &outColor,NumberSocket &outAlpha)
{
	auto &albedoNode = m_albedoSet.GetAlbedoNode();
	if(albedoNode.has_value() == false)
		return false;
	outColor = albedoNode->outColor;
	outAlpha = albedoNode->outAlpha;
	if(ShouldUseVertexAlphasForBlending())
	{
		auto albedoNode2 = m_albedoSet2.AddAlbedoMap(*this,shader);
		if(albedoNode2.has_value())
		{
			auto alpha = shader.AddVertexAlphaNode();
			// Color blend
			auto colorMix = shader.AddMixNode(albedoNode->outColor,albedoNode2->outColor,MixNode::Type::Mix,alpha);
			// Alpha transparency blend
			auto alphaMix = albedoNode->outAlpha +(albedoNode2->outAlpha -albedoNode->outAlpha) *alpha;
			outColor = colorMix;
			outAlpha = alphaMix;
		}
	}
	// Wrinkle maps
	if(m_wrinkleCompressMap.has_value() && m_wrinkleStretchMap.has_value())
	{
		// Vertex alphas and wrinkle maps can not be used both at the same time
		assert(!ShouldUseVertexAlphasForBlending());

		auto nodeWrinkleCompress = shader.AddColorImageTextureNode(*m_wrinkleCompressMap,shader.GetUVSocket(Shader::TextureType::Albedo));
		auto nodeWrinkleStretch = shader.AddColorImageTextureNode(*m_wrinkleStretchMap,shader.GetUVSocket(Shader::TextureType::Albedo));
		auto wrinkleValue = shader.AddWrinkleFactorNode();
		auto compressFactor = -wrinkleValue;
		compressFactor = compressFactor.clamp(0.f,1.f);
		auto stretchFactor = wrinkleValue.clamp(0.f,1.f);
		auto baseFactor = 1.f -compressFactor -stretchFactor;

		auto baseRgb = shader.AddSeparateRGBNode(outColor);
		auto compressRgb = shader.AddSeparateRGBNode(nodeWrinkleCompress);
		auto stretchRgb = shader.AddSeparateRGBNode(nodeWrinkleStretch);
		auto r = baseRgb.outR *baseFactor +compressRgb.outR *compressFactor +stretchRgb.outR *stretchFactor;
		auto g = baseRgb.outG *baseFactor +compressRgb.outG *compressFactor +stretchRgb.outG *stretchFactor;
		auto b = baseRgb.outB *baseFactor +compressRgb.outB *compressFactor +stretchRgb.outB *stretchFactor;
		outColor = shader.AddCombineRGBNode(r,g,b).outColor;
	}
	return true;
}
void raytracing::ShaderModuleAlbedo::SetWrinkleStretchMap(const std::string &wrinkleStretchMap) {m_wrinkleStretchMap = wrinkleStretchMap;}
void raytracing::ShaderModuleAlbedo::SetWrinkleCompressMap(const std::string &wrinkleCompressMap) {m_wrinkleCompressMap = wrinkleCompressMap;}
bool raytracing::ShaderModuleAlbedo::GetAlbedoColorNode(const Shader &shader,CCLShader &cclShader,Socket &outColor,NumberSocket *optOutAlpha)
{
	Socket albedoColor;
	NumberSocket albedoAlpha;
	if(SetupAlbedoNodes(cclShader,albedoColor,albedoAlpha) == false)
		return false;
	InitializeAlbedoColor(albedoColor);
	outColor = albedoColor;
	if(optOutAlpha)
		*optOutAlpha = albedoAlpha *shader.GetAlphaFactor();
	return true;
}
void raytracing::ShaderModuleAlbedo::LinkAlbedo(const Shader &shader,const Socket &color,const NumberSocket *optAlpha,bool useAlphaIfFlagSet,NumberSocket *optOutAlpha)
{
	Socket albedoColor;
	NumberSocket albedoAlpha;
	auto &cclShader = color.GetShader();
	if(GetAlbedoColorNode(shader,cclShader,albedoColor,&albedoAlpha) == false)
		return;
	cclShader.Link(albedoColor,color);
	if((optAlpha || optOutAlpha) && useAlphaIfFlagSet && cclShader.GetShader().GetAlphaMode() != AlphaMode::Opaque)
	{
		albedoAlpha = cclShader.ApplyAlphaMode(albedoAlpha,cclShader.GetShader().GetAlphaMode(),cclShader.GetShader().GetAlphaCutoff());
		InitializeAlbedoAlpha(albedoColor,albedoAlpha);
		if(optOutAlpha)
			*optOutAlpha = albedoAlpha;
		if(optAlpha)
			cclShader.Link(albedoAlpha,*optAlpha);
	}
}
void raytracing::ShaderModuleAlbedo::LinkAlbedoToBSDF(const Shader &shader,const Socket &bsdf)
{
	Socket albedoColor;
	NumberSocket albedoAlpha;
	auto &cclShader = bsdf.GetShader();
	if(SetupAlbedoNodes(cclShader,albedoColor,albedoAlpha) == false)
		return;
	InitializeAlbedoColor(albedoColor);
	auto alphaMode = cclShader.GetShader().GetAlphaMode();
	if(alphaMode != AlphaMode::Opaque)
	{
		InitializeAlbedoAlpha(albedoColor,albedoAlpha);
		albedoAlpha = albedoAlpha *shader.GetAlphaFactor();
		auto nodeTransparentBsdf = cclShader.AddTransparencyClosure(albedoColor,albedoAlpha,alphaMode,cclShader.GetShader().GetAlphaCutoff());
		cclShader.Link(nodeTransparentBsdf,bsdf);
	}
	else
		cclShader.Link(albedoColor,bsdf);
}

void raytracing::ShaderModuleAlbedo::Serialize(DataStream &dsOut) const
{
	m_albedoSet.Serialize(dsOut);
	m_albedoSet2.Serialize(dsOut);
	serialize_path(dsOut,m_wrinkleStretchMap);
	serialize_path(dsOut,m_wrinkleCompressMap);
	dsOut->Write<bool>(m_useVertexAlphasForBlending);
}
void raytracing::ShaderModuleAlbedo::Deserialize(DataStream &dsIn)
{
	m_albedoSet.Deserialize(dsIn);
	m_albedoSet2.Deserialize(dsIn);

	deserialize_path(dsIn,m_wrinkleStretchMap);
	deserialize_path(dsIn,m_wrinkleCompressMap);

	m_useVertexAlphasForBlending = dsIn->Read<bool>();
}

void raytracing::ShaderModuleAlbedo::InitializeAlbedoColor(Socket &inOutColor) {}
void raytracing::ShaderModuleAlbedo::InitializeAlbedoAlpha(const Socket &inAlbedoColor,NumberSocket &inOutAlpha)
{
	auto &shader = inAlbedoColor.GetShader();
	if(umath::is_flag_set(shader.GetShader().GetFlags(),Shader::Flags::AdditiveByColor))
	{
		auto rgb = shader.AddSeparateRGBNode(inAlbedoColor);
		inOutAlpha = rgb.outR.max(rgb.outG.max(rgb.outB)); // HSV value
	}
}

////////////////

bool raytracing::ShaderGlass::InitializeCCLShader(CCLShader &cclShader)
{
	auto glassBsdf = cclShader.AddGlassBSDFNode();
	// Default settings (Taken from Blender)
	glassBsdf.SetIOR(GetIOR());
	glassBsdf.SetDistribution(GlassBSDFNode::Distribution::Beckmann);

	// Roughness map
	if(AddRoughnessMap(cclShader))
		LinkRoughness(glassBsdf.inRoughness);

	// See https://docs.blender.org/manual/en/dev/render/cycles/optimizations/reducing_noise.html#render-cycles-reducing-noise-glass-and-transp-shadows
	auto lightPathNode = cclShader.AddLightPathNode();

	auto min = cclShader.AddMathNode(lightPathNode.outIsShadowRay,lightPathNode.outIsReflectionRay,ccl::NodeMathType::NODE_MATH_MINIMUM);
	auto translucent = cclShader.AddTranslucentBSDFNode();

	// Albedo map
	auto alpha = cclShader.AddConstantNode(1.f);
	/*if(GetAlbedoSet().AddAlbedoMap(*this,cclShader).has_value())
	{
		LinkAlbedo(*this,glassBsdf.inColor,nullptr,true,&alpha);
		LinkAlbedo(*this,translucent.inColor);
	}

	// Normal map
	if(AddNormalMap(cclShader))
	{
		LinkNormal(glassBsdf.inNormal);
		LinkNormal(translucent.inNormal);
	}*/

	auto mix = cclShader.AddMixClosureNode();
	cclShader.Link(min,mix.inFac);
	cclShader.Link(glassBsdf,mix.inClosure1);
	cclShader.Link(translucent,mix.inClosure2);

	auto transparent = cclShader.AddTransparentBSDFNode();
	auto finalMix = cclShader.AddMixClosureNode();
	cclShader.Link(alpha,finalMix.inFac);
	cclShader.Link(transparent,finalMix.inClosure1);
	cclShader.Link(mix,finalMix.inClosure2);

	//cclShader.Link(finalMix,cclShader.GetOutputNode().inSurface);
	cclShader.Link(mix,cclShader.GetOutputNode().inSurface);
	return true;
}

void raytracing::ShaderGlass::DoSerialize(DataStream &dsIn) const
{
	ShaderModuleNormal::Serialize(dsIn);
	ShaderModuleRoughness::Serialize(dsIn);
	ShaderModuleSpriteSheet::Serialize(dsIn);
	ShaderModuleIOR::Serialize(dsIn);
}
void raytracing::ShaderGlass::DoDeserialize(DataStream &dsIn,uint32_t version)
{
	ShaderModuleNormal::Deserialize(dsIn);
	ShaderModuleRoughness::Deserialize(dsIn);
	ShaderModuleSpriteSheet::Deserialize(dsIn);
	ShaderModuleIOR::Deserialize(dsIn,version);
}

////////////////

bool raytracing::ShaderGeneric::InitializeCCLShader(CCLShader &cclShader) {return true;}

void raytracing::ShaderGeneric::DoSerialize(DataStream &dsIn) const {}
void raytracing::ShaderGeneric::DoDeserialize(DataStream &dsIn,uint32_t version) {}

////////////////

bool raytracing::ShaderAlbedo::InitializeCCLShader(CCLShader &cclShader)
{
	// Albedo map
	if(GetAlbedoSet().AddAlbedoMap(*this,cclShader).has_value())
		LinkAlbedoToBSDF(*this,cclShader.GetOutputNode().inSurface);
	return true;
}
void raytracing::ShaderAlbedo::DoSerialize(DataStream &dsIn) const
{
	ShaderModuleAlbedo::Serialize(dsIn);
	ShaderModuleSpriteSheet::Serialize(dsIn);
}
void raytracing::ShaderAlbedo::DoDeserialize(DataStream &dsIn,uint32_t version)
{
	ShaderModuleAlbedo::Deserialize(dsIn);
	ShaderModuleSpriteSheet::Deserialize(dsIn);
}

////////////////

bool raytracing::ShaderColorTest::InitializeCCLShader(CCLShader &cclShader)
{
	auto colorNode = cclShader.AddColorNode();
	colorNode.SetColor({0.f,1.f,0.f});
	cclShader.Link(colorNode,cclShader.GetOutputNode().inSurface);
	return true;
}
void raytracing::ShaderColorTest::DoSerialize(DataStream &dsIn) const {}
void raytracing::ShaderColorTest::DoDeserialize(DataStream &dsIn,uint32_t version) {}

////////////////

bool raytracing::ShaderVolumeScatter::InitializeCCLShader(CCLShader &cclShader)
{
	auto volumeScatterNode = cclShader.AddScatterVolumeNode();
	//volumeScatterNode.inDensity = 0.0001f;
	volumeScatterNode.inVolumeMixWeight = 0.f;
	volumeScatterNode.inAnisotropy = 0.f;
	cclShader.Link(cclShader.AddConstantNode(0.01f),volumeScatterNode.inDensity);
	//cclShader.Link(cclShader.AddCombineRGBNode(1.f,0.f,0.f),volumeScatterNode.inColor);
	cclShader.Link(volumeScatterNode,cclShader.GetOutputNode().inVolume);
	return true;
}
void raytracing::ShaderVolumeScatter::DoSerialize(DataStream &dsIn) const {}
void raytracing::ShaderVolumeScatter::DoDeserialize(DataStream &dsIn,uint32_t version) {}

////////////////

bool raytracing::ShaderNormal::InitializeCCLShader(CCLShader &cclShader)
{
	// Normal map
	if(AddNormalMap(cclShader).has_value())
		LinkNormalToBSDF(cclShader.GetOutputNode().inSurface);
	return true;
}
void raytracing::ShaderNormal::DoSerialize(DataStream &dsIn) const
{
	ShaderModuleNormal::Serialize(dsIn);
	ShaderModuleSpriteSheet::Serialize(dsIn);
}
void raytracing::ShaderNormal::DoDeserialize(DataStream &dsIn,uint32_t version)
{
	ShaderModuleNormal::Deserialize(dsIn);
	ShaderModuleSpriteSheet::Deserialize(dsIn);
}

////////////////

bool raytracing::ShaderDepth::InitializeCCLShader(CCLShader &cclShader)
{
	auto camNode = cclShader.AddCameraDataNode();
	auto d = camNode.outViewZDepth; // Subtracting near plane apparently not required?
	d = d /m_farZ;
	auto rgb = cclShader.AddCombineRGBNode(d,d,d);
	// TODO: Take transparency of albedo map into account?
	cclShader.Link(rgb,cclShader.GetOutputNode().inSurface);
	return true;
}
void raytracing::ShaderDepth::SetFarZ(float farZ) {m_farZ = farZ;}
void raytracing::ShaderDepth::DoSerialize(DataStream &dsIn) const
{
	ShaderModuleAlbedo::Serialize(dsIn);
	ShaderModuleSpriteSheet::Serialize(dsIn);
	dsIn->Write(m_farZ);
}
void raytracing::ShaderDepth::DoDeserialize(DataStream &dsIn,uint32_t version)
{
	ShaderModuleAlbedo::Deserialize(dsIn);
	ShaderModuleSpriteSheet::Deserialize(dsIn);
	m_farZ = dsIn->Read<decltype(m_farZ)>();
}

////////////////

bool raytracing::ShaderToon::InitializeCCLShader(CCLShader &cclShader)
{
	auto e0 = cclShader.AddEmissionNode();
	e0.SetColor(Vector3{1.f,1.f,1.f});
	e0.SetStrength(0.f);

	auto td = cclShader.AddToonBSDFNode();
	td.SetComponent(raytracing::ToonBSDFNode::Component::Diffuse);
	td.SetColor({1.0,0.637597,0.473532});
	td.SetSize(0.9);
	td.SetSmooth(0.0);

	cclShader.Link(cclShader.AddConstantNode(0.9),td.inSize);
	cclShader.Link(cclShader.AddConstantNode(0.0),td.inSmooth);
	cclShader.Link(cclShader.AddCombineRGBNode(1.0,0.637597,0.473532),td.inColor);

	auto tg = cclShader.AddToonBSDFNode();
	tg.SetComponent(raytracing::ToonBSDFNode::Component::Glossy);
	tg.SetColor({0.099899,0.099899,0.099899});
	tg.SetSize(0.2);
	tg.SetSmooth(0.0);

	auto e1 = cclShader.AddEmissionNode();
	e1.SetColor({0.701102,0.318547,0.212231});
	e1.SetStrength(1.0);

	auto a0 = cclShader.AddAddClosureNode();
	cclShader.Link(td,a0.inClosure1);
	cclShader.Link(tg,a0.inClosure2);

	auto a1 = cclShader.AddAddClosureNode();
	cclShader.Link(a0,a1.inClosure1);
	cclShader.Link(e1,a1.inClosure2);

	auto lp = cclShader.AddLightPathNode();

	auto m = cclShader.AddMixClosureNode();
	cclShader.Link(a1,m.inClosure1);
	cclShader.Link(e0,m.inClosure2);
	cclShader.Link(lp.outIsDiffuseRay,m.inFac);

	//cclShader.Link(m,cclShader.GetOutputNode().inSurface);

	cclShader.Link(td,cclShader.GetOutputNode().inSurface);
	return true;
#if 0
	// See https://www.blendswap.com/blend/20178
	auto diffEmission = cclShader.AddEmissionNode();
	diffEmission.SetStrength(0.f);

	auto diffuseToon = cclShader.AddToonBSDFNode();
	diffuseToon.SetComponent(raytracing::ToonBSDFNode::Component::Diffuse);
	diffuseToon.SetSize(m_diffuseSize);
	diffuseToon.SetSmooth(m_diffuseSmooth);

	// Albedo map
	if(GetAlbedoSet().AddAlbedoMap(*this,cclShader).has_value())
		LinkAlbedoToBSDF(*this,diffuseToon.inColor);

	auto specularToon = cclShader.AddToonBSDFNode();
	specularToon.SetComponent(raytracing::ToonBSDFNode::Component::Glossy);
	specularToon.SetSize(m_specularSize);
	specularToon.SetSmooth(m_specularSmooth);
	specularToon.SetColor(m_specularColor);

	// Roughness map
	if(AddRoughnessMap(cclShader).has_value())
	{
		auto roughnessRgb = cclShader.AddSeparateRGBNode();
		LinkRoughness(roughnessRgb.inColor);

		auto specular = cclShader.AddCombineRGBNode(1.f -roughnessRgb.outR,1.f -roughnessRgb.outG,1.f -roughnessRgb.outB);
		cclShader.Link(specular,specularToon.inColor);
	}

	// Normal map
	if(AddNormalMap(cclShader))
	{
		LinkNormal(diffuseToon.inNormal);
		LinkNormal(specularToon.inNormal);
	}

	auto emission = cclShader.AddEmissionNode();
	emission.SetColor(m_shadeColor);

	auto diffuseSpecular = cclShader.AddAddClosureNode();
	cclShader.Link(diffuseToon,diffuseSpecular.inClosure1);
	cclShader.Link(specularToon,diffuseSpecular.inClosure2);

	auto diffSpecEmission = cclShader.AddAddClosureNode();
	cclShader.Link(diffuseSpecular,diffSpecEmission.inClosure1);
	cclShader.Link(emission,diffSpecEmission.inClosure2);
	
	auto lightPathNode = cclShader.AddLightPathNode();
	auto bsdf = cclShader.AddMixClosureNode();
	cclShader.Link(diffSpecEmission,bsdf.inClosure1);
	cclShader.Link(diffEmission,bsdf.inClosure2);
	cclShader.Link(lightPathNode.outIsDiffuseRay,bsdf.inFac);

	cclShader.Link(bsdf,cclShader.GetOutputNode().inSurface);
	return true;
#endif
}
void raytracing::ShaderToon::DoSerialize(DataStream &dsIn) const
{
	ShaderModuleNormal::Serialize(dsIn);
	ShaderModuleSpriteSheet::Serialize(dsIn);
	ShaderModuleRoughness::Serialize(dsIn);

	dsIn->Write<decltype(m_specularColor)>(m_specularColor);
	dsIn->Write<decltype(m_shadeColor)>(m_shadeColor);
	dsIn->Write<decltype(m_diffuseSize)>(m_diffuseSize);
	dsIn->Write<decltype(m_diffuseSmooth)>(m_diffuseSmooth);
	dsIn->Write<decltype(m_specularSize)>(m_specularSize);
	dsIn->Write<decltype(m_specularSmooth)>(m_specularSmooth);
}
void raytracing::ShaderToon::DoDeserialize(DataStream &dsIn,uint32_t version)
{
	ShaderModuleNormal::Deserialize(dsIn);
	ShaderModuleSpriteSheet::Deserialize(dsIn);
	if(version >= 2)
	{
		ShaderModuleRoughness::Deserialize(dsIn);
		m_specularColor = dsIn->Read<decltype(m_specularColor)>();
		m_shadeColor = dsIn->Read<decltype(m_shadeColor)>();
		m_diffuseSize = dsIn->Read<decltype(m_diffuseSize)>();
		m_diffuseSmooth = dsIn->Read<decltype(m_diffuseSmooth)>();
		m_specularSize = dsIn->Read<decltype(m_specularSize)>();
		m_specularSmooth = dsIn->Read<decltype(m_specularSmooth)>();
	}
}
void raytracing::ShaderToon::SetSpecularColor(const Vector3 &col) {m_specularColor = col;}
void raytracing::ShaderToon::SetShadeColor(const Vector3 &col) {m_shadeColor = col;}
void raytracing::ShaderToon::SetDiffuseSize(float size) {m_diffuseSize = size;}
void raytracing::ShaderToon::SetDiffuseSmooth(float smooth) {m_diffuseSmooth = smooth;}
void raytracing::ShaderToon::SetSpecularSize(float specSize) {m_specularSize = specSize;}
void raytracing::ShaderToon::SetSpecularSmooth(float smooth) {m_specularSmooth = smooth;}

////////////////

void raytracing::ShaderModuleNormal::SetNormalMap(const std::string &normalMap) {m_normalMap = normalMap;}
const std::optional<std::string> &raytracing::ShaderModuleNormal::GetNormalMap() const {return m_normalMap;}
void raytracing::ShaderModuleNormal::SetNormalMapSpace(NormalMapNode::Space space) {m_space = space;}
raytracing::NormalMapNode::Space raytracing::ShaderModuleNormal::GetNormalMapSpace() const {return m_space;}
std::optional<raytracing::Socket> raytracing::ShaderModuleNormal::AddNormalMap(CCLShader &shader)
{
	if(m_normalSocket.has_value())
		return m_normalSocket;
	if(m_normalMap.has_value()) // Use normal map
		m_normalSocket = shader.AddNormalMapImageTextureNode(*m_normalMap,shader.GetShader().GetMeshName(),shader.GetUVSocket(Shader::TextureType::Normal),GetNormalMapSpace());
	else // Use geometry normals
		m_normalSocket = shader.AddGeometryNode().outNormal;
	return m_normalSocket;
}
void raytracing::ShaderModuleNormal::LinkNormalToBSDF(const Socket &bsdf)
{
	if(m_normalSocket.has_value() == false)
		return;
	auto &shader = bsdf.GetShader();
	auto socketOutput = *m_normalSocket;
	auto alphaMode = shader.GetShader().GetAlphaMode();
	if(alphaMode != AlphaMode::Opaque)
	{
		auto nodeAlbedo = GetAlbedoSet().AddAlbedoMap(*this,shader);
		if(nodeAlbedo.has_value())
		{
			auto albedoAlpha = nodeAlbedo->outAlpha;
			if(ShouldUseVertexAlphasForBlending())
			{
				auto nodeAlbedo2 = GetAlbedoSet2().AddAlbedoMap(*this,shader);
				if(nodeAlbedo2.has_value())
				{
					auto alpha = shader.AddVertexAlphaNode();
					albedoAlpha = albedoAlpha +(nodeAlbedo2->outAlpha -albedoAlpha) *alpha;
				}
			}

			// Object uses translucency, which means we have to take the alpha of the albedo map into account.
			// Transparent normals don't make any sense, so we'll just always treat it as masked alpha
			// (with a default cutoff factor of 0.5).
			auto alphaCutoff = shader.GetShader().GetAlphaCutoff();
			socketOutput = shader.AddTransparencyClosure(socketOutput,albedoAlpha,AlphaMode::Mask,alphaCutoff).outClosure;
		}
	}
	shader.Link(socketOutput,bsdf);
}
void raytracing::ShaderModuleNormal::LinkNormal(const Socket &normal)
{
	if(m_normalSocket.has_value() == false)
		return;
	normal.GetShader().Link(*m_normalSocket,normal);
}
void raytracing::ShaderModuleNormal::Serialize(DataStream &dsOut) const
{
	ShaderModuleAlbedo::Serialize(dsOut);
	serialize_path(dsOut,m_normalMap);
	dsOut->Write(m_space);
}
void raytracing::ShaderModuleNormal::Deserialize(DataStream &dsIn)
{
	ShaderModuleAlbedo::Deserialize(dsIn);
	deserialize_path(dsIn,m_normalMap);
	m_space = dsIn->Read<decltype(m_space)>();
}

////////////////

void raytracing::ShaderModuleMetalness::SetMetalnessFactor(float metalnessFactor) {m_metalnessFactor = metalnessFactor;}
void raytracing::ShaderModuleMetalness::SetMetalnessMap(const std::string &metalnessMap,Channel channel)
{
	m_metalnessMap = metalnessMap;
	m_metalnessChannel = channel;
}
std::optional<raytracing::NumberSocket> raytracing::ShaderModuleMetalness::AddMetalnessMap(CCLShader &shader)
{
	if(m_metalnessSocket.has_value())
		return m_metalnessSocket;
	if(m_metalnessMap.has_value() == false)
	{
		// If no metalness map is available, just use metalness factor directly
		if(m_metalnessFactor.has_value())
		{
			m_metalnessSocket = shader.AddConstantNode(*m_metalnessFactor);
			return m_metalnessSocket;
		}
		return {};
	}
	auto nodeMetalness = shader.AddGradientImageTextureNode(*m_metalnessMap,shader.GetUVSocket(Shader::TextureType::Metalness));

	auto socketMetalness = get_channel_socket(shader,nodeMetalness.outColor,m_metalnessChannel);
	if(m_metalnessFactor.has_value())
	{
		// Material has a metalness factor, which we need to multiply with the value from the metalness texture
		socketMetalness = shader.AddMathNode(socketMetalness,*m_metalnessFactor,ccl::NodeMathType::NODE_MATH_MULTIPLY).outValue;
	}

	m_metalnessSocket = socketMetalness;
	return socketMetalness;
}
void raytracing::ShaderModuleMetalness::LinkMetalness(const NumberSocket &metalness)
{
	if(m_metalnessSocket.has_value() == false)
		return;
	metalness.GetShader().Link(*m_metalnessSocket,metalness);
}
void raytracing::ShaderModuleMetalness::Serialize(DataStream &dsOut) const
{
	serialize_path(dsOut,m_metalnessMap);
	serialize_optional(dsOut,m_metalnessFactor);
	dsOut->Write(m_metalnessChannel);
}
void raytracing::ShaderModuleMetalness::Deserialize(DataStream &dsIn)
{
	deserialize_path(dsIn,m_metalnessMap);
	deserialize_optional(dsIn,m_metalnessFactor);
	m_metalnessChannel = dsIn->Read<decltype(m_metalnessChannel)>();
}

////////////////

void raytracing::ShaderModuleRoughness::SetRoughnessFactor(float roughness) {m_roughnessFactor = roughness;}
void raytracing::ShaderModuleRoughness::SetRoughnessMap(const std::string &roughnessMap,Channel channel)
{
	m_roughnessMap = roughnessMap;
	m_roughnessChannel = channel;
}
void raytracing::ShaderModuleRoughness::SetSpecularMap(const std::string &specularMap,Channel channel)
{
	m_specularMap = specularMap;
	m_roughnessChannel = channel;
}
std::optional<raytracing::NumberSocket> raytracing::ShaderModuleRoughness::AddRoughnessMap(CCLShader &shader)
{
	if(m_roughnessSocket.has_value())
		return m_roughnessSocket;
	if(m_roughnessMap.has_value() == false && m_specularMap.has_value() == false)
	{
		// If no roughness map is available, just use roughness factor directly
		if(m_roughnessFactor.has_value())
		{
			m_roughnessSocket = shader.AddConstantNode(*m_roughnessFactor);
			return m_roughnessSocket;
		}
		return {};
	}
	std::string roughnessMap {};
	auto isSpecularMap = false;
	if(m_roughnessMap.has_value())
		roughnessMap = *m_roughnessMap;
	else
	{
		roughnessMap = *m_specularMap;
		isSpecularMap = true;
	}

	auto nodeImgRoughness = shader.AddGradientImageTextureNode(roughnessMap,shader.GetUVSocket(Shader::TextureType::Roughness));

	auto socketRoughness = get_channel_socket(shader,nodeImgRoughness.outColor,m_roughnessChannel);
	if(isSpecularMap)
	{
		// We also have to invert the specular value
		socketRoughness = 1.f -socketRoughness;
	}
	if(m_roughnessFactor.has_value())
	{
		// Material has a roughness factor, which we need to multiply with the value from the roughness texture
		socketRoughness = shader.AddMathNode(socketRoughness,*m_roughnessFactor,ccl::NodeMathType::NODE_MATH_MULTIPLY).outValue;
	}
	m_roughnessSocket = socketRoughness;
	return m_roughnessSocket;
}
void raytracing::ShaderModuleRoughness::LinkRoughness(const NumberSocket &roughness)
{
	if(m_roughnessSocket.has_value() == false)
		return;
	roughness.GetShader().Link(*m_roughnessSocket,roughness);
}
void raytracing::ShaderModuleRoughness::Serialize(DataStream &dsOut) const
{
	serialize_path(dsOut,m_roughnessMap);
	serialize_path(dsOut,m_specularMap);

	serialize_optional(dsOut,m_roughnessFactor);
	dsOut->Write(m_roughnessChannel);
}
void raytracing::ShaderModuleRoughness::Deserialize(DataStream &dsIn)
{
	deserialize_path(dsIn,m_roughnessMap);
	deserialize_path(dsIn,m_specularMap);

	deserialize_optional(dsIn,m_roughnessFactor);
	m_roughnessChannel = dsIn->Read<decltype(m_roughnessChannel)>();
}

////////////////

void raytracing::ShaderModuleEmission::SetEmissionMap(const std::string &emissionMap) {m_emissionMap = emissionMap;}
void raytracing::ShaderModuleEmission::SetEmissionFactor(const Vector3 &factor) {m_emissionFactor = factor;}
const Vector3 &raytracing::ShaderModuleEmission::GetEmissionFactor() const {return m_emissionFactor;}
void raytracing::ShaderModuleEmission::SetEmissionIntensity(float intensity) {m_emissionIntensity = intensity;}
float raytracing::ShaderModuleEmission::GetEmissionIntensity() const {return m_emissionIntensity;}
const std::optional<std::string> &raytracing::ShaderModuleEmission::GetEmissionMap() const {return m_emissionMap;}
void raytracing::ShaderModuleEmission::InitializeEmissionColor(Socket &inOutColor) {}
std::optional<raytracing::Socket> raytracing::ShaderModuleEmission::AddEmissionMap(CCLShader &shader)
{
	if(m_emissionSocket.has_value())
		return m_emissionSocket;
	auto emissionFactor = m_emissionFactor *m_emissionIntensity;
	if(m_emissionMap.has_value() == false || uvec::length_sqr(emissionFactor) == 0.0)
		return {};
	auto *modSpriteSheet = dynamic_cast<ShaderModuleSpriteSheet*>(this);
	auto nodeImgEmission = shader.AddColorImageTextureNode(*m_emissionMap,shader.GetUVSocket(Shader::TextureType::Emission,modSpriteSheet));
	if(modSpriteSheet && modSpriteSheet->GetSpriteSheetData().has_value())
	{
		auto &spriteSheetData = *modSpriteSheet->GetSpriteSheetData();
		auto uvSocket2 = shader.GetUVSocket(Shader::TextureType::Emission,modSpriteSheet,SpriteSheetFrame::Second);
		auto nodeAlbedo2 = shader.AddColorImageTextureNode(spriteSheetData.albedoMap2,uvSocket2);
		nodeImgEmission.outColor = shader.AddMixNode(nodeImgEmission.outColor,nodeAlbedo2.outColor,raytracing::MixNode::Type::Mix,spriteSheetData.interpFactor);
		nodeImgEmission.outAlpha = nodeImgEmission.outAlpha.lerp(nodeAlbedo2.outAlpha,spriteSheetData.interpFactor);
	}
	auto emissionColor = nodeImgEmission.outColor;
	InitializeEmissionColor(emissionColor);
	if(shader.GetShader().HasFlag(Shader::Flags::EmissionFromAlbedoAlpha))
	{
		// Glow intensity
		auto nodeGlowRGB = shader.AddCombineRGBNode();

		auto glowIntensity = emissionFactor;
		nodeGlowRGB.SetR(glowIntensity.r);
		nodeGlowRGB.SetG(glowIntensity.g);
		nodeGlowRGB.SetB(glowIntensity.b);

		// Multiply glow color with intensity
		auto nodeMixEmission = shader.AddMixNode(emissionColor,nodeGlowRGB,MixNode::Type::Multiply,1.f);

		// Grab alpha from glow map and create an RGB color from it
		auto nodeAlphaRGB = shader.AddCombineRGBNode(nodeImgEmission.outAlpha,nodeImgEmission.outAlpha,nodeImgEmission.outAlpha);

		// Multiply alpha with glow color
		m_emissionSocket = shader.AddMixNode(nodeMixEmission,nodeAlphaRGB,MixNode::Type::Multiply,1.f);
		return m_emissionSocket;
	}
	auto nodeEmissionRgb = shader.AddSeparateRGBNode(emissionColor);
	auto r = nodeEmissionRgb.outR *emissionFactor.r;
	auto g = nodeEmissionRgb.outG *emissionFactor.g;
	auto b = nodeEmissionRgb.outB *emissionFactor.b;
	m_emissionSocket = shader.AddCombineRGBNode(r,g,b);
	return m_emissionSocket;
	//m_emissionSocket = emissionColor;
	//return m_emissionSocket;
}
void raytracing::ShaderModuleEmission::LinkEmission(const Socket &emission)
{
	if(m_emissionSocket.has_value() == false)
		return;
	emission.GetShader().Link(*m_emissionSocket,emission);
}
void raytracing::ShaderModuleEmission::Serialize(DataStream &dsOut) const
{
	serialize_path(dsOut,m_emissionMap);
	dsOut->Write(m_emissionFactor);
	dsOut->Write(m_emissionIntensity);
}
void raytracing::ShaderModuleEmission::Deserialize(DataStream &dsIn)
{
	deserialize_path(dsIn,m_emissionMap);
	m_emissionFactor = dsIn->Read<decltype(m_emissionFactor)>();
	m_emissionIntensity = dsIn->Read<decltype(m_emissionIntensity)>();
}

////////////////

void raytracing::ShaderModuleIOR::SetIOR(float ior) {m_ior = ior;}
float raytracing::ShaderModuleIOR::GetIOR() const {return m_ior;}

void raytracing::ShaderModuleIOR::Serialize(DataStream &dsOut) const
{
	dsOut->Write(m_ior);
}
void raytracing::ShaderModuleIOR::Deserialize(DataStream &dsIn,uint32_t version)
{
	if(version < 2)
		return;
	m_ior = dsIn->Read<decltype(m_ior)>();
}

////////////////

void raytracing::ShaderParticle::SetRenderFlags(RenderFlags flags) {m_renderFlags = flags;}
void raytracing::ShaderParticle::SetColor(const Color &color) {m_color = color;}
const Color &raytracing::ShaderParticle::GetColor() const {return m_color;}
void raytracing::ShaderParticle::InitializeAlbedoColor(Socket &inOutColor)
{
	auto &shader = inOutColor.GetShader();

	auto mixColor = shader.AddMixNode(MixNode::Type::Multiply);
	shader.Link(inOutColor,mixColor.inColor1);
	mixColor.SetColor2(m_color.ToVector3());

	inOutColor = mixColor;
}
void raytracing::ShaderParticle::InitializeEmissionColor(Socket &inOutColor) {InitializeAlbedoColor(inOutColor);}
void raytracing::ShaderParticle::InitializeAlbedoAlpha(const Socket &inAlbedoColor,NumberSocket &inOutAlpha)
{
	ShaderPBR::InitializeAlbedoAlpha(inAlbedoColor,inOutAlpha);
	auto &shader = inOutAlpha.GetShader();
	auto mixAlpha = shader.AddMathNode();
	mixAlpha.SetType(ccl::NodeMathType::NODE_MATH_MULTIPLY);
	shader.Link(inOutAlpha,mixAlpha.inValue1);
	mixAlpha.SetValue2(m_color.a /255.f);

	inOutAlpha = mixAlpha.outValue;
}

bool raytracing::ShaderParticle::InitializeCCLShader(CCLShader &cclShader)
{
	// Note: We always need the albedo texture information for the translucency.
	// Whether metalness/roughness/etc. affect baking in any way is unclear (probably not),
	// but it also doesn't hurt to have them.
	auto albedoNode = GetAlbedoSet().AddAlbedoMap(*this,cclShader);
	if(albedoNode.has_value() == false)
		return false;

	auto transparentBsdf = cclShader.AddTransparentBSDFNode();
	auto diffuseBsdf = cclShader.AddDiffuseBSDFNode();

	auto mix = cclShader.AddMixClosureNode();
	cclShader.Link(transparentBsdf,mix.inClosure1);
	cclShader.Link(diffuseBsdf,mix.inClosure2);

	auto alphaHandled = InitializeTransparency(cclShader,*albedoNode,mix.inFac);
	LinkAlbedo(*this,diffuseBsdf.inColor,&mix.inFac,alphaHandled == util::EventReply::Unhandled);

	if(AddEmissionMap(cclShader).has_value())
	{
		auto emissionBsdf = cclShader.AddEmissionNode();
		emissionBsdf.inStrength = 1.f;
		LinkEmission(emissionBsdf.inColor);

		auto lightPathNode = cclShader.AddLightPathNode();
		auto mixEmission = cclShader.AddMixClosureNode();
		cclShader.Link(emissionBsdf,mixEmission.inClosure1);
		cclShader.Link(mix.outClosure,mixEmission.inClosure2);
		cclShader.Link(lightPathNode.outIsCameraRay,mixEmission.inFac);
		mix = mixEmission;
	}

	cclShader.Link(mix,cclShader.GetOutputNode().inSurface);
	return true;
}
util::EventReply raytracing::ShaderParticle::InitializeTransparency(CCLShader &cclShader,ImageTextureNode &albedoNode,const NumberSocket &alphaSocket) const
{
	// TODO: Remove this code! It's obsolete.
#if 0
	auto aNode = albedoNode.outAlpha;
	if(umath::is_flag_set(m_renderFlags,RenderFlags::AdditiveByColor))
	{
		auto rgbNode = cclShader.AddSeparateRGBNode(albedoNode.outColor);
		auto rgMaxNode = cclShader.AddMathNode(rgbNode.outR,rgbNode.outG,ccl::NodeMathType::NODE_MATH_MAXIMUM);
		auto rgbMaxNode = cclShader.AddMathNode(rgMaxNode,rgbNode.outB,ccl::NodeMathType::NODE_MATH_MAXIMUM);
		auto clampedNode = cclShader.AddMathNode(cclShader.AddMathNode(rgbMaxNode,0.f,ccl::NodeMathType::NODE_MATH_MAXIMUM),1.f,ccl::NodeMathType::NODE_MATH_MINIMUM);
		aNode = albedoNode.outAlpha *clampedNode;
	}
	cclShader.Link(aNode,alphaSocket);
	return util::EventReply::Handled;
#endif
	return util::EventReply::Unhandled;
}
void raytracing::ShaderParticle::DoSerialize(DataStream &dsIn) const
{
	ShaderPBR::DoSerialize(dsIn);

	dsIn->Write(m_renderFlags);
	dsIn->Write(m_color);
}
void raytracing::ShaderParticle::DoDeserialize(DataStream &dsIn,uint32_t version)
{
	ShaderPBR::DoDeserialize(dsIn,version);

	m_renderFlags = dsIn->Read<decltype(m_renderFlags)>();
	m_color = dsIn->Read<decltype(m_color)>();
}

////////////////

util::EventReply raytracing::ShaderPBR::InitializeTransparency(CCLShader &cclShader,ImageTextureNode &albedoNode,const NumberSocket &alphaSocket) const {return util::EventReply::Unhandled;}
bool raytracing::ShaderPBR::InitializeCCLShader(CCLShader &cclShader)
{
	// Note: We always need the albedo texture information for the translucency.
	// Whether metalness/roughness/etc. affect baking in any way is unclear (probably not),
	// but it also doesn't hurt to have them.
	auto albedoNode = GetAlbedoSet().AddAlbedoMap(*this,cclShader);
	if(albedoNode.has_value() == false)
		return false;

	float specular;
	if(m_specular.has_value())
		specular = *m_specular;
	else
		specular = umath::calc_dielectric_specular_reflection(GetIOR());

	auto nodeBsdf = cclShader.AddPrincipledBSDFNode();
	nodeBsdf.SetMetallic(m_metallic);
	nodeBsdf.SetSpecular(specular);
	nodeBsdf.SetSpecularTint(m_specularTint);
	nodeBsdf.SetAnisotropic(m_anisotropic);
	nodeBsdf.SetAnisotropicRotation(m_anisotropicRotation);
	nodeBsdf.SetSheen(m_sheen);
	nodeBsdf.SetSheenTint(m_sheenTint);
	nodeBsdf.SetClearcoat(m_clearcoat);
	nodeBsdf.SetClearcoatRoughness(m_clearcoatRoughness);
	nodeBsdf.SetIOR(GetIOR());
	nodeBsdf.SetTransmission(m_transmission);
	nodeBsdf.SetTransmissionRoughness(m_transmissionRoughness);

	// Subsurface scattering
	nodeBsdf.SetSubsurface(m_subsurface);
	if(m_subsurface > 0.f)
	{
		Socket albedoColor;
		if(GetAlbedoColorNode(*this,cclShader,albedoColor))
		{
			albedoColor = cclShader.AddMixNode(albedoColor,cclShader.AddCombineRGBNode(m_subsurfaceColorFactor.r,m_subsurfaceColorFactor.g,m_subsurfaceColorFactor.b));
			cclShader.Link(albedoColor,nodeBsdf.inSubsurfaceColor);
			nodeBsdf.SetSubsurfaceMethod(m_subsurfaceMethod);
			nodeBsdf.SetSubsurfaceRadius(m_subsurfaceRadius);
		}
	}

	// Albedo map
	auto alphaHandled = InitializeTransparency(cclShader,*albedoNode,nodeBsdf.inAlpha);
	LinkAlbedo(*this,nodeBsdf.inBaseColor,&nodeBsdf.inAlpha,alphaHandled == util::EventReply::Unhandled);

	// Normal map
	if(AddNormalMap(cclShader).has_value())
		LinkNormal(nodeBsdf.inNormal);

	// Metalness map
	if(AddMetalnessMap(cclShader).has_value())
		LinkMetalness(nodeBsdf.inMetallic);

	// Roughness map
	if(AddRoughnessMap(cclShader).has_value())
		LinkRoughness(nodeBsdf.inRoughness);

	// Emission map
	if(AddEmissionMap(cclShader).has_value())
		LinkEmission(nodeBsdf.inEmission);

	enum class DebugMode : uint8_t
	{
		None = 0u,
		Metalness,
		Specular,
		Albedo,
		Normal,
		Roughness,
		Emission,
		Subsurface
	};

	static auto debugMode = DebugMode::None;
	switch(debugMode)
	{
	case DebugMode::Metalness:
	{
		auto color = cclShader.AddCombineRGBNode();
		LinkMetalness(color.inR);
		LinkMetalness(color.inG);
		LinkMetalness(color.inB);
		cclShader.Link(color,cclShader.GetOutputNode().inSurface);
		return true;
	}
	case DebugMode::Specular:
		cclShader.Link(cclShader.AddCombineRGBNode(m_specular,m_specular,m_specular),cclShader.GetOutputNode().inSurface);
		return true;
	case DebugMode::Albedo:
		LinkAlbedoToBSDF(*this,cclShader.GetOutputNode().inSurface);
		return true;
	case DebugMode::Normal:
		LinkNormalToBSDF(cclShader.GetOutputNode().inSurface);
		return true;
	case DebugMode::Roughness:
	{
		auto color = cclShader.AddCombineRGBNode();
		LinkRoughness(color.inR);
		LinkRoughness(color.inG);
		LinkRoughness(color.inB);
		cclShader.Link(color,cclShader.GetOutputNode().inSurface);
		return true;
	}
	case DebugMode::Emission:
	{
		auto color = cclShader.AddCombineRGBNode();
		LinkEmission(color.inR);
		LinkEmission(color.inG);
		LinkEmission(color.inB);
		cclShader.Link(color,cclShader.GetOutputNode().inSurface);
		return true;
	}
	case DebugMode::Subsurface:
	{
		auto color = cclShader.AddCombineRGBNode(m_subsurfaceColorFactor.r,m_subsurfaceColorFactor.g,m_subsurfaceColorFactor.b);
		cclShader.Link(color,cclShader.GetOutputNode().inSurface);
		return true;
	}
	}

	cclShader.Link(nodeBsdf,cclShader.GetOutputNode().inSurface);
	return true;
}
void raytracing::ShaderPBR::DoSerialize(DataStream &dsIn) const
{
	ShaderModuleNormal::Serialize(dsIn);
	ShaderModuleRoughness::Serialize(dsIn);
	ShaderModuleMetalness::Serialize(dsIn);
	ShaderModuleEmission::Serialize(dsIn);
	ShaderModuleSpriteSheet::Serialize(dsIn);
	ShaderModuleIOR::Serialize(dsIn);

	dsIn->Write(m_metallic);
	dsIn->Write(m_specular);
	dsIn->Write(m_specularTint);
	dsIn->Write(m_anisotropic);
	dsIn->Write(m_anisotropicRotation);
	dsIn->Write(m_sheen);
	dsIn->Write(m_sheenTint);
	dsIn->Write(m_clearcoat);
	dsIn->Write(m_clearcoatRoughness);
	dsIn->Write(m_transmission);
	dsIn->Write(m_transmissionRoughness);

	dsIn->Write(m_subsurface);
	dsIn->Write(m_subsurfaceColorFactor);
	dsIn->Write(m_subsurfaceMethod);
	dsIn->Write(m_subsurfaceRadius);
}
void raytracing::ShaderPBR::DoDeserialize(DataStream &dsIn,uint32_t version)
{
	ShaderModuleNormal::Deserialize(dsIn);
	ShaderModuleRoughness::Deserialize(dsIn);
	ShaderModuleMetalness::Deserialize(dsIn);
	ShaderModuleEmission::Deserialize(dsIn);
	ShaderModuleSpriteSheet::Deserialize(dsIn);
	ShaderModuleIOR::Deserialize(dsIn,version);

	m_metallic = dsIn->Read<decltype(m_metallic)>();
	m_specular = dsIn->Read<decltype(m_specular)>();
	m_specularTint = dsIn->Read<decltype(m_specularTint)>();
	m_anisotropic = dsIn->Read<decltype(m_anisotropic)>();
	m_anisotropicRotation = dsIn->Read<decltype(m_anisotropicRotation)>();
	m_sheen = dsIn->Read<decltype(m_sheen)>();
	m_sheenTint = dsIn->Read<decltype(m_sheenTint)>();
	m_clearcoat = dsIn->Read<decltype(m_clearcoat)>();
	m_clearcoatRoughness = dsIn->Read<decltype(m_clearcoatRoughness)>();
	if(version < 2)
		SetIOR(dsIn->Read<float>());
	m_transmission = dsIn->Read<decltype(m_transmission)>();
	m_transmissionRoughness = dsIn->Read<decltype(m_transmissionRoughness)>();

	m_subsurface = dsIn->Read<decltype(m_subsurface)>();
	m_subsurfaceColorFactor = dsIn->Read<decltype(m_subsurfaceColorFactor)>();
	m_subsurfaceMethod = dsIn->Read<decltype(m_subsurfaceMethod)>();
	m_subsurfaceRadius = dsIn->Read<decltype(m_subsurfaceRadius)>();
}

////////////////

raytracing::PShaderNode raytracing::ShaderNode::Create(CCLShader &shader,ccl::ShaderNode &shaderNode)
{
	return PShaderNode{new ShaderNode{shader,shaderNode}};
}
raytracing::ShaderNode::ShaderNode(CCLShader &shader,ccl::ShaderNode &shaderNode)
	: m_shader{shader},m_shaderNode{shaderNode}
{}

util::WeakHandle<raytracing::ShaderNode> raytracing::ShaderNode::GetHandle()
{
	return util::WeakHandle<raytracing::ShaderNode>{shared_from_this()};
}

ccl::ShaderNode *raytracing::ShaderNode::operator->() {return &m_shaderNode;}
ccl::ShaderNode *raytracing::ShaderNode::operator*() {return &m_shaderNode;}

ccl::ShaderInput *raytracing::ShaderNode::FindInput(const std::string &inputName)
{
	auto it = std::find_if(m_shaderNode.inputs.begin(),m_shaderNode.inputs.end(),[&inputName](const ccl::ShaderInput *shInput) {
		return ccl::string_iequals(shInput->socket_type.name.string(),inputName);
		});
	return (it != m_shaderNode.inputs.end()) ? *it : nullptr;
}
ccl::ShaderOutput *raytracing::ShaderNode::FindOutput(const std::string &outputName)
{
	auto it = std::find_if(m_shaderNode.outputs.begin(),m_shaderNode.outputs.end(),[&outputName](const ccl::ShaderOutput *shOutput) {
		return ccl::string_iequals(shOutput->socket_type.name.string(),outputName);
		});
	return (it != m_shaderNode.outputs.end()) ? *it : nullptr;
}

///////////

raytracing::UVHandlerEye::UVHandlerEye(const Vector4 &irisProjU,const Vector4 &irisProjV,float dilationFactor,float maxDilationFactor,float irisUvRadius)
	: m_irisProjU{irisProjU},m_irisProjV{irisProjV},m_dilationFactor{dilationFactor},m_maxDilationFactor{maxDilationFactor},m_irisUvRadius{irisUvRadius}
{}
std::optional<raytracing::Socket> raytracing::UVHandlerEye::InitializeNodes(CCLShader &shader)
{
	auto nodeGeometry = shader.AddGeometryNode();
	auto nodeSeparateXYZ = shader.AddSeparateXYZNode(nodeGeometry.outPosition);
	auto cyclesUnitsToPragma = static_cast<float>(util::pragma::metres_to_units(1.f));
	auto x = nodeSeparateXYZ.outX *cyclesUnitsToPragma;
	auto y = nodeSeparateXYZ.outY *cyclesUnitsToPragma;
	auto z = nodeSeparateXYZ.outZ *cyclesUnitsToPragma;
	auto nodeUvX = NumberSocket::dot({x,y,z,1.f},{m_irisProjU.x,m_irisProjU.y,m_irisProjU.z,m_irisProjU.w});
	auto nodeUvY = NumberSocket::dot({x,y,z,1.f},{m_irisProjV.x,m_irisProjV.y,m_irisProjV.z,m_irisProjV.w});

	// Pupil dilation
	auto pupilCenterToBorder = (NumberSocket::len({nodeUvX,nodeUvY}) /m_irisUvRadius).clamp(0.f,1.f);
	auto factor = shader.AddConstantNode(1.f).lerp(pupilCenterToBorder,umath::clamp(m_dilationFactor,0.f,m_maxDilationFactor) *2.5f -1.25f);
	nodeUvX = nodeUvX *factor;
	nodeUvY = nodeUvY *factor;

	nodeUvX = (nodeUvX +1.f) /2.f;
	nodeUvY = (nodeUvY +1.f) /2.f;
	return shader.AddCombineXYZNode(nodeUvX,nodeUvY);
}
enum class UVHandlerType : uint32_t
{
	Eye = 0
};
std::shared_ptr<raytracing::UVHandler> raytracing::UVHandler::Create(DataStream &dsIn)
{
	auto type = dsIn->Read<UVHandlerType>();
	switch(type)
	{
	case UVHandlerType::Eye:
	{
		auto uvHandlerEye = std::make_shared<UVHandlerEye>();
		uvHandlerEye->Deserialize(dsIn);
		return uvHandlerEye;
	}
	}
	return nullptr;
}
void raytracing::UVHandlerEye::Serialize(DataStream &dsOut) const
{
	dsOut->Write(UVHandlerType::Eye);
	dsOut->Write(m_irisProjU);
	dsOut->Write(m_irisProjV);
	dsOut->Write(m_dilationFactor);
	dsOut->Write(m_maxDilationFactor);
	dsOut->Write(m_irisUvRadius);
}
void raytracing::UVHandlerEye::Deserialize(DataStream &dsIn)
{
	m_irisProjU = dsIn->Read<decltype(m_irisProjU)>();
	m_irisProjV = dsIn->Read<decltype(m_irisProjV)>();
	m_dilationFactor = dsIn->Read<decltype(m_dilationFactor)>();
	m_maxDilationFactor = dsIn->Read<decltype(m_maxDilationFactor)>();
	m_irisUvRadius = dsIn->Read<decltype(m_irisUvRadius)>();
}
#pragma optimize("",on)
