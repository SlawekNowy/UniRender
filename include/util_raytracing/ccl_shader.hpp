/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __CCL_SHADER_HPP__
#define __CCL_SHADER_HPP__

#include "definitions.hpp"
#include <memory>
#include "shader.hpp"
#include "shader_nodes.hpp"

namespace ccl
{
	class Scene; class Shader; class ShaderGraph; class ShaderNode; class ShaderInput; class ShaderOutput;
	enum NodeMathType : int32_t;
	enum AttributeStandard : int32_t;
};

namespace raytracing
{
	class DLLRTUTIL CCLShader
		: public std::enable_shared_from_this<CCLShader>
	{
	public:
		static std::shared_ptr<CCLShader> Create(Shader &shader);
		static std::shared_ptr<CCLShader> Create(Shader &shader,ccl::Shader &cclShader);

		~CCLShader();
		void Finalize();
		PShaderNode AddNode(const std::string &type,const std::string &name);
		PShaderNode FindNode(const std::string &name) const;
		bool Link(
			const std::string &fromNodeName,const std::string &fromSocketName,
			const std::string &toNodeName,const std::string &toSocketName,
			bool breakExistingLinks=false
		);
		bool Link(const Socket &fromSocket,const Socket &toSocket,bool breakExistingLinks=false);
		bool Link(const NumberSocket &fromSocket,const Socket &toSocket);
		bool Link(const Socket &fromSocket,const NumberSocket &toSocket);
		bool Link(const NumberSocket &fromSocket,const NumberSocket &toSocket);
		void Disconnect(const Socket &socket);
		bool ValidateSocket(const std::string &nodeName,const std::string &socketName,bool output=true) const;

		OutputNode GetOutputNode() const;
		MathNode AddMathNode();
		MathNode AddMathNode(const NumberSocket &n0,const NumberSocket &n1,ccl::NodeMathType mathOp);
		MathNode AddConstantNode(float f);
		ImageTextureNode AddColorImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket={});
		ImageTextureNode AddGradientImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket={});
		NormalMapNode AddNormalMapImageTextureNode(const std::string &fileName,const std::string &meshName,const std::optional<Socket> &uvSocket={},NormalMapNode::Space space=NormalMapNode::Space::Tangent);
		EnvironmentTextureNode AddEnvironmentTextureNode(const std::string &fileName);
		SeparateXYZNode AddSeparateXYZNode(const Socket &srcSocket);
		CombineXYZNode AddCombineXYZNode(const std::optional<const NumberSocket> &x={},const std::optional<const NumberSocket> &y={},const std::optional<const NumberSocket> &z={});
		SeparateRGBNode AddSeparateRGBNode(const Socket &srcSocket);
		SeparateRGBNode AddSeparateRGBNode();
		CombineRGBNode AddCombineRGBNode(const std::optional<const NumberSocket> &r={},const std::optional<const NumberSocket> &g={},const std::optional<const NumberSocket> &b={});
		GeometryNode AddGeometryNode();
		CameraDataNode AddCameraDataNode();
		NormalMapNode AddNormalMapNode();
		LightPathNode AddLightPathNode();
		MixClosureNode AddMixClosureNode();
		AddClosureNode AddAddClosureNode();
		ScatterVolumeNode AddScatterVolumeNode();
		HSVNode AddHSVNode();
		MixNode AddMixNode(const Socket &socketColor1,const Socket &socketColor2,MixNode::Type type=MixNode::Type::Mix,const std::optional<const NumberSocket> &fac={});
		MixNode AddMixNode(MixNode::Type type=MixNode::Type::Mix);
		BackgroundNode AddBackgroundNode();
		TextureCoordinateNode AddTextureCoordinateNode();
		MappingNode AddMappingNode();
		ColorNode AddColorNode();
		AttributeNode AddAttributeNode(ccl::AttributeStandard attrType);
		EmissionNode AddEmissionNode();
		NumberSocket AddVertexAlphaNode();
		NumberSocket AddWrinkleFactorNode();

		PrincipledBSDFNode AddPrincipledBSDFNode();
		ToonBSDFNode AddToonBSDFNode();
		GlassBSDFNode AddGlassBSDFNode();
		TransparentBsdfNode AddTransparentBSDFNode();
		TranslucentBsdfNode AddTranslucentBSDFNode();
		DiffuseBsdfNode AddDiffuseBSDFNode();
		MixClosureNode AddTransparencyClosure(const Socket &colorSocket,const NumberSocket &alphaSocket,AlphaMode alphaMode,float alphaCutoff=0.5f);
		NumberSocket ApplyAlphaMode(const NumberSocket &alphaSocket,AlphaMode alphaMode,float alphaCutoff=0.5f);

		Shader &GetShader() const;
		std::optional<Socket> GetUVSocket(Shader::TextureType type,ShaderModuleSpriteSheet *shaderModSpriteSheet=nullptr,SpriteSheetFrame frame=SpriteSheetFrame::First);

		ccl::Shader *operator->();
		ccl::Shader *operator*();
	protected:
		CCLShader(Shader &shader,ccl::Shader &cclShader,ccl::ShaderGraph &cclShaderGraph);
		ImageTextureNode AddImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket,bool color);
		std::string GetCurrentInternalNodeName() const;
	private:
		friend Shader;
		Shader &m_shader;
		ccl::Shader &m_cclShader;
		ccl::ShaderGraph &m_cclGraph;
		std::vector<PShaderNode> m_nodes = {};
		std::array<std::optional<Socket>,umath::to_integral(Shader::TextureType::Count)> m_uvSockets = {};
		bool m_bDeleteGraphIfUnused = false;
	};
};

#endif
