/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __UTIL_RAYTRACING_SCENE_HPP__
#define __UTIL_RAYTRACING_SCENE_HPP__

#include "definitions.hpp"
#include <sharedutils/datastream.h>
#include <sharedutils/util_weak_handle.hpp>
#include <sharedutils/util.h>
#include <condition_variable>
#include <memory>
#include <mathutil/uvec.h>
#include <functional>
#include <optional>
#include <atomic>
#include <thread>

#define ENABLE_TEST_AMBIENT_OCCLUSION

namespace ccl
{
	class Session;
	class Scene;
	class ShaderInput;
	class ShaderNode;
	class ShaderOutput;
	class ShaderGraph;
	struct float3;
	struct float2;
	struct Transform;
	class ImageTextureNode;
	class EnvironmentTextureNode;
	class BufferParams;
	class SessionParams;
};
namespace udm
{
	struct Property;
};
namespace OpenImageIO_v2_1
{
	class ustring;
};
namespace umath {class Transform; class ScaledTransform;};
namespace uimg {class ImageBuffer;};
namespace util::ocio {class ColorProcessor;};
class DataStream;
namespace unirender
{
	class GroupNodeDesc;
	class SceneObject;
	class Scene;
	class Shader;
	class WorldObject;
	class ShaderModuleRoughness;
	using PShader = std::shared_ptr<Shader>;
	class Object;
	using PObject = std::shared_ptr<Object>;
	class Light;
	using PLight = std::shared_ptr<Light>;
	class Camera;
	using PCamera = std::shared_ptr<Camera>;
	using PScene = std::shared_ptr<Scene>;
	class Mesh;
	using PMesh = std::shared_ptr<Mesh>;
	struct Socket;

	DLLRTUTIL void serialize_udm_property(DataStream &dsOut,const udm::Property &prop);
	DLLRTUTIL void deserialize_udm_property(DataStream &dsIn,udm::Property &prop);

