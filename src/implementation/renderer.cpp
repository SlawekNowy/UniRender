/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
*/

module;

#include <iostream>
#include <util_image_buffer.hpp>
#include <util_ocio.hpp>
#include <sharedutils/util_path.hpp>
#include <sharedutils/util_library.hpp>
#include <sharedutils/magic_enum.hpp>
#include <sharedutils/util_parallel_job.hpp>
#include <sharedutils/util_event_reply.hpp>
#include <util_image.hpp>
#include <fsys/ifile.hpp>
#include <udm.hpp>

module pragma.scenekit;

import :renderer;
import :scene;
import :denoise;
import :model_cache;
import :mesh;
import :object;
import :camera;
import :light;
import :shader;

pragma::scenekit::RenderWorker::RenderWorker(Renderer &renderer) : util::ParallelWorker<uimg::ImageLayerSet> {}, m_renderer {renderer.shared_from_this()} {}
void pragma::scenekit::RenderWorker::DoCancel(const std::string &resultMsg, std::optional<int32_t> resultCode)
{
	util::ParallelWorker<uimg::ImageLayerSet>::DoCancel(resultMsg, resultCode);
	m_renderer->OnParallelWorkerCancelled();
}
void pragma::scenekit::RenderWorker::Wait()
{
	util::ParallelWorker<uimg::ImageLayerSet>::Wait();
	m_renderer->Wait();
}
uimg::ImageLayerSet pragma::scenekit::RenderWorker::GetResult()
{
	uimg::ImageLayerSet result {};
	for(auto &pair : m_renderer->GetResultImageBuffers()) {
		auto &imgBuf = pair.second.front();
		if(!imgBuf)
			continue;
		result.images[std::string {magic_enum::enum_name(pair.first)}] = imgBuf;
	}
	return result;
}

///////////////////

static std::string g_moduleLookupLocation {};
void pragma::scenekit::set_module_lookup_location(const std::string &location) { g_moduleLookupLocation = location; }
static std::unordered_map<std::string, std::shared_ptr<util::Library>> g_rendererLibs {};
void pragma::scenekit::Renderer::Close()
{
	g_rendererLibs.clear();
	pragma::scenekit::set_log_handler();
}
std::shared_ptr<pragma::scenekit::Renderer> pragma::scenekit::Renderer::Create(const pragma::scenekit::Scene &scene, const std::string &rendererIdentifier, std::string &outErr, Flags flags)
{
	auto res = scene.GetResolution();
	if(res.x <= 0 || res.y <= 0) {
		outErr = "Illegal resolution " + std::to_string(res.x) + "x" + std::to_string(res.y) + ": Resolution must not be 0.";
		return nullptr;
	}
	pragma::scenekit::PRenderer renderer = nullptr;
	auto it = g_rendererLibs.find(rendererIdentifier);
	if(it == g_rendererLibs.end()) {
		auto moduleLocation = util::Path::CreatePath(util::get_program_path());
		moduleLocation += g_moduleLookupLocation + rendererIdentifier + "/";

		std::vector<std::string> additionalSearchDirectories;
		additionalSearchDirectories.push_back(moduleLocation.GetString());
		std::string err;
		auto libName = "UniRender_" + rendererIdentifier;
#ifdef __linux__
		libName = "lib" + libName;
#endif
		auto lib = util::Library::Load(moduleLocation.GetString() + libName, additionalSearchDirectories, &err);
		if(lib == nullptr) {
			std::cout << "Unable to load renderer module for '" << rendererIdentifier << "': " << err << std::endl;
			outErr = "Failed to load renderer module '" + rendererIdentifier + "/" + libName + "': " + err;
			return nullptr;
		}
		it = g_rendererLibs.insert(std::make_pair(rendererIdentifier, lib)).first;
	}
	auto &lib = it->second;
	auto *func = lib->FindSymbolAddress<bool (*)(const pragma::scenekit::Scene &, Flags, std::shared_ptr<pragma::scenekit::Renderer> &, std::string &)>("create_renderer");
	if(func == nullptr) {
		outErr = "Failed to locate symbol 'create_renderer' in renderer module!";
		return nullptr;
	}
	auto success = func(scene, flags, renderer, outErr);
	return renderer;
}
bool pragma::scenekit::Renderer::UnloadRendererLibrary(const std::string &rendererIdentifier)
{
	auto it = g_rendererLibs.find(rendererIdentifier);
	if(it == g_rendererLibs.end())
		return false;
	g_rendererLibs.erase(it);
	return true;
}

