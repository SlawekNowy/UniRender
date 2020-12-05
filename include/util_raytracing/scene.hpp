/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __UTIL_RAYTRACING_SCENE_HPP__
#define __UTIL_RAYTRACING_SCENE_HPP__

#include "definitions.hpp"
#include "util_raytracing/tilemanager.hpp"
#include <sharedutils/util_weak_handle.hpp>
#include <sharedutils/util_parallel_job.hpp>
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
namespace OpenImageIO_v2_1
{
	class ustring;
};
namespace umath {class Transform; class ScaledTransform;};
namespace uimg {class ImageBuffer;};
namespace util::ocio {class ColorProcessor;};
class DataStream;
namespace raytracing
{
	enum class StereoEye : uint8_t
	{
		Left = 0,
		Right,
		Count,

		None = std::numeric_limits<uint8_t>::max()
	};
	class GroupNodeDesc;
	class SceneObject;
	class Scene;
	class Shader;
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

	class DLLRTUTIL SceneWorker
		: public util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>
	{
	public:
		friend Scene;
		SceneWorker(Scene &scene);
		using util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::Cancel;
		virtual void Wait() override;
		virtual std::shared_ptr<uimg::ImageBuffer> GetResult() override;
	private:
		virtual void DoCancel(const std::string &resultMsg) override;
		PScene m_scene = nullptr;
		template<typename TJob,typename... TARGS>
		friend util::ParallelJob<typename TJob::RESULT_TYPE> util::create_parallel_job(TARGS&& ...args);
	};

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
		static constexpr uint32_t SERIALIZATION_VERSION = 5;
		struct SerializationData
		{
			std::string outputFileName;
		};

		enum class DeviceType : uint8_t
		{
			CPU = 0u,
			GPU,

