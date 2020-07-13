/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __UTIL_RAYTRACING_SCENE_HPP__
#define __UTIL_RAYTRACING_SCENE_HPP__

#include "definitions.hpp"
#include <sharedutils/util_weak_handle.hpp>
#include <sharedutils/util_parallel_job.hpp>
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
class DataStream;
namespace raytracing
{
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

	class CCLShader;
	class DLLRTUTIL Scene
		: public std::enable_shared_from_this<Scene>
	{
	public:
		struct DenoiseInfo
		{
			uint32_t numThreads = 16;
			uint32_t width = 0;
			uint32_t height = 0;
			bool hdr = false;
			bool lightmap = false;
		};
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
			DenoiseResult = 1u,
			OutputResultWithHDRColors = DenoiseResult<<1u,
			SkyInitialized = OutputResultWithHDRColors<<1u
		};

		struct CreateInfo
		{
			std::optional<uint32_t> samples = {};
			bool hdrOutput = false;
			bool denoise = true;
			DeviceType deviceType = DeviceType::GPU;
		};
		static bool IsRenderSceneMode(RenderMode renderMode);
		static void SetKernelPath(const std::string &kernelPath);
		static std::shared_ptr<Scene> Create(RenderMode renderMode,const CreateInfo &createInfo={});
		static std::shared_ptr<Scene> Create(DataStream &ds,RenderMode renderMode,const CreateInfo &createInfo={});
		static std::shared_ptr<Scene> Create(DataStream &ds);
		static bool ReadHeaderInfo(DataStream &ds,RenderMode &outRenderMode,CreateInfo &outCreateInfo,SerializationData &outSerializationData,SceneInfo *optOutSceneInfo=nullptr);
		//
		static Vector3 ToPragmaPosition(const ccl::float3 &pos);
		static ccl::float3 ToCyclesVector(const Vector3 &v);
		static ccl::float3 ToCyclesPosition(const Vector3 &pos);
		static ccl::float3 ToCyclesNormal(const Vector3 &n);
		static ccl::float2 ToCyclesUV(const Vector2 &uv);
		static ccl::Transform ToCyclesTransform(const umath::ScaledTransform &t);
		static float ToCyclesLength(float len);
		static std::string ToRelativePath(const std::string &absPath);
		static std::string ToAbsolutePath(const std::string &relPath);
		static bool Denoise(
			const DenoiseInfo &denoise,float *inOutData,
			float *optAlbedoData=nullptr,float *optInNormalData=nullptr,
			const std::function<bool(float)> &fProgressCallback=nullptr
		);

		static constexpr uint32_t INPUT_CHANNEL_COUNT = 4u;
		static constexpr uint32_t OUTPUT_CHANNEL_COUNT = 4u;

		~Scene();
		void AddSkybox(const std::string &texture);
		Camera &GetCamera();
		float GetProgress() const;
		RenderMode GetRenderMode() const;
		ccl::Scene *operator->();
		ccl::Scene *operator*();

		const std::vector<PShader> &GetShaders() const;
		std::vector<PShader> &GetShaders();
		const std::vector<PObject> &GetObjects() const;
		std::vector<PObject> &GetObjects();
		const std::vector<PLight> &GetLights() const;
		std::vector<PLight> &GetLights();
		const std::vector<PMesh> &GetMeshes() const;
		std::vector<PMesh> &GetMeshes();

		void SetLightIntensityFactor(float f);
		float GetLightIntensityFactor() const;

		static void SetVerbose(bool verbose);
		static bool IsVerbose();

		static bool ReadSerializationHeader(DataStream &dsIn,RenderMode &outRenderMode,CreateInfo &outCreateInfo,SerializationData &outSerializationData,SceneInfo *optOutSceneInfo=nullptr);
		void Serialize(DataStream &dsOut,const SerializationData &serializationData) const;
		bool Deserialize(DataStream &dsIn);

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

		util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> Finalize();

		void AddShader(CCLShader &shader);
		ccl::Session *GetCCLSession();
	private:
		friend Shader;
		friend Object;
		friend Light;
		Scene(std::unique_ptr<ccl::Session> session,ccl::Scene &scene,RenderMode renderMode,DeviceType deviceType);
		static ccl::ShaderOutput *FindShaderNodeOutput(ccl::ShaderNode &node,const std::string &output);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const std::string &nodeName);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const OpenImageIO_v2_1::ustring &name);
		void InitializeAlbedoPass(bool reloadShaders);
		void InitializeNormalPass(bool reloadShaders);
		void ApplyPostProcessing(uimg::ImageBuffer &imgBuffer,RenderMode renderMode);
		void DenoiseHDRImageArea(uimg::ImageBuffer &imgBuffer,uint32_t imgWidth,uint32_t imgHeight,uint32_t x,uint32_t y,uint32_t w,uint32_t h) const;
		bool Denoise(
			const DenoiseInfo &denoise,uimg::ImageBuffer &imgBuffer,
			uimg::ImageBuffer *optImgBufferAlbedo=nullptr,
			uimg::ImageBuffer *optImgBufferNormal=nullptr,
			const std::function<bool(float)> &fProgressCallback=nullptr
		) const;
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

		SceneInfo m_sceneInfo {};
		DeviceType m_deviceType = DeviceType::GPU;
		std::vector<PShader> m_shaders = {};
		std::vector<std::shared_ptr<CCLShader>> m_cclShaders = {};
		std::vector<PMesh> m_meshes = {};
		std::vector<PObject> m_objects = {};
		std::vector<PLight> m_lights = {};
		std::unique_ptr<ccl::Session> m_session = nullptr;
		ccl::Scene &m_scene;
		CreateInfo m_createInfo {};
		PCamera m_camera = nullptr;
		StateFlags m_stateFlags = StateFlags::None;
		RenderMode m_renderMode = RenderMode::RenderImage;
		std::weak_ptr<Object> m_bakeTarget = {};
		std::shared_ptr<uimg::ImageBuffer> m_resultImageBuffer = nullptr;
		std::shared_ptr<uimg::ImageBuffer> m_normalImageBuffer = nullptr;
		std::shared_ptr<uimg::ImageBuffer> m_albedoImageBuffer = nullptr;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(raytracing::Scene::StateFlags)

#endif
