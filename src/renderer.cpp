/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/renderer.hpp"
#include "util_raytracing/model_cache.hpp"
#include "util_raytracing/mesh.hpp"
#include "util_raytracing/light.hpp"
#include "util_raytracing/object.hpp"
#include "util_raytracing/scene.hpp"
#include "util_raytracing/camera.hpp"
#include "util_raytracing/shader.hpp"
#include "util_raytracing/denoise.hpp"
#include <util_image_buffer.hpp>
#include <util_ocio.hpp>

#pragma optimize("",off)
unirender::RenderWorker::RenderWorker(Renderer &renderer)
	: util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>{},m_renderer{renderer.shared_from_this()}
{}
void unirender::RenderWorker::DoCancel(const std::string &resultMsg)
{
	util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::DoCancel(resultMsg);
	m_renderer->OnParallelWorkerCancelled();
}
void unirender::RenderWorker::Wait()
{
	util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::Wait();
	m_renderer->Wait();
}
std::shared_ptr<uimg::ImageBuffer> unirender::RenderWorker::GetResult() {return m_renderer->GetResultImageBuffer();}

///////////////////

unirender::Renderer::Renderer(const Scene &scene)
	: m_scene{const_cast<Scene&>(scene).shared_from_this()}
{}
std::shared_ptr<uimg::ImageBuffer> &unirender::Renderer::GetResultImageBuffer(StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	return m_resultImageBuffer.at(umath::to_integral(eye));
}

std::shared_ptr<uimg::ImageBuffer> &unirender::Renderer::GetAlbedoImageBuffer(StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	return m_albedoImageBuffer.at(umath::to_integral(eye));
}