///////////////////

pragma::scenekit::Renderer::Renderer(const Scene &scene, Flags flags) : m_scene {const_cast<Scene &>(scene).shared_from_this()}, m_apiData {udm::Property::Create(udm::Type::Element)}, m_flags {flags} {}
std::pair<uint32_t, pragma::scenekit::PassType> pragma::scenekit::Renderer::AddPass(PassType passType)
{
	auto it = m_passes.find(passType);
	if(it == m_passes.end())
		it = m_passes.insert(std::make_pair(passType, m_nextOutputIndex++)).first;
	return {it->second, passType};
}
uimg::ImageBuffer *pragma::scenekit::Renderer::FindResultImageBuffer(PassType type, StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	auto it = m_resultImageBuffers.find(type);
	if(it == m_resultImageBuffers.end())
		return nullptr;
	return it->second.at(umath::to_integral(eye)).get();
}
std::shared_ptr<uimg::ImageBuffer> &pragma::scenekit::Renderer::GetResultImageBuffer(PassType type, StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	auto it = m_resultImageBuffers.find(type);
	if(it == m_resultImageBuffers.end())
		it = m_resultImageBuffers.insert(std::make_pair(type, std::array<std::shared_ptr<uimg::ImageBuffer>, umath::to_integral(StereoEye::Count)> {})).first;
	return it->second.at(umath::to_integral(eye));
}

void pragma::scenekit::Renderer::UpdateActorMap() { m_actorMap = m_scene->BuildActorMap(); }
bool pragma::scenekit::Renderer::IsFeatureEnabled(Feature feature) const { return false; }

