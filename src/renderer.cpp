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
