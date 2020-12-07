/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __PR_CYCLES_SHADER_HPP__
#define __PR_CYCLES_SHADER_HPP__

#include "definitions.hpp"
#include "scene_object.hpp"
#include "shader_nodes.hpp"
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <mathutil/umath.h>
#include <mathutil/color.h>
#include <sharedutils/util_event_reply.hpp>
#include <sharedutils/alpha_mode.hpp>

#if 0
namespace ccl
{
	class Scene; class Shader; class ShaderGraph; class ShaderNode; class ShaderInput; class ShaderOutput;
	enum NodeMathType : int32_t;
	enum AttributeStandard : int32_t;
};

class DataStream;
namespace unirender
{
	enum class Channel : uint8_t
	{
		Red = 0,
		Green,
		Blue,
		Alpha
	};

	class Scene;
	class Shader;
	class ShaderNode;
	using PShaderNode = std::shared_ptr<ShaderNode>;
	using PShader = std::shared_ptr<Shader>;
	struct Socket;
	class CCLShader;
	class ShaderDesc;
	struct UVHandler;
	class DLLRTUTIL Shader
		: public SceneObject,
		public std::enable_shared_from_this<Shader>
	{
	public:
		template<class TShader>
			static std::shared_ptr<TShader> Create(Scene &scene,const std::string &name);
		static PShader Create(Scene &scene,DataStream &dsIn,uint32_t version);

		enum class Flags : uint8_t
		{
			None = 0u,
			EmissionFromAlbedoAlpha = 1u,
			AdditiveByColor = EmissionFromAlbedoAlpha<<1u
		};

		enum class TextureType : uint8_t
		{
			Albedo = 0u,
			Normal,
			Roughness,
			Metalness,
			Emission,
			Specular,

			Count
		};

		util::WeakHandle<Shader> GetHandle();

		virtual void DoFinalize() override;

		Scene &GetScene() const;
		const std::string &GetName() const;
		const std::string &GetMeshName() const;
		void SetMeshName(const std::string &meshName);
		bool HasFlag(Flags flags) const;
		void SetFlags(Flags flags,bool enabled);
		Flags GetFlags() const;
		void SetUVHandler(TextureType type,const std::shared_ptr<UVHandler> &uvHandler);
		const std::shared_ptr<UVHandler> &GetUVHandler(TextureType type) const;

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn,uint32_t version);

		void SetAlphaMode(AlphaMode alphaMode,float alphaCutoff=0.5f);
		AlphaMode GetAlphaMode() const;
		float GetAlphaCutoff() const;

		void SetAlphaFactor(float factor);
		float GetAlphaFactor() const;

		void SetUVHandlers(const std::array<std::shared_ptr<UVHandler>,umath::to_integral(TextureType::Count)> &handlers);
		const std::array<std::shared_ptr<UVHandler>,umath::to_integral(TextureType::Count)> &GetUVHandlers() const;

