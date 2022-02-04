/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
*/

#ifndef __UNIRENDER_RENDERER_HPP__
#define __UNIRENDER_RENDERER_HPP__

#include "definitions.hpp"
#include "util_raytracing/tilemanager.hpp"
#include <sharedutils/util_parallel_job.hpp>
#include <sharedutils/util.h>
#include <sharedutils/util_event_reply.hpp>
#include <udm_types.hpp>
#include <cinttypes>

namespace unirender
{
	DLLRTUTIL void set_log_handler(const std::function<void(const std::string)> &logHandler=nullptr);
	DLLRTUTIL const std::function<void(const std::string)> &get_log_handler();

	DLLRTUTIL void set_module_lookup_location(const std::string &location);

	class ModelCache;
	class ShaderCache;
	class Renderer;
	using PRenderer = std::shared_ptr<Renderer>;
	class DLLRTUTIL RenderWorker
		: public util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>
	{
	public:
		friend Renderer;
		RenderWorker(Renderer &renderer);
		using util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::Cancel;
		virtual void Wait() override;
		virtual std::shared_ptr<uimg::ImageBuffer> GetResult() override;

		using util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::SetResultMessage;
		using util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::AddThread;
		using util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::UpdateProgress;
	private:
		virtual void DoCancel(const std::string &resultMsg) override;
		PRenderer m_renderer = nullptr;
		template<typename TJob,typename... TARGS>
			friend util::ParallelJob<typename TJob::RESULT_TYPE> util::create_parallel_job(TARGS&& ...args);
	};

	class Scene;
	class Mesh;
	class Shader;
	class Object;
	class DLLRTUTIL Renderer
		: public std::enable_shared_from_this<Renderer>
	{
	public:
		enum class Flags : uint32_t
		{
			None = 0u,
			EnableLiveEditing = 1u
		};
		enum class StereoEye : uint8_t
		{
			Left = 0,
			Right,
			Count,

			None = std::numeric_limits<uint8_t>::max()
		};
		static std::shared_ptr<Renderer> Create(const unirender::Scene &scene,const std::string &rendererIdentifier,Flags flags=Flags::None);
		static bool UnloadRendererLibrary(const std::string &rendererIdentifier);
		static void Close();
		static constexpr const char *OUTPUT_COLOR = "COLOR";
		static constexpr const char *OUTPUT_ALBEDO = "ALBEDO";
		static constexpr const char *OUTPUT_NORMAL = "NORMAL";
		static constexpr const char *OUTPUT_DEPTH = "DEPTH";

		virtual ~Renderer()=default;
		virtual void Wait()=0;
		virtual void Start()=0;
		virtual float GetProgress() const=0;
		virtual void Reset()=0;
		virtual void Restart()=0;
		virtual bool Stop()=0;
		virtual bool Pause()=0;
		virtual bool Resume()=0;
		virtual bool Suspend()=0;
		virtual bool BeginSceneEdit() const {return false;}
		virtual bool EndSceneEdit() const {return false;}
		virtual bool Export(const std::string &path)=0;
		virtual std::optional<std::string> SaveRenderPreview(const std::string &path,std::string &outErr) const=0;
		virtual util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> StartRender()=0;
		void StopRendering();

		std::shared_ptr<Mesh> FindRenderMeshByHash(const util::MurmurHash3 &hash) const;
		udm::PropertyWrapper GetApiData() const;

		bool ShouldUseProgressiveFloatFormat() const;
		bool ShouldUseTransparentSky() const;
		Scene &GetScene() {return *m_scene;}
		const Scene &GetScene() const {return const_cast<Renderer*>(this)->GetScene();}
		TileManager &GetTileManager() {return m_tileManager;}
		const TileManager &GetTileManager() const {return const_cast<Renderer*>(this)->GetTileManager();}
		std::vector<unirender::TileManager::TileData> GetRenderedTileBatch();
	protected:
		Renderer(const Scene &scene);
		bool Initialize();
		Object *FindObject(const std::string &objectName) const;
		friend RenderWorker;
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
		void OnParallelWorkerCancelled();
		unirender::Renderer::RenderStageResult StartNextRenderStage(RenderWorker &worker,unirender::Renderer::ImageRenderStage stage,StereoEye eyeStage);
		virtual util::EventReply HandleRenderStage(RenderWorker &worker,unirender::Renderer::ImageRenderStage stage,StereoEye eyeStage,unirender::Renderer::RenderStageResult *optResult=nullptr);
		virtual void PrepareCyclesSceneForRendering();
		virtual bool UpdateStereoEye(unirender::RenderWorker &worker,unirender::Renderer::ImageRenderStage stage,StereoEye &eyeStage)=0;
		virtual void SetCancelled(const std::string &msg="Cancelled by application.")=0;
		virtual void CloseRenderScene()=0;
		virtual void FinalizeImage(uimg::ImageBuffer &imgBuf,StereoEye eyeStage) {};
		std::pair<uint32_t,std::string> AddOutput(const std::string &type);

		std::shared_ptr<Scene> m_scene = nullptr;
		TileManager m_tileManager {};
		udm::PProperty m_apiData = nullptr;

		struct {
			std::shared_ptr<ShaderCache> shaderCache;
			std::shared_ptr<ModelCache> modelCache;
		} m_renderData;
		
		std::atomic<bool> m_progressiveRunning = false;
		std::condition_variable m_progressiveCondition {};
		std::mutex m_progressiveMutex {};
		std::shared_ptr<util::ocio::ColorProcessor> m_colorTransformProcessor = nullptr;

		std::shared_ptr<uimg::ImageBuffer> &GetResultImageBuffer(const std::string &type,StereoEye eye=StereoEye::Left);
		uimg::ImageBuffer *FindResultImageBuffer(const std::string &type,StereoEye eye=StereoEye::Left);
		std::unordered_map<std::string,std::array<std::shared_ptr<uimg::ImageBuffer>,umath::to_integral(StereoEye::Count)>> m_resultImageBuffers = {};
		std::unordered_map<std::string,uint32_t> m_outputs {};
		uint32_t m_nextOutputIndex = 0;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(unirender::Renderer::Flags)

#endif
