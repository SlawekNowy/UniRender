/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
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
#include <sharedutils/util_path.hpp>
#include <sharedutils/util_library.hpp>

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
std::shared_ptr<uimg::ImageBuffer> unirender::RenderWorker::GetResult() {return m_renderer->GetResultImageBuffer(unirender::Renderer::OUTPUT_COLOR);}

///////////////////

static std::string g_moduleLookupLocation {};
void unirender::set_module_lookup_location(const std::string &location) {g_moduleLookupLocation = location;}
static std::unordered_map<std::string,std::shared_ptr<util::Library>> g_rendererLibs {};
void unirender::Renderer::Close()
{
	g_rendererLibs.clear();
	unirender::set_log_handler();
}
std::shared_ptr<unirender::Renderer> unirender::Renderer::Create(const unirender::Scene &scene,const std::string &rendererIdentifier,Flags flags)
{
	auto res = scene.GetResolution();
	if(res.x <= 0 || res.y <= 0)
		return nullptr;
	unirender::PRenderer renderer = nullptr;
	auto it = g_rendererLibs.find(rendererIdentifier);
	if(it == g_rendererLibs.end())
	{
		auto moduleLocation = util::Path::CreatePath(util::get_program_path());
		moduleLocation += g_moduleLookupLocation +rendererIdentifier +"/";

		std::vector<std::string> additionalSearchDirectories;
		additionalSearchDirectories.push_back(moduleLocation.GetString());
		std::string err;
		auto lib = util::Library::Load(moduleLocation.GetString() +"UniRender_" +rendererIdentifier,additionalSearchDirectories,&err);
		if(lib == nullptr)
		{
			std::cout<<"Unable to load renderer module for '"<<rendererIdentifier<<"': "<<err<<std::endl;
			return nullptr;
		}
		it = g_rendererLibs.insert(std::make_pair(rendererIdentifier,lib)).first;
	}
	auto &lib = it->second;
	auto *func = lib->FindSymbolAddress<bool(*)(const unirender::Scene&,Flags,std::shared_ptr<unirender::Renderer>&)>("create_renderer");
	if(func == nullptr)
		return nullptr;
	auto success = func(scene,flags,renderer);
	return renderer;
}
bool unirender::Renderer::UnloadRendererLibrary(const std::string &rendererIdentifier)
{
	auto it = g_rendererLibs.find(rendererIdentifier);
	if(it == g_rendererLibs.end())
		return false;
	g_rendererLibs.erase(it);
	return true;
}

///////////////////

unirender::Renderer::Renderer(const Scene &scene)
	: m_scene{const_cast<Scene&>(scene).shared_from_this()},
	m_apiData{udm::Property::Create(udm::Type::Element)}
{}
std::pair<uint32_t,std::string> unirender::Renderer::AddOutput(const std::string &type)
{
	auto it = m_outputs.find(type);
	if(it == m_outputs.end())
		it = m_outputs.insert(std::make_pair(type,m_nextOutputIndex++)).first;
	return {it->second,type};
}
uimg::ImageBuffer *unirender::Renderer::FindResultImageBuffer(const std::string &type,StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	auto it = m_resultImageBuffers.find(type);
	if(it == m_resultImageBuffers.end())
		return nullptr;
	return it->second.at(umath::to_integral(eye)).get();
}
std::shared_ptr<uimg::ImageBuffer> &unirender::Renderer::GetResultImageBuffer(const std::string &type,StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	auto it = m_resultImageBuffers.find(type);
	if(it == m_resultImageBuffers.end())
		it = m_resultImageBuffers.insert(std::make_pair(type,std::array<std::shared_ptr<uimg::ImageBuffer>,umath::to_integral(StereoEye::Count)>{})).first;
	return it->second.at(umath::to_integral(eye));
}