			Count
		};
#pragma pack(push,1)
		struct SceneInfo
		{
			std::string sky = "";
			EulerAngles skyAngles {};
			float skyStrength = 1.f;
			float emissionStrength = 1.f;
			float lightIntensityFactor = 1.f;
			float motionBlurStrength = 0.f;
			uint32_t maxTransparencyBounces = 64;
			uint32_t maxBounces = 12;
			uint32_t maxDiffuseBounces = 4;
			uint32_t maxGlossyBounces = 4;
			uint32_t maxTransmissionBounces = 12;
			float exposure = 1.f;
		};
#pragma pack(pop)
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
			SceneAlbedo,
			SceneNormals,
			SceneDepth
		};
		enum class StateFlags : uint16_t
		{
			None = 0u,
			OutputResultWithHDRColors = 1u,
			SkyInitialized = OutputResultWithHDRColors<<1u,
			HasRenderingStarted = SkyInitialized<<1u
		};

		enum class DenoiseMode : uint8_t
		{
			None = 0,
			Fast,
			Detailed
		};

		struct ColorTransformInfo
		{
			std::string config;
			std::optional<std::string> lookName {};
		};

		struct CreateInfo
		{
			void Serialize(DataStream &ds) const;
			void Deserialize(DataStream &ds);

			std::optional<uint32_t> samples = {};
			bool hdrOutput = false;
			DenoiseMode denoiseMode = DenoiseMode::Detailed;
			bool progressive = false;
			bool progressiveRefine = false;
			DeviceType deviceType = DeviceType::GPU;
			float exposure = 1.f;
			std::optional<ColorTransformInfo> colorTransform {};
		};
		static bool IsRenderSceneMode(RenderMode renderMode);
		static void SetKernelPath(const std::string &kernelPath);
		static std::shared_ptr<Scene> Create(NodeManager &nodeManager,RenderMode renderMode,const CreateInfo &createInfo={});
		static std::shared_ptr<Scene> Create(NodeManager &nodeManager,DataStream &dsIn,const std::string &rootDir,RenderMode renderMode,const CreateInfo &createInfo={});
		static std::shared_ptr<Scene> Create(NodeManager &nodeManager,DataStream &dsIn,const std::string &rootDir);
		static bool ReadHeaderInfo(DataStream &ds,RenderMode &outRenderMode,CreateInfo &outCreateInfo,SerializationData &outSerializationData,uint32_t &outVersion,SceneInfo *optOutSceneInfo=nullptr);
		//
		static Vector3 ToPragmaPosition(const ccl::float3 &pos);
		static ccl::float3 ToCyclesVector(const Vector3 &v);
		static ccl::float3 ToCyclesPosition(const Vector3 &pos);
		static ccl::float3 ToCyclesNormal(const Vector3 &n);
		static ccl::float2 ToCyclesUV(const Vector2 &uv);
		static ccl::Transform ToCyclesTransform(const umath::ScaledTransform &t,bool applyRotOffset=false);
		static float ToCyclesLength(float len);
		static std::string ToRelativePath(const std::string &absPath);
		static std::string ToAbsolutePath(const std::string &relPath);

		static constexpr uint32_t INPUT_CHANNEL_COUNT = 4u;
		static constexpr uint32_t OUTPUT_CHANNEL_COUNT = 4u;

		~Scene();
		void AddSkybox(const std::string &texture);
		Camera &GetCamera();
		bool IsValid() const;
		float GetProgress() const;
		RenderMode GetRenderMode() const;
		bool IsProgressive() const;
		bool IsProgressiveRefine() const;
		void StopRendering();
		ccl::Scene *operator->();
		ccl::Scene *operator*();

		const std::vector<PLight> &GetLights() const;
		std::vector<PLight> &GetLights();

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
		void SetAOBakeTarget(Object &o);
		std::vector<raytracing::TileManager::TileData> GetRenderedTileBatch();
		const TileManager &GetTileManager() const {return m_tileManager;}
		Vector2i GetTileSize() const;
		Vector2i GetResolution() const;

		const std::vector<std::shared_ptr<ModelCache>> &GetModelCaches() const {return m_mdlCaches;}
		void AddModelsFromCache(const ModelCache &cache);
		void AddLight(Light &light);
		
		void Reset();
		void Restart();
		util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> Finalize();

		DenoiseMode GetDenoiseMode() const {return m_createInfo.denoiseMode;}
		bool ShouldDenoise() const {return GetDenoiseMode() != DenoiseMode::None;}

		PMesh FindRenderMeshByHash(const util::MurmurHash3 &hash) const;
		PObject FindRenderObjectByHash(const util::MurmurHash3 &hash) const;

		std::shared_ptr<CCLShader> GetCachedShader(const GroupNodeDesc &desc) const;
		void AddShader(CCLShader &shader,const GroupNodeDesc *optDesc=nullptr);
		ccl::Session *GetCCLSession();
		std::optional<uint32_t> FindCCLObjectId(const Object &o) const;
		Object *FindObject(const std::string &objectName) const;
	private:
		enum class ImageRenderStage : uint8_t
		{
			InitializeScene = 0,
			Lighting,
			Albedo,
			Normal,
			Denoise,
			FinalizeImage,
			MergeStereoscopic,

			SceneAlbedo,
			SceneNormals,
			SceneDepth,

			Bake,

			Finalize
		};
		enum class RenderStageResult : uint8_t
		{
			Complete = 0,
			Continue
		};
		friend Shader;
		friend Object;
		friend Light;
		Scene(NodeManager &nodeManager,std::unique_ptr<ccl::Session> session,ccl::Scene &scene,RenderMode renderMode,DeviceType deviceType);
		float GetGamma() const;
		void PrepareCyclesSceneForRendering();
		void StartTextureBaking(SceneWorker &worker);
		void ReloadProgressiveRender(bool clearExposure=true,bool waitForPreviousCompletion=false);
		static ccl::ShaderOutput *FindShaderNodeOutput(ccl::ShaderNode &node,const std::string &output);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const std::string &nodeName);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const OpenImageIO_v2_1::ustring &name);
		void SetCancelled(const std::string &msg="Cancelled by application.");
		void WaitForRenderStage(SceneWorker &worker,float baseProgress,float progressMultiplier,const std::function<RenderStageResult()> &fOnComplete);
		RenderStageResult StartNextRenderImageStage(SceneWorker &worker,ImageRenderStage stage,StereoEye eyeStage);
		void InitializeAlbedoPass(bool reloadShaders);
		void InitializeNormalPass(bool reloadShaders);
		void InitializePassShaders(const std::function<std::shared_ptr<GroupNodeDesc>(const Shader&)> &fGetPassDesc);
		void ApplyPostProcessing(uimg::ImageBuffer &imgBuffer,RenderMode renderMode);
		void DenoiseHDRImageArea(uimg::ImageBuffer &imgBuffer,uint32_t imgWidth,uint32_t imgHeight,uint32_t x,uint32_t y,uint32_t w,uint32_t h) const;
		bool UpdateStereo(raytracing::SceneWorker &worker,ImageRenderStage stage,raytracing::StereoEye &eyeStage);
		bool IsValidTexture(const std::string &filePath) const;
		void CloseCyclesScene();
		void FinalizeAndCloseCyclesScene();
		std::shared_ptr<uimg::ImageBuffer> FinalizeCyclesScene();
		ccl::BufferParams GetBufferParameters() const;

		void SetupRenderSettings(
			ccl::Scene &scene,ccl::Session &session,ccl::BufferParams &bufferParams,RenderMode renderMode,
			uint32_t maxTransparencyBounces
		) const;

		void OnParallelWorkerCancelled();
		void Wait();
		friend SceneWorker;

		std::atomic<bool> m_progressiveRunning = false;
		std::condition_variable m_progressiveCondition {};
		std::mutex m_progressiveMutex {};
		std::shared_ptr<util::ocio::ColorProcessor> m_colorTransformProcessor = nullptr;

		std::atomic<uint32_t> m_restartState = 0;
		TileManager m_tileManager {};
		std::shared_ptr<NodeManager> m_nodeManager = nullptr;
		SceneInfo m_sceneInfo {};
		DeviceType m_deviceType = DeviceType::GPU;
		std::vector<std::shared_ptr<CCLShader>> m_cclShaders = {};
		std::unordered_map<const GroupNodeDesc*,size_t> m_shaderCache {};
		std::vector<std::shared_ptr<ModelCache>> m_mdlCaches {};
		std::vector<PLight> m_lights = {};
		std::unique_ptr<ccl::Session> m_session = nullptr;
		ccl::Scene &m_scene;
		CreateInfo m_createInfo {};
		PCamera m_camera = nullptr;
		StateFlags m_stateFlags = StateFlags::None;
		RenderMode m_renderMode = RenderMode::RenderImage;

		struct {
			std::shared_ptr<ShaderCache> shaderCache;
			std::shared_ptr<ModelCache> modelCache;
		} m_renderData;

		std::shared_ptr<uimg::ImageBuffer> &GetResultImageBuffer(StereoEye eye=StereoEye::Left);
		std::shared_ptr<uimg::ImageBuffer> &GetAlbedoImageBuffer(StereoEye eye=StereoEye::Left);
		std::shared_ptr<uimg::ImageBuffer> &GetNormalImageBuffer(StereoEye eye=StereoEye::Left);
		std::array<std::shared_ptr<uimg::ImageBuffer>,umath::to_integral(StereoEye::Count)> m_resultImageBuffer = {};
		std::array<std::shared_ptr<uimg::ImageBuffer>,umath::to_integral(StereoEye::Count)> m_normalImageBuffer = {};
		std::array<std::shared_ptr<uimg::ImageBuffer>,umath::to_integral(StereoEye::Count)> m_albedoImageBuffer = {};
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(raytracing::Scene::StateFlags)

#endif