	class ModelCache;
	class ShaderCache;
	class NodeManager;
	class CCLShader;
	class TileManager;
	enum class ColorTransform : uint8_t;
	class DLLRTUTIL Scene
		: public std::enable_shared_from_this<Scene>
	{
	public:
		static constexpr uint32_t SERIALIZATION_VERSION = 6;
		struct DLLRTUTIL SerializationData
		{
			std::string outputFileName;
		};

		enum class DeviceType : uint8_t
		{
			CPU = 0u,
			GPU,

			Count
		};

		struct DLLRTUTIL SceneInfo
		{
			std::string sky = "";
			EulerAngles skyAngles {};
			float skyStrength = 1.f;
			bool transparentSky = false;
			float emissionStrength = 1.f;
			float lightIntensityFactor = 1.f;
			float motionBlurStrength = 0.f;
			uint32_t maxTransparencyBounces = 64;
			uint32_t maxBounces = 12;
			uint32_t maxDiffuseBounces = 4;
			uint32_t maxGlossyBounces = 4;
			uint32_t maxTransmissionBounces = 12;
			float exposure = 1.f;
			bool useAdaptiveSampling = true;
			float adaptiveSamplingThreshold = 0.01f;
			uint32_t adaptiveMinSamples = 0;
		};

		enum class ColorSpace : uint8_t
		{
			SRGB = 0,
			Raw
		};
		enum class RenderMode : uint8_t
		{
			RenderImage = 0u,
			BakeAmbientOcclusion,
			BakeNormals,
			BakeDiffuseLighting,
			BakeDiffuseLightingSeparate,
			SceneAlbedo,
			SceneNormals,
			SceneDepth,
			
			Alpha,
			GeometryNormal,
			ShadingNormal,
			DirectDiffuse,
			DirectDiffuseReflect,
			DirectDiffuseTransmit,
			DirectGlossy,
			DirectGlossyReflect,
			DirectGlossyTransmit,
			Emission,
			IndirectDiffuse,
			IndirectDiffuseReflect,
			IndirectDiffuseTransmit,
			IndirectGlossy,
			IndirectGlossyReflect,
			IndirectGlossyTransmit,
			IndirectSpecular,
			IndirectSpecularReflect,
			IndirectSpecularTransmit,
			Uv,
			Irradiance,
			Noise,
			Caustic,

			Count,

			BakingStart = BakeAmbientOcclusion,
			BakingEnd = BakeDiffuseLightingSeparate,
			LightmapBakingStart = BakeDiffuseLighting,
			LightmapBakingEnd = BakeDiffuseLightingSeparate
		};
		enum class StateFlags : uint16_t
		{
			None = 0u,
			OutputResultWithHDRColors = 1u
		};

		enum class DenoiseMode : uint8_t
		{
			None = 0,
			AutoFast,
			AutoDetailed,
			Optix,
			OpenImage
		};

		struct DLLRTUTIL ColorTransformInfo
		{
			std::string config;
			std::optional<std::string> lookName {};
		};

		struct DLLRTUTIL CreateInfo
		{
			CreateInfo();
			void Serialize(DataStream &ds) const;
			void Deserialize(DataStream &ds,uint32_t version);

			std::string renderer = "cycles";
			std::optional<uint32_t> samples = {};
			bool hdrOutput = false;
			DenoiseMode denoiseMode = DenoiseMode::AutoDetailed;
			bool progressive = false;
			bool progressiveRefine = false;
			DeviceType deviceType = DeviceType::GPU;
			float exposure = 1.f;
			std::optional<ColorTransformInfo> colorTransform {};
			bool preCalculateLight = false;
		};
		static bool IsRenderSceneMode(RenderMode renderMode);
		static bool IsLightmapRenderMode(RenderMode renderMode);
		static bool IsBakingRenderMode(RenderMode renderMode);
		static void SetKernelPath(const std::string &kernelPath);
		static std::shared_ptr<Scene> Create(NodeManager &nodeManager,RenderMode renderMode,const CreateInfo &createInfo={});
		static std::shared_ptr<Scene> Create(NodeManager &nodeManager,DataStream &dsIn,const std::string &rootDir,RenderMode renderMode,const CreateInfo &createInfo={});
		static std::shared_ptr<Scene> Create(NodeManager &nodeManager,DataStream &dsIn,const std::string &rootDir);
		static bool ReadHeaderInfo(DataStream &ds,RenderMode &outRenderMode,CreateInfo &outCreateInfo,SerializationData &outSerializationData,uint32_t &outVersion,SceneInfo *optOutSceneInfo=nullptr);
		//
		static std::optional<std::string> GetAbsSkyPath(const std::string &skyTex);
		static std::string ToRelativePath(const std::string &absPath);
		static std::string ToAbsolutePath(const std::string &relPath);
		template<class T>
			static void SerializeDataBlock(const T &value,DataStream &dsOut,size_t dataStartOffset)
		{
			dsOut->Write(reinterpret_cast<const uint8_t*>(&value) +dataStartOffset,sizeof(T) -dataStartOffset);
		}

		template<class T>
			static void DeserializeDataBlock(T &value,DataStream &dsOut,size_t dataStartOffset)
		{
			dsOut->Read(reinterpret_cast<uint8_t*>(&value) +dataStartOffset,sizeof(T) -dataStartOffset);
		}

		static constexpr uint32_t INPUT_CHANNEL_COUNT = 4u;
		static constexpr uint32_t OUTPUT_CHANNEL_COUNT = 4u;

		~Scene();
		Camera &GetCamera();
		float GetProgress() const;
		RenderMode GetRenderMode() const {return m_renderMode;}

		bool IsProgressive() const;
		bool IsProgressiveRefine() const;

		const std::vector<PLight> &GetLights() const;
		std::vector<PLight> &GetLights();

		const SceneInfo &GetSceneInfo() const {return const_cast<Scene*>(this)->GetSceneInfo();}
		SceneInfo &GetSceneInfo() {return m_sceneInfo;}
		StateFlags GetStateFlags() const {return m_stateFlags;}

		void SetLightIntensityFactor(float f);
		float GetLightIntensityFactor() const;

		static void SetVerbose(bool verbose);
		static bool IsVerbose();

		static bool ReadSerializationHeader(DataStream &dsIn,RenderMode &outRenderMode,CreateInfo &outCreateInfo,SerializationData &outSerializationData,uint32_t &outVersion,SceneInfo *optOutSceneInfo=nullptr);
		void Save(DataStream &dsOut,const std::string &rootDir,const SerializationData &serializationData) const;
		bool Load(DataStream &dsIn,const std::string &rootDir);

		void HandleError(const std::string &errMsg) const;

		NodeManager &GetShaderNodeManager() const;
		void SetSky(const std::string &skyPath);
		void SetSkyAngles(const EulerAngles &angSky);
		void SetSkyStrength(float strength);
		void SetEmissionStrength(float strength);
		float GetEmissionStrength() const;
		void SetMaxTransparencyBounces(uint32_t maxBounces);
		void SetMaxBounces(uint32_t maxBounces);
		void SetMaxDiffuseBounces(uint32_t bounces);
		void SetMaxGlossyBounces(uint32_t bounces);
		void SetMaxTransmissionBounces(uint32_t bounces);
		void SetMotionBlurStrength(float strength);
		void SetAdaptiveSampling(bool enabled,float adaptiveSamplingThreshold=0.01f,uint32_t adaptiveMinSamples=0);
		void SetBakeTarget(Object &o);
		const std::string *GetBakeTargetName() const;
		bool HasBakeTarget() const;
		Vector2i GetResolution() const;
		const CreateInfo &GetCreateInfo() const {return m_createInfo;}

		const std::vector<std::shared_ptr<ModelCache>> &GetModelCaches() const {return m_mdlCaches;}
		void AddModelsFromCache(const ModelCache &cache);
		void AddLight(Light &light);
		
		void Close();
		void Finalize();

		DenoiseMode GetDenoiseMode() const {return m_createInfo.denoiseMode;}
		bool ShouldDenoise() const {return GetDenoiseMode() != DenoiseMode::None;}
		float GetGamma() const;

		std::unordered_map<size_t,WorldObject*> BuildActorMap() const;
		static void AddActorToActorMap(std::unordered_map<size_t,WorldObject*> &map,WorldObject &obj);

		void PrintLogInfo();
	private:
		friend Shader;
		friend Object;
		friend Light;
		Scene(NodeManager &nodeManager,RenderMode renderMode);
		static ccl::ShaderOutput *FindShaderNodeOutput(ccl::ShaderNode &node,const std::string &output);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const std::string &nodeName);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const OpenImageIO_v2_1::ustring &name);
		void DenoiseHDRImageArea(uimg::ImageBuffer &imgBuffer,uint32_t imgWidth,uint32_t imgHeight,uint32_t x,uint32_t y,uint32_t w,uint32_t h) const;
		bool IsValidTexture(const std::string &filePath) const;

		std::shared_ptr<NodeManager> m_nodeManager = nullptr;
		SceneInfo m_sceneInfo {};
		std::vector<std::shared_ptr<ModelCache>> m_mdlCaches {};
		std::vector<PLight> m_lights = {};
		std::optional<std::string> m_bakeTargetName {};
		CreateInfo m_createInfo {};
		PCamera m_camera = nullptr;
		StateFlags m_stateFlags = StateFlags::None;
		RenderMode m_renderMode = RenderMode::RenderImage;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(unirender::Scene::StateFlags)

#endif