std::shared_ptr<uimg::ImageBuffer> &unirender::Renderer::GetNormalImageBuffer(StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	return m_normalImageBuffer.at(umath::to_integral(eye));
}
unirender::Renderer::RenderStageResult unirender::Renderer::StartNextRenderStage(RenderWorker &worker,unirender::Renderer::ImageRenderStage stage,StereoEye eyeStage)
{
	auto result = RenderStageResult::Continue;
	auto handled = HandleRenderStage(worker,stage,eyeStage,&result);
	return result;
}
util::EventReply unirender::Renderer::HandleRenderStage(RenderWorker &worker,unirender::Renderer::ImageRenderStage stage,StereoEye eyeStage,unirender::Renderer::RenderStageResult *optResult)
{
	switch(stage)
	{
	case ImageRenderStage::Denoise:
	{
		// Denoise
		DenoiseInfo denoiseInfo {};
		auto &resultImageBuffer = GetResultImageBuffer(eyeStage);
		denoiseInfo.hdr = true;
		denoiseInfo.width = resultImageBuffer->GetWidth();
		denoiseInfo.height = resultImageBuffer->GetHeight();

		static auto dbgAlbedo = false;
		static auto dbgNormals = false;
		if(dbgAlbedo)
			m_resultImageBuffer = m_albedoImageBuffer;
		else if(dbgNormals)
			m_resultImageBuffer = m_normalImageBuffer;
		else
		{
			auto &albedoImageBuffer = GetAlbedoImageBuffer(eyeStage);
			auto &normalImageBuffer = GetNormalImageBuffer(eyeStage);
			resultImageBuffer->Convert(uimg::ImageBuffer::Format::RGB_FLOAT);
			if(albedoImageBuffer)
				albedoImageBuffer->Convert(uimg::ImageBuffer::Format::RGB_FLOAT);
			if(normalImageBuffer)
				normalImageBuffer->Convert(uimg::ImageBuffer::Format::RGB_FLOAT);

			/*{
				auto f0 = FileManager::OpenFile<VFilePtrReal>("imgbuf.png","wb");
				if(f0)
					uimg::save_image(f0,*resultImageBuffer,uimg::ImageFormat::PNG);
			}
			{
				auto f0 = FileManager::OpenFile<VFilePtrReal>("imgbuf_albedo.png","wb");
				if(f0)
					uimg::save_image(f0,*albedoImageBuffer,uimg::ImageFormat::PNG);
			}
			{
				auto f0 = FileManager::OpenFile<VFilePtrReal>("imgbuf_normal.png","wb");
				if(f0)
					uimg::save_image(f0,*normalImageBuffer,uimg::ImageFormat::PNG);
			}*/

			denoise(denoiseInfo,*resultImageBuffer,albedoImageBuffer.get(),normalImageBuffer.get(),[this,&worker](float progress) -> bool {
				return !worker.IsCancelled();
			});
		}

		if(UpdateStereoEye(worker,stage,eyeStage))
		{
			if(optResult)
				*optResult = RenderStageResult::Continue;
			return util::EventReply::Handled;
		}

		return HandleRenderStage(worker,ImageRenderStage::FinalizeImage,eyeStage,optResult);
	}
	case ImageRenderStage::FinalizeImage:
	{
		auto &resultImageBuffer = GetResultImageBuffer(eyeStage);
		if(m_colorTransformProcessor) // TODO: Should we really apply color transform if we're not denoising?
		{
			std::string err;
			if(m_colorTransformProcessor->Apply(*resultImageBuffer,err,0.f,m_scene->GetGamma()) == false)
				m_scene->HandleError("Unable to apply color transform: " +err);
		}
		resultImageBuffer->Convert(uimg::ImageBuffer::Format::RGBA_HDR);
		resultImageBuffer->ClearAlpha();
		FinalizeImage(*resultImageBuffer);
		if(UpdateStereoEye(worker,stage,eyeStage))
		{
			if(optResult)
				*optResult = RenderStageResult::Continue;
			return util::EventReply::Handled;
		}
		if(eyeStage == StereoEye::Left)
			return HandleRenderStage(worker,ImageRenderStage::MergeStereoscopic,StereoEye::None,optResult);
		return HandleRenderStage(worker,ImageRenderStage::Finalize,StereoEye::None,optResult);
	}
	case ImageRenderStage::MergeStereoscopic:
	{
		auto &imgLeft = m_resultImageBuffer.at(umath::to_integral(StereoEye::Left));
		auto &imgRight = m_resultImageBuffer.at(umath::to_integral(StereoEye::Right));
		auto w = imgLeft->GetWidth();
		auto h = imgLeft->GetHeight();
		auto imgComposite = uimg::ImageBuffer::Create(w,h *2,imgLeft->GetFormat());
		auto *dataSrcLeft = imgLeft->GetData();
		auto *dataSrcRight = imgRight->GetData();
		auto *dataDst = imgComposite->GetData();
		memcpy(dataDst,dataSrcLeft,imgLeft->GetSize());
		memcpy(static_cast<uint8_t*>(dataDst) +imgLeft->GetSize(),dataSrcRight,imgRight->GetSize());
		m_resultImageBuffer.at(umath::to_integral(StereoEye::Left)) = imgComposite;
		m_resultImageBuffer.at(umath::to_integral(StereoEye::Right)) = nullptr;
		return HandleRenderStage(worker,ImageRenderStage::Finalize,StereoEye::None,optResult);
	}
	case ImageRenderStage::Finalize:
		// We're done here
		CloseRenderScene();
		if(optResult)
			*optResult = RenderStageResult::Complete;
		return util::EventReply::Handled;
	}
	return util::EventReply::Unhandled;
}
void unirender::Renderer::PrepareCyclesSceneForRendering()
{
	m_renderData.shaderCache = ShaderCache::Create();
	m_renderData.modelCache = ModelCache::Create();

	for(auto &mdlCache : m_scene->GetModelCaches())
		m_renderData.modelCache->Merge(*mdlCache);
	m_renderData.modelCache->Bake();
}
unirender::PMesh unirender::Renderer::FindRenderMeshByHash(const util::MurmurHash3 &hash) const
{
	// TODO: Do this via a lookup table
	for(auto &chunk : m_renderData.modelCache->GetChunks())
	{
		for(auto &mesh : chunk.GetMeshes())
		{
			if(mesh->GetHash() == hash)
				return mesh;
		}
	}
	return nullptr;
}
void unirender::Renderer::StopRendering()
{
	m_progressiveCondition.notify_one();
	m_progressiveRunning = false;
}
void unirender::Renderer::OnParallelWorkerCancelled()
{
	SetCancelled();
	// m_session->set_pause(true);
	// StopRendering();
}
std::vector<unirender::TileManager::TileData> unirender::Renderer::GetRenderedTileBatch() {return m_tileManager.GetRenderedTileBatch();}
bool unirender::Renderer::Initialize()
{
	m_scene->GetCamera().Finalize(*m_scene);
	for(auto &light : m_scene->GetLights())
		light->Finalize(*m_scene);

	auto &mdlCache = m_renderData.modelCache;
	mdlCache->GenerateData();
	for(auto &chunk : mdlCache->GetChunks())
	{
		for(auto &o : chunk.GetObjects())
			o->Finalize(*m_scene);
		for(auto &o : chunk.GetMeshes())
			o->Finalize(*m_scene);
	}
	for(auto &shader : m_renderData.shaderCache->GetShaders())
		shader->Finalize();
	return true;
}
#pragma optimize("",on)