		std::shared_ptr<CCLShader> GenerateCCLShader(const ShaderDesc &desc,const std::function<void(const std::string&)> &errorLog=nullptr);
		std::shared_ptr<CCLShader> GenerateCCLShader(ccl::Shader &cclShader,const ShaderDesc &desc,const std::function<void(const std::string&)> &errorLog=nullptr);
	protected:
		Shader(Scene &scene,const std::string &name);
		bool SetupCCLShader(CCLShader &cclShader);
		virtual bool InitializeCCLShader(CCLShader &cclShader)=0;
		virtual void DoSerialize(DataStream &dsIn) const=0;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version)=0;

		std::string m_name;
		std::string m_meshName;
		Flags m_flags = Flags::None;
		AlphaMode m_alphaMode = AlphaMode::Opaque;
		float m_alphaCutoff = 0.5f;
		float m_alphaFactor = 1.f;
		Scene &m_scene;
		std::array<std::shared_ptr<UVHandler>,umath::to_integral(TextureType::Count)> m_uvHandlers = {};
	};

	class ShaderModuleSpriteSheet;
	enum class SpriteSheetFrame : uint8_t
	{
		First = 0,
		Second
	};

	class DLLRTUTIL ShaderNode
		: public std::enable_shared_from_this<ShaderNode>
	{
	public:
		static PShaderNode Create(const std::string &name);
		util::WeakHandle<ShaderNode> GetHandle();
		ccl::ShaderNode *operator->();
		ccl::ShaderNode *operator*();

		template<typename T>
			bool SetInputArgument(const std::string &inputName,const T &arg);
	private:
		friend CCLShader;
		ShaderNode(const std::string &name);
		ccl::ShaderInput *FindInput(const std::string &inputName);
		ccl::ShaderOutput *FindOutput(const std::string &outputName);
		std::string m_name;
	};

	class ShaderModuleAlbedo;
	class DLLRTUTIL ShaderAlbedoSet
	{
	public:
		virtual ~ShaderAlbedoSet()=default;

		void SetAlbedoMap(const std::string &albedoMap);
		const std::optional<std::string> &GetAlbedoMap() const;

		void SetColorFactor(const Vector4 &colorFactor);
		const Vector4 &GetColorFactor() const;

		const std::optional<ImageTextureNode> &GetAlbedoNode() const;

		std::optional<ImageTextureNode> AddAlbedoMap(ShaderModuleAlbedo &albedoModule,CCLShader &shader);

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn);
	private:
		std::optional<std::string> m_albedoMap {};
		std::optional<ImageTextureNode> m_albedoNode {};
		Vector4 m_colorFactor = {1.f,1.f,1.f,1.f};
	};

	class DLLRTUTIL ShaderModuleSpriteSheet
	{
	public:
		struct SpriteSheetData
		{
			std::pair<Vector2,Vector2> uv0;
			std::string albedoMap2 {};
			std::pair<Vector2,Vector2> uv1;
			float interpFactor = 0.f;
		};
		void SetSpriteSheetData(
			const Vector2 &uv0Min,const Vector2 &uv0Max,
			const std::string &albedoMap2,const Vector2 &uv1Min,const Vector2 &uv1Max,
			float interpFactor
		);
		void SetSpriteSheetData(const SpriteSheetData &spriteSheetData);
		const std::optional<SpriteSheetData> &GetSpriteSheetData() const;

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn);
	private:
		std::optional<SpriteSheetData> m_spriteSheetData {};
	};

	class DLLRTUTIL ShaderModuleAlbedo
	{
	public:
		virtual ~ShaderModuleAlbedo()=default;

		void SetEmissionFromAlbedoAlpha(Shader &shader,bool b);

		const ShaderAlbedoSet &GetAlbedoSet() const;
		ShaderAlbedoSet &GetAlbedoSet();

		const ShaderAlbedoSet &GetAlbedoSet2() const;
		ShaderAlbedoSet &GetAlbedoSet2();

		void SetUseVertexAlphasForBlending(bool useAlphasForBlending);
		bool ShouldUseVertexAlphasForBlending() const;

		void SetWrinkleStretchMap(const std::string &wrinkleStretchMap);
		void SetWrinkleCompressMap(const std::string &wrinkleCompressMap);

		bool GetAlbedoColorNode(const Shader &shader,CCLShader &cclShader,Socket &outColor,NumberSocket *optOutAlpha=nullptr);
		void LinkAlbedo(const Shader &shader,const Socket &color,const NumberSocket *optLinkAlpha=nullptr,bool useAlphaIfFlagSet=true,NumberSocket *optOutAlpha=nullptr);
		void LinkAlbedoToBSDF(const Shader &shader,const Socket &bsdf);

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn);
	protected:
		virtual void InitializeAlbedoColor(Socket &inOutColor);
		virtual void InitializeAlbedoAlpha(const Socket &inAlbedoColor,NumberSocket &inOutAlpha);
	private:
		bool SetupAlbedoNodes(CCLShader &shader,Socket &outColor,NumberSocket &outAlpha);
		ShaderAlbedoSet m_albedoSet = {};
		ShaderAlbedoSet m_albedoSet2 = {};
		std::optional<std::string> m_wrinkleStretchMap;
		std::optional<std::string> m_wrinkleCompressMap;
		bool m_useVertexAlphasForBlending = false;
	};

	class DLLRTUTIL ShaderModuleNormal
		: public ShaderModuleAlbedo
	{
	public:
		void SetNormalMap(const std::string &normalMap);
		const std::optional<std::string> &GetNormalMap() const;

		void SetNormalMapSpace(NormalMapNode::Space space);
		NormalMapNode::Space GetNormalMapSpace() const;

		std::optional<Socket> AddNormalMap(CCLShader &shader);
		void LinkNormal(const Socket &normal);
		void LinkNormalToBSDF(const Socket &bsdf);

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn);
	private:
		std::optional<std::string> m_normalMap;
		std::optional<Socket> m_normalSocket = {};
		NormalMapNode::Space m_space = NormalMapNode::Space::Tangent;
	};

	class DLLRTUTIL ShaderModuleMetalness
	{
	public:
		virtual ~ShaderModuleMetalness()=default;
		void SetMetalnessMap(const std::string &metalnessMap,Channel channel=Channel::Blue);
		void SetMetalnessFactor(float metalnessFactor);

		std::optional<NumberSocket> AddMetalnessMap(CCLShader &shader);
		void LinkMetalness(const NumberSocket &metalness);

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn);
	private:
		std::optional<std::string> m_metalnessMap;
		Channel m_metalnessChannel = Channel::Blue;
		std::optional<NumberSocket> m_metalnessSocket = {};
		std::optional<float> m_metalnessFactor = {};
	};

	class DLLRTUTIL ShaderModuleRoughness
	{
	public:
		virtual ~ShaderModuleRoughness()=default;
		void SetRoughnessMap(const std::string &roughnessMap,Channel channel=Channel::Green);
		void SetSpecularMap(const std::string &specularMap,Channel channel=Channel::Green);

		void SetRoughnessFactor(float roughness);

		std::optional<NumberSocket> AddRoughnessMap(CCLShader &shader);
		void LinkRoughness(const NumberSocket &roughness);

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn);
	private:
		std::optional<std::string> m_roughnessMap;
		std::optional<std::string> m_specularMap;
		Channel m_roughnessChannel = Channel::Green;

		std::optional<NumberSocket> m_roughnessSocket = {};
		std::optional<float> m_roughnessFactor = {};
	};

	class DLLRTUTIL ShaderModuleEmission
	{
	public:
		virtual ~ShaderModuleEmission()=default;
		void SetEmissionMap(const std::string &emissionMap);
		void SetEmissionFactor(const Vector3 &factor);
		const Vector3 &GetEmissionFactor() const;
		void SetEmissionIntensity(float intensity);
		float GetEmissionIntensity() const;
		const std::optional<std::string> &GetEmissionMap() const;

		std::optional<Socket> AddEmissionMap(CCLShader &shader);
		void LinkEmission(const Socket &emission);

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn);
	protected:
		virtual void InitializeEmissionColor(Socket &inOutColor);
	private:
		std::optional<std::string> m_emissionMap;
		Vector3 m_emissionFactor = {0.f,0.f,0.f};
		float m_emissionIntensity = 1.f;

		std::optional<Socket> m_emissionSocket = {};
	};

	class DLLRTUTIL ShaderModuleIOR
	{
	public:
		virtual ~ShaderModuleIOR()=default;
		void SetIOR(float ior);
		float GetIOR() const;

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn,uint32_t version);
	private:
		float m_ior = 1.45f;
	};

	/////////////////////

	class DLLRTUTIL ShaderGeneric
		: public Shader
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		using Shader::Shader;
	};

	class DLLRTUTIL ShaderAlbedo
		: public Shader,
		public ShaderModuleAlbedo,
		public ShaderModuleSpriteSheet
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		using Shader::Shader;
	};

	class DLLRTUTIL ShaderColorTest
		: public Shader
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		using Shader::Shader;
	};

	class DLLRTUTIL ShaderVolumeScatter
		: public Shader
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		using Shader::Shader;
	};

	class DLLRTUTIL ShaderNormal
		: public Shader,
		public ShaderModuleNormal,
		public ShaderModuleSpriteSheet
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		using Shader::Shader;
	};

	class DLLRTUTIL ShaderDepth
		: public Shader,
		public ShaderModuleAlbedo,
		public ShaderModuleSpriteSheet
	{
	public:
		void SetFarZ(float farZ);
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		using Shader::Shader;
	private:
		float m_farZ = 1.f;
	};

	class DLLRTUTIL ShaderToon
		: public Shader,
		public ShaderModuleNormal,
		public ShaderModuleSpriteSheet,
		public ShaderModuleRoughness
	{
	public:
		void SetSpecularColor(const Vector3 &col);
		void SetShadeColor(const Vector3 &col);
		void SetDiffuseSize(float size);
		void SetDiffuseSmooth(float smooth);
		void SetSpecularSize(float specSize);
		void SetSpecularSmooth(float smooth);
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		using Shader::Shader;
	private:
		Vector3 m_specularColor {0.1f,0.1f,0.1f};
		Vector3 m_shadeColor {0.701102f,0.318547f,0.212231f};
		float m_diffuseSize = 0.9f;
		float m_diffuseSmooth = 0.f;
		float m_specularSize = 0.2f;
		float m_specularSmooth = 0.f;
	};

	class DLLRTUTIL ShaderGlass
		: public Shader,
		public ShaderModuleNormal,
		public ShaderModuleRoughness,
		public ShaderModuleSpriteSheet,
		public ShaderModuleIOR
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		using Shader::Shader;
	};

	class DLLRTUTIL ShaderPBR
		: public Shader,
		public ShaderModuleNormal,
		public ShaderModuleRoughness,
		public ShaderModuleMetalness,
		public ShaderModuleEmission,
		public ShaderModuleSpriteSheet,
		public ShaderModuleIOR
	{
	public:
		void SetMetallic(float metallic);
		void SetSpecular(float specular);
		void SetSpecularTint(float specularTint);
		void SetAnisotropic(float anisotropic);
		void SetAnisotropicRotation(float anisotropicRotation);
		void SetSheen(float sheen);
		void SetSheenTint(float sheenTint);
		void SetClearcoat(float clearcoat);
		void SetClearcoatRoughness(float clearcoatRoughness);
		void SetTransmission(float transmission);
		void SetTransmissionRoughness(float transmissionRoughness);

		// Subsurface scattering
		void SetSubsurface(float subsurface);
		void SetSubsurfaceColorFactor(const Vector3 &color);
		void SetSubsurfaceMethod(PrincipledBSDFNode::SubsurfaceMethod method);
		void SetSubsurfaceRadius(const Vector3 &radius);
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		virtual util::EventReply InitializeTransparency(CCLShader &cclShader,ImageTextureNode &albedoNode,const NumberSocket &alphaSocket) const;
		using Shader::Shader;
	private:
		// Default settings (Taken from Blender)
		float m_metallic = 0.f;
		std::optional<float> m_specular {};
		float m_specularTint = 0.f;
		float m_anisotropic = 0.f;
		float m_anisotropicRotation = 0.f;
		float m_sheen = 0.f;
		float m_sheenTint = 0.5f;
		float m_clearcoat = 0.f;
		float m_clearcoatRoughness = 0.03f;
		float m_transmission = 0.f;
		float m_transmissionRoughness = 0.f;

		// Subsurface scattering
		float m_subsurface = 0.f;
		Vector3 m_subsurfaceColorFactor = {1.f,1.f,1.f};
		PrincipledBSDFNode::SubsurfaceMethod m_subsurfaceMethod = PrincipledBSDFNode::SubsurfaceMethod::Burley;
		Vector3 m_subsurfaceRadius = {0.f,0.f,0.f};
	};

	class DLLRTUTIL ShaderParticle
		: public ShaderPBR
	{
	public:
		enum class RenderFlags : uint32_t
		{
			None = 0u,
			AdditiveByColor = 1u
		};

		using ShaderPBR::ShaderPBR;
		void SetRenderFlags(RenderFlags flags);
		void SetColor(const Color &color);
		const Color &GetColor() const;
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual void DoSerialize(DataStream &dsIn) const override;
		virtual void DoDeserialize(DataStream &dsIn,uint32_t version) override;
		virtual util::EventReply InitializeTransparency(CCLShader &cclShader,ImageTextureNode &albedoNode,const NumberSocket &alphaSocket) const override;
		virtual void InitializeAlbedoColor(Socket &inOutColor) override;
		virtual void InitializeAlbedoAlpha(const Socket &inAlbedoColor,NumberSocket &inOutAlpha) override;
		virtual void InitializeEmissionColor(Socket &inOutColor) override;
	private:
		RenderFlags m_renderFlags = RenderFlags::None;
		Color m_color = Color::White;
	};

	struct DLLRTUTIL UVHandler
	{
		static std::shared_ptr<UVHandler> Create(DataStream &inDs);
		virtual std::optional<Socket> InitializeNodes(CCLShader &shader)=0;
		virtual void Serialize(DataStream &dsOut) const=0;
		virtual void Deserialize(DataStream &dsIn)=0;
	};

	struct DLLRTUTIL UVHandlerEye
		: public UVHandler
	{
		UVHandlerEye(const Vector4 &irisProjU,const Vector4 &irisProjV,float dilationFactor,float maxDilationFactor,float irisUvRadius);
		UVHandlerEye()=default;
		virtual std::optional<Socket> InitializeNodes(CCLShader &shader) override;
		virtual void Serialize(DataStream &dsOut) const override;
		virtual void Deserialize(DataStream &dsIn) override;
	private:
		Vector4 m_irisProjU = {};
		Vector4 m_irisProjV = {};
		float m_dilationFactor = 0.5f;
		float m_maxDilationFactor = 1.0f;
		float m_irisUvRadius = 0.2f;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(unirender::Shader::Flags)
REGISTER_BASIC_BITWISE_OPERATORS(unirender::ShaderParticle::RenderFlags)

template<class TShader>
	std::shared_ptr<TShader> unirender::Shader::Create(Scene &scene,const std::string &name)
{
	auto pShader = PShader{new TShader{scene,name}};
	scene.m_shaders.push_back(pShader);
	return std::static_pointer_cast<TShader>(pShader);
}

template<typename T>
	bool unirender::ShaderNode::SetInputArgument(const std::string &inputName,const T &arg)
{
	auto it = std::find_if(m_shaderNode.inputs.begin(),m_shaderNode.inputs.end(),[&inputName](const ccl::ShaderInput *shInput) {
		return ccl::string_iequals(shInput->socket_type.name.string(),inputName);
		});
	if(it == m_shaderNode.inputs.end())
		return false;
	auto *input = *it;
	input->set(arg);
	return true;
}
#endif

#endif