pragma::scenekit::Renderer::RenderStageResult pragma::scenekit::Renderer::StartNextRenderStage(RenderWorker &worker, pragma::scenekit::Renderer::ImageRenderStage stage, StereoEye eyeStage)
{
	auto result = RenderStageResult::Continue;
	auto handled = HandleRenderStage(worker, stage, eyeStage, &result);
	return result;
}
void pragma::scenekit::Renderer::DumpImage(const std::string &renderStage, uimg::ImageBuffer &imgBuffer, uimg::ImageFormat format, const std::optional<std::string> &fileNameOverride) const
{
	filemanager::create_path("temp");
	auto fileName = fileNameOverride.has_value() ? *fileNameOverride : ("temp/render_output_" + renderStage + "." + uimg::get_file_extension(format));
	auto f = filemanager::open_file(fileName, filemanager::FileMode::Write | filemanager::FileMode::Binary);
	if(!f) {
		std::cout << "Failed to dump render stage image '" << renderStage << "': Could not open file '" << fileName << "' for writing!" << std::endl;
		return;
	}
	fsys::File fp {f};
	auto res = uimg::save_image(fp, imgBuffer, format);
	if(!res)
		std::cout << "Failed to dump render stage image '" << renderStage << "': Unknown error!" << std::endl;
}
bool pragma::scenekit::Renderer::ShouldDumpRenderStageImages() const
{
	auto apiData = GetApiData();
	auto dumpRenderStageImages = false;
	apiData.GetFromPath("debug/dumpRenderStageImages")(dumpRenderStageImages);
	return dumpRenderStageImages;
}
util::EventReply pragma::scenekit::Renderer::HandleRenderStage(RenderWorker &worker, pragma::scenekit::Renderer::ImageRenderStage stage, StereoEye eyeStage, pragma::scenekit::Renderer::RenderStageResult *optResult)
{
	switch(stage) {
	case ImageRenderStage::Denoise:
		{
			auto denoiseImg = [this, eyeStage, &worker](uimg::ImageBuffer &imgBuf, bool lightmap) {
				auto albedoImageBuffer = GetResultImageBuffer(PassType::Albedo, eyeStage);
				auto normalImageBuffer = GetResultImageBuffer(PassType::Normals, eyeStage);

				denoise::Info denoiseInfo {};
				denoiseInfo.width = imgBuf.GetWidth();
				denoiseInfo.height = imgBuf.GetHeight();
				denoiseInfo.lightmap = lightmap;
				denoise::denoise(denoiseInfo, imgBuf, albedoImageBuffer.get(), normalImageBuffer.get(), [this, &worker](float progress) -> bool { return !worker.IsCancelled(); });
			};
			if(Scene::IsLightmapRenderMode(m_scene->GetRenderMode())) {
				switch(m_scene->GetRenderMode()) {
				case Scene::RenderMode::BakeDiffuseLighting:
					denoiseImg(*GetResultImageBuffer(PassType::Diffuse, eyeStage), true);
					break;
				case Scene::RenderMode::BakeDiffuseLightingSeparate:
					denoiseImg(*GetResultImageBuffer(PassType::DiffuseDirect, eyeStage), true);
					denoiseImg(*GetResultImageBuffer(PassType::DiffuseIndirect, eyeStage), true);
					break;
				}
			}
			else {
				auto passType = get_main_pass_type(m_scene->GetRenderMode());
				assert(passType.has_value());
				if(passType.has_value()) {
					auto &resultImageBuffer = GetResultImageBuffer(*passType, eyeStage);

					static auto dbgAlbedo = false;
					static auto dbgNormals = false;
					if(dbgAlbedo)
						resultImageBuffer = GetResultImageBuffer(PassType::Albedo, eyeStage);
					else if(dbgNormals)
						resultImageBuffer = GetResultImageBuffer(PassType::Normals, eyeStage);
					else {
						denoiseImg(*resultImageBuffer, false);
						if(ShouldDumpRenderStageImages())
							DumpImage("denoise", *resultImageBuffer, uimg::ImageFormat::HDR);
					}
				}
			}

			if(UpdateStereoEye(worker, stage, eyeStage)) {
				if(optResult)
					*optResult = RenderStageResult::Continue;
				return util::EventReply::Handled;
			}

			return HandleRenderStage(worker, ImageRenderStage::FinalizeImage, eyeStage, optResult);
		}
	case ImageRenderStage::FinalizeImage:
		{
			for(auto &pair : m_resultImageBuffers) {
				auto &resultImageBuffer = pair.second[umath::to_integral((eyeStage != StereoEye::None) ? eyeStage : StereoEye::Left)];
				if(!resultImageBuffer)
					continue;
				if(ShouldDumpRenderStageImages())
					DumpImage("raw_output", *resultImageBuffer, uimg::ImageFormat::PNG);
				if(m_colorTransformProcessor) // TODO: Should we really apply color transform if we're not denoising?
				{
					std::string err;
					if(m_colorTransformProcessor->Apply(*resultImageBuffer, err) == false)
						m_scene->HandleError("Unable to apply color transform: " + err);
					if(ShouldDumpRenderStageImages())
						DumpImage("color_transform", *resultImageBuffer, uimg::ImageFormat::HDR);
				}
				if(!ShouldUseTransparentSky() || pragma::scenekit::Scene::IsLightmapRenderMode(m_scene->GetRenderMode()))
					resultImageBuffer->ClearAlpha();
				if(ShouldDumpRenderStageImages())
					DumpImage("alpha", *resultImageBuffer, uimg::ImageFormat::HDR);
				FinalizeImage(*resultImageBuffer, eyeStage);
			}
			if(eyeStage == StereoEye::Left) {
				if(optResult)
					*optResult = RenderStageResult::Continue;
				return util::EventReply::Handled;
			}
			if(eyeStage == StereoEye::Right)
				return HandleRenderStage(worker, ImageRenderStage::MergeStereoscopic, StereoEye::None, optResult);
			return HandleRenderStage(worker, ImageRenderStage::Finalize, StereoEye::None, optResult);
		}
	case ImageRenderStage::MergeStereoscopic:
		{
			auto passType = get_main_pass_type(m_scene->GetRenderMode());
			if(passType.has_value()) {
				auto &imgLeft = GetResultImageBuffer(*passType, StereoEye::Left);
				auto &imgRight = GetResultImageBuffer(*passType, StereoEye::Right);
				auto w = imgLeft->GetWidth();
				auto h = imgLeft->GetHeight();
				auto imgComposite = uimg::ImageBuffer::Create(w, h * 2, imgLeft->GetFormat());
				auto *dataSrcLeft = imgLeft->GetData();
				auto *dataSrcRight = imgRight->GetData();
				auto *dataDst = imgComposite->GetData();
				memcpy(dataDst, dataSrcLeft, imgLeft->GetSize());
				memcpy(static_cast<uint8_t *>(dataDst) + imgLeft->GetSize(), dataSrcRight, imgRight->GetSize());
				imgLeft = imgComposite;
				imgRight = nullptr;
			}
			return HandleRenderStage(worker, ImageRenderStage::Finalize, StereoEye::None, optResult);
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
void pragma::scenekit::Renderer::PrepareCyclesSceneForRendering()
{
	m_tileManager.SetUseFloatData(ShouldUseProgressiveFloatFormat());
	m_renderData.shaderCache = ShaderCache::Create();
	m_renderData.modelCache = ModelCache::Create();

	for(auto &mdlCache : m_scene->GetModelCaches())
		m_renderData.modelCache->Merge(*mdlCache);
	m_renderData.modelCache->Bake();

	m_scene->PrintLogInfo();
}
bool pragma::scenekit::Renderer::ShouldUseProgressiveFloatFormat() const { return true; }
bool pragma::scenekit::Renderer::ShouldUseTransparentSky() const { return m_scene->GetSceneInfo().transparentSky; }
bool pragma::scenekit::Renderer::IsDisplayDriverEnabled() const { return !umath::is_flag_set(static_cast<pragma::scenekit::Renderer::Flags>(m_flags), Flags::DisableDisplayDriver); }
bool pragma::scenekit::Renderer::IsBuildingKernels() const { return umath::is_flag_set(static_cast<pragma::scenekit::Renderer::Flags>(m_flags), Flags::CompilingKernels); }
void pragma::scenekit::Renderer::SetIsBuildingKernels(bool compiling)
{
	pragma::scenekit::Renderer::Flags flags = m_flags;
	if(compiling == umath::is_flag_set(flags, pragma::scenekit::Renderer::Flags::CompilingKernels))
		return;
	umath::set_flag(flags, pragma::scenekit::Renderer::Flags::CompilingKernels, compiling);
	m_flags = flags;
	auto &f = pragma::scenekit::get_kernel_compile_callback();
	if(f)
		f(compiling);
}
udm::PropertyWrapper pragma::scenekit::Renderer::GetApiData() const { return *m_apiData; }
pragma::scenekit::WorldObject *pragma::scenekit::Renderer::FindActor(const util::Uuid &uuid)
{
	auto it = m_actorMap.find(util::get_uuid_hash(uuid));
	if(it == m_actorMap.end())
		return nullptr;
	return it->second;
}
pragma::scenekit::PMesh pragma::scenekit::Renderer::FindRenderMeshByHash(const util::MurmurHash3 &hash) const
{
	// TODO: Do this via a lookup table
	for(auto &chunk : m_renderData.modelCache->GetChunks()) {
		for(auto &mesh : chunk.GetMeshes()) {
			if(mesh->GetHash() == hash)
				return mesh;
		}
	}
	return nullptr;
}
void pragma::scenekit::Renderer::StopRendering()
{
	m_progressiveCondition.notify_one();
	m_progressiveRunning = false;
}
pragma::scenekit::Object *pragma::scenekit::Renderer::FindObject(const std::string &objectName) const
{
	for(auto &chunk : m_renderData.modelCache->GetChunks()) {
		for(auto &obj : chunk.GetObjects()) {
			if(obj->GetName() == objectName)
				return obj.get();
		}
	}
	return nullptr;
}
void pragma::scenekit::Renderer::OnParallelWorkerCancelled()
{
	SetCancelled();
	// m_session->set_pause(true);
	// StopRendering();
}
std::vector<pragma::scenekit::TileManager::TileData> pragma::scenekit::Renderer::GetRenderedTileBatch() { return m_tileManager.GetRenderedTileBatch(); }
void pragma::scenekit::Renderer::AddActorToActorMap(WorldObject &obj) { Scene::AddActorToActorMap(m_actorMap, obj); }
bool pragma::scenekit::Renderer::Initialize()
{
	m_scene->GetCamera().Finalize(*m_scene);
	for(auto &light : m_scene->GetLights())
		light->Finalize(*m_scene);

	auto &mdlCache = m_renderData.modelCache;
	mdlCache->GenerateData();
	for(auto &chunk : mdlCache->GetChunks()) {
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
void pragma::scenekit::set_log_handler(const std::function<void(const std::string)> &logHandler) { g_logHandler = logHandler; }
const std::function<void(const std::string)> &pragma::scenekit::get_log_handler() { return g_logHandler; }