void unirender::Renderer::UpdateActorMap() {m_actorMap = m_scene->BuildActorMap();}

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
		auto &resultImageBuffer = GetResultImageBuffer(OUTPUT_COLOR,eyeStage);
		denoiseInfo.hdr = true;
		denoiseInfo.width = resultImageBuffer->GetWidth();
		denoiseInfo.height = resultImageBuffer->GetHeight();
		denoiseInfo.lightmap = (m_scene->GetRenderMode() == unirender::Scene::RenderMode::BakeDiffuseLighting);

		static auto dbgAlbedo = false;
		static auto dbgNormals = false;
		if(dbgAlbedo)
			resultImageBuffer = GetResultImageBuffer(OUTPUT_ALBEDO,eyeStage);
		else if(dbgNormals)
			resultImageBuffer = GetResultImageBuffer(OUTPUT_NORMAL,eyeStage);
		else
		{
			auto albedoImageBuffer = GetResultImageBuffer(OUTPUT_ALBEDO,eyeStage);
			auto normalImageBuffer = GetResultImageBuffer(OUTPUT_NORMAL,eyeStage);
			resultImageBuffer->Convert(uimg::Format::RGB_FLOAT);
			if(albedoImageBuffer)
				albedoImageBuffer->Convert(uimg::Format::RGB_FLOAT);
			if(normalImageBuffer)
				normalImageBuffer->Convert(uimg::Format::RGB_FLOAT);

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
		auto &resultImageBuffer = GetResultImageBuffer(OUTPUT_COLOR,eyeStage);
		resultImageBuffer->Convert(uimg::Format::RGBA_HDR);
		if(m_colorTransformProcessor) // TODO: Should we really apply color transform if we're not denoising?
		{
			std::string err;
			if(m_colorTransformProcessor->Apply(*resultImageBuffer,err) == false)
				m_scene->HandleError("Unable to apply color transform: " +err);
		}
		resultImageBuffer->ClearAlpha();
		FinalizeImage(*resultImageBuffer,eyeStage);
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
		auto &imgLeft = GetResultImageBuffer(OUTPUT_COLOR,StereoEye::Left);
		auto &imgRight = GetResultImageBuffer(OUTPUT_COLOR,StereoEye::Right);
		auto w = imgLeft->GetWidth();
		auto h = imgLeft->GetHeight();
		auto imgComposite = uimg::ImageBuffer::Create(w,h *2,imgLeft->GetFormat());
		auto *dataSrcLeft = imgLeft->GetData();
		auto *dataSrcRight = imgRight->GetData();
		auto *dataDst = imgComposite->GetData();
		memcpy(dataDst,dataSrcLeft,imgLeft->GetSize());
		memcpy(static_cast<uint8_t*>(dataDst) +imgLeft->GetSize(),dataSrcRight,imgRight->GetSize());
		imgLeft = imgComposite;
		imgRight = nullptr;
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
	m_tileManager.SetUseFloatData(ShouldUseProgressiveFloatFormat());
	m_renderData.shaderCache = ShaderCache::Create();
	m_renderData.modelCache = ModelCache::Create();

	for(auto &mdlCache : m_scene->GetModelCaches())
		m_renderData.modelCache->Merge(*mdlCache);
	m_renderData.modelCache->Bake();

	m_scene->PrintLogInfo();
}
bool unirender::Renderer::ShouldUseProgressiveFloatFormat() const {return true;}
bool unirender::Renderer::ShouldUseTransparentSky() const {return m_scene->GetSceneInfo().transparentSky;}
udm::PropertyWrapper unirender::Renderer::GetApiData() const {return *m_apiData;}
unirender::WorldObject *unirender::Renderer::FindActor(const util::Uuid &uuid)
{
	auto it = m_actorMap.find(util::get_uuid_hash(uuid));
	if(it == m_actorMap.end())
		return nullptr;
	return it->second;
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
unirender::Object *unirender::Renderer::FindObject(const std::string &objectName) const
{
	for(auto &chunk : m_renderData.modelCache->GetChunks())
	{
		for(auto &obj : chunk.GetObjects())
		{
			if(obj->GetName() == objectName)
				return obj.get();
		}
	}
	return nullptr;
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
static std::function<void(const std::string)> g_logHandler = nullptr;
void unirender::set_log_handler(const std::function<void(const std::string)> &logHandler) {g_logHandler = logHandler;}
const std::function<void(const std::string)> &unirender::get_log_handler() {return g_logHandler;}
#pragma optimize("",on)
