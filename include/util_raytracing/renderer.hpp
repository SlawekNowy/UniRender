/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __UNIRENDER_RENDERER_HPP__
#define __UNIRENDER_RENDERER_HPP__

#include "definitions.hpp"
#include "util_raytracing/tilemanager.hpp"
#include <sharedutils/util_parallel_job.hpp>
#include <sharedutils/util.h>
#include <cinttypes>

namespace unirender
{
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
	class DLLRTUTIL Renderer
		: public std::enable_shared_from_this<Renderer>
	{
	public:
		enum class StereoEye : uint8_t
		{
			Left = 0,
			Right,
			Count,

			None = std::numeric_limits<uint8_t>::max()
		};

		virtual ~Renderer()=default;
		virtual void Wait()=0;
		virtual void Start()=0;
		virtual float GetProgress() const=0;
		virtual void Reset()=0;
		virtual void Restart()=0;
		virtual util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> StartRender()=0;
		void StopRendering();

		std::shared_ptr<Mesh> FindRenderMeshByHash(const util::MurmurHash3 &hash) const;

		Scene &GetScene() {return *m_scene;}
		const Scene &GetScene() const {return const_cast<Renderer*>(this)->GetScene();}
		TileManager &GetTileManager() {return m_tileManager;}
		const TileManager &GetTileManager() const {return const_cast<Renderer*>(this)->GetTileManager();}
		std::vector<unirender::TileManager::TileData> GetRenderedTileBatch();
	protected:
		Renderer(const Scene &scene);
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
		virtual void PrepareCyclesSceneForRendering();
		virtual void SetCancelled(const std::string &msg="Cancelled by application.")=0;

		std::shared_ptr<Scene> m_scene = nullptr;
		TileManager m_tileManager {};

		struct {
			std::shared_ptr<ShaderCache> shaderCache;
			std::shared_ptr<ModelCache> modelCache;
		} m_renderData;
		
		std::atomic<bool> m_progressiveRunning = false;
		std::condition_variable m_progressiveCondition {};
		std::mutex m_progressiveMutex {};
		std::shared_ptr<util::ocio::ColorProcessor> m_colorTransformProcessor = nullptr;
		std::shared_ptr<uimg::ImageBuffer> &GetResultImageBuffer(StereoEye eye=StereoEye::Left);
		std::shared_ptr<uimg::ImageBuffer> &GetAlbedoImageBuffer(StereoEye eye=StereoEye::Left);
		std::shared_ptr<uimg::ImageBuffer> &GetNormalImageBuffer(StereoEye eye=StereoEye::Left);
		std::array<std::shared_ptr<uimg::ImageBuffer>,umath::to_integral(StereoEye::Count)> m_resultImageBuffer = {};
		std::array<std::shared_ptr<uimg::ImageBuffer>,umath::to_integral(StereoEye::Count)> m_normalImageBuffer = {};
		std::array<std::shared_ptr<uimg::ImageBuffer>,umath::to_integral(StereoEye::Count)> m_albedoImageBuffer = {};
	};
};

#endif
