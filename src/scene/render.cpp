/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/scene.hpp"
#include "util_raytracing/mesh.hpp"
#include "util_raytracing/camera.hpp"
#include "util_raytracing/shader.hpp"
#include "util_raytracing/ccl_shader.hpp"
#include "util_raytracing/object.hpp"
#include "util_raytracing/light.hpp"
#include "util_raytracing/baking.hpp"
#include "util_raytracing/denoise.hpp"
#include "util_raytracing/model_cache.hpp"
#include "util_raytracing/color_management.hpp"
#include <render/buffers.h>
#include <render/scene.h>
#include <render/session.h>
#include <render/shader.h>
#include <render/camera.h>
#include <render/light.h>
#include <render/mesh.h>
#include <render/graph.h>
#include <render/nodes.h>
#include <render/object.h>
#include <render/background.h>
#include <render/integrator.h>
#include <render/svm.h>
#include <render/bake.h>
#include <render/particles.h>
#include <util/util_path.h>
#include <util_ocio.hpp>

#pragma optimize("",off)
void raytracing::Scene::PrepareCyclesSceneForRendering()
{
	if(IsBakingSceneMode(m_renderMode))
	{
		auto *light = new ccl::Light{}; // Object will be removed automatically by cycles
		light->set_tfm(ccl::transform_identity());

		m_scene.lights.push_back(light);
		light->set_strength({1.f,1.f,1.f});
		light->set_co({0.f,0.f,0.f});
		light->set_dir({0.f,0.f,0.f});
		light->set_size(0.f);
		light->set_angle(0.f);
		light->set_light_type(ccl::LightType::LIGHT_BACKGROUND);
		light->set_map_resolution(2'048);
		light->set_shader(m_scene.default_background);
		light->set_use_mis(true);
		light->set_max_bounces(1'024);
		light->set_samples(1);
		light->tag_update(&m_scene);

		m_scene.background->set_ao_factor(1.f);
	}
	else if(m_sceneInfo.sky.empty() == false)
		AddSkybox(m_sceneInfo.sky);

	m_sceneInfo.exposure = m_createInfo.exposure;

	m_stateFlags |= StateFlags::HasRenderingStarted;
	auto bufferParams = GetBufferParameters();

	m_session->scene = &m_scene;
	m_session->reset(bufferParams,m_session->params.samples);

	/*auto *cam = **m_camera;
	cam->set_full_width(1920);
	cam->set_full_height(1080);
	cam->set_fov(0.399596483);
	cam->set_shuttertime(0.500000000);
	cam = m_scene.dicing_camera;
	cam->set_full_width(1920);
	cam->set_full_height(1080);
	cam->set_fov(0.399596483);
	cam->set_shuttertime(0.500000000);*/

	m_camera->Finalize(*this);

	m_renderData.shaderCache = ShaderCache::Create();
	m_renderData.modelCache = ModelCache::Create();

	for(auto &mdlCache : m_mdlCaches)
		m_renderData.modelCache->Merge(*mdlCache);
	m_renderData.modelCache->Bake();
	
	auto &mdlCache = m_renderData.modelCache;
	mdlCache->GenerateData();
	uint32_t numObjects = 0;
	uint32_t numMeshes = 0;
	for(auto &chunk : mdlCache->GetChunks())
	{
		numObjects += chunk.GetObjects().size();
		numMeshes += chunk.GetMeshes().size();
	}
	m_scene.objects.reserve(m_scene.objects.size() +numObjects);
	m_scene.geometry.reserve(m_scene.geometry.size() +numMeshes);

	// Note: Lights and objects have to be initialized before shaders, because they may
	// create additional shaders.
	m_scene.lights.reserve(m_lights.size());
	for(auto &light : m_lights)
	{
		light->Finalize(*this);
		m_scene.lights.push_back(**light);
	}
	for(auto &chunk : mdlCache->GetChunks())
	{
		for(auto &o : chunk.GetObjects())
			o->Finalize(*this);
		for(auto &o : chunk.GetMeshes())
			o->Finalize(*this);
	}
	for(auto &shader : m_renderData.shaderCache->GetShaders())
		shader->Finalize();
	for(auto &cclShader : m_cclShaders)
		cclShader->Finalize(*this);

	constexpr auto validate = false;
	if constexpr(validate)
	{
		for(auto &chunk : m_renderData.modelCache->GetChunks())
		{
			for(auto &o : chunk.GetObjects())
			{
				auto &mesh = o->GetMesh();
				mesh.Validate();
			}
		}
	}

	if(m_createInfo.colorTransform.has_value())
	{
		std::string err;
		ColorTransformProcessorCreateInfo createInfo {};
		createInfo.config = m_createInfo.colorTransform->config;
		createInfo.lookName = m_createInfo.colorTransform->lookName;
		m_colorTransformProcessor = create_color_transform_processor(createInfo,err);
		if(m_colorTransformProcessor == nullptr)
			HandleError("Unable to initialize color transform processor: " +err);
	}

	if(m_createInfo.progressive || IsBakingSceneMode(m_renderMode))
	{
		auto w = m_scene.camera->get_full_width();
		auto h = m_scene.camera->get_full_height();
		m_tileManager.Initialize(w,h,GetTileSize().x,GetTileSize().y,m_deviceType == DeviceType::CPU,m_createInfo.exposure,GetGamma(),m_colorTransformProcessor.get());
		bool flipHorizontally = true;
		if(m_scene.camera->get_camera_type() == ccl::CameraType::CAMERA_PANORAMA)
		{
			switch(m_scene.camera->get_panorama_type())
			{
			case ccl::PanoramaType::PANORAMA_EQUIRECTANGULAR:
			case ccl::PanoramaType::PANORAMA_FISHEYE_EQUIDISTANT:
				flipHorizontally = false; // I have no idea why some types have to be flipped and others don't
				break;
			}
		}
		m_tileManager.SetFlipImage(flipHorizontally,true);
		m_tileManager.SetExposure(m_sceneInfo.exposure);
	}
}

static void get_bake_shader_type(raytracing::Scene::RenderMode renderMode,ccl::ShaderEvalType &shaderType,int &bakePassFilter)
{
	switch(renderMode)
	{
	case raytracing::Scene::RenderMode::BakeAmbientOcclusion:
		shaderType = ccl::ShaderEvalType::SHADER_EVAL_AO;
		bakePassFilter = ccl::BAKE_FILTER_AO;
		break;
	case raytracing::Scene::RenderMode::BakeDiffuseLighting:
		shaderType = ccl::ShaderEvalType::SHADER_EVAL_DIFFUSE;//ccl::ShaderEvalType::SHADER_EVAL_DIFFUSE;
		bakePassFilter = 510 &~ccl::BAKE_FILTER_COLOR &~ccl::BAKE_FILTER_GLOSSY;//ccl::BakePassFilterCombos::BAKE_FILTER_COMBINED;//ccl::BAKE_FILTER_DIFFUSE | ccl::BAKE_FILTER_INDIRECT | ccl::BAKE_FILTER_DIRECT;
		break;
	case raytracing::Scene::RenderMode::BakeNormals:
		shaderType = ccl::ShaderEvalType::SHADER_EVAL_NORMAL;
		bakePassFilter = 0;
		break;
	}
}
static void validate_session(ccl::Scene &scene)
{
	for(auto *shader : scene.shaders)
	{
		if(shader->graph == nullptr)
			throw std::logic_error{"Found shader with invalid graph!"};
	}
}
raytracing::Scene::RenderStageResult raytracing::Scene::StartNextRenderImageStage(SceneWorker &worker,ImageRenderStage stage,StereoEye eyeStage)
{
	switch(stage)
	{
	case ImageRenderStage::InitializeScene:
	{
		worker.AddThread([this,&worker]() {
			PrepareCyclesSceneForRendering();

			if(IsRenderSceneMode(m_renderMode))
			{
				auto stereoscopic = m_camera->IsStereoscopic();
				if(stereoscopic)
					m_camera->SetStereoscopicEye(raytracing::StereoEye::Left);
				ImageRenderStage initialRenderStage;
				switch(m_renderMode)
				{
				case RenderMode::RenderImage:
					initialRenderStage = ImageRenderStage::Lighting;
					break;
				case RenderMode::SceneAlbedo:
					initialRenderStage = ImageRenderStage::SceneAlbedo;
					break;
				case RenderMode::SceneNormals:
					initialRenderStage = ImageRenderStage::SceneNormals;
					break;
				case RenderMode::SceneDepth:
					initialRenderStage = ImageRenderStage::SceneDepth;
					break;
				default:
					throw std::invalid_argument{"Invalid render mode " +std::to_string(umath::to_integral(m_renderMode))};
				}
				StartNextRenderImageStage(worker,initialRenderStage,stereoscopic ? StereoEye::Left : StereoEye::None);
				worker.Start();
			}
			else
			{
				StartNextRenderImageStage(worker,ImageRenderStage::Bake,StereoEye::None);
				worker.Start();
			}
			return RenderStageResult::Continue;
		});
		break;
	}
	/*case ImageRenderStage::Bake:
	{
		StartTextureBaking(worker);
		break;
	}*/
	case ImageRenderStage::Bake:
	{
#if 0
		ccl::ShaderEvalType shaderType;
		int bakePassFilter;
		get_bake_shader_type(m_renderMode,shaderType,bakePassFilter);
		auto *aoTarget = FindObject(BAKE_TARGET_OBJECT_NAME);
		if(aoTarget == nullptr)
		{
			worker.SetStatus(util::JobStatus::Failed,"Invalid bake target!");
			return RenderStageResult::Complete;
		}
		(*aoTarget)->name = BAKE_TARGET_OBJECT_NAME;
		m_scene.bake_manager->set(&m_scene,BAKE_TARGET_OBJECT_NAME,shaderType,bakePassFilter);
		// Note: no break is on purpose!
#endif
	}
	case ImageRenderStage::Lighting:
	{
		if(IsProgressiveRefine())
			m_progressiveRunning = true;
		worker.AddThread([this,&worker,stage,eyeStage]() {
			// start ao test
			if(IsBakingSceneMode(m_renderMode))
			{
#if 0
			validate_session(m_scene);

			auto *aoTarget = FindObject("bake_target");
			if(aoTarget == nullptr)
			{
				worker.SetStatus(util::JobStatus::Failed,"Invalid bake target!");
				return;
			}

		auto resolution = GetResolution();
		auto imgWidth = resolution.x;
		auto imgHeight = resolution.y;
		//std::vector<raytracing::baking::BakePixel> pixelArray;
		//pixelArray.resize(numPixels);
			//raytracing::baking::prepare_bake_data(*this,*aoTarget,pixelArray.data(),numPixels,imgWidth,imgHeight,bakeLightmaps);
			ccl::ShaderEvalType shaderType;
			int bake_pass_filter;
			static auto albedoOnly = false;
			switch(m_renderMode)
			{
			case RenderMode::BakeAmbientOcclusion:
				shaderType = ccl::ShaderEvalType::SHADER_EVAL_AO;
				bake_pass_filter = ccl::BAKE_FILTER_AO;
				break;
			case RenderMode::BakeDiffuseLighting:
				if(albedoOnly)
				{
					shaderType = ccl::ShaderEvalType::SHADER_EVAL_DIFFUSE;
					bake_pass_filter = ccl::BAKE_FILTER_DIFFUSE | ccl::BAKE_FILTER_COLOR;
				}
				else
				{
					shaderType = ccl::ShaderEvalType::SHADER_EVAL_DIFFUSE;//ccl::ShaderEvalType::SHADER_EVAL_DIFFUSE;
					bake_pass_filter = 510 &~ccl::BAKE_FILTER_COLOR &~ccl::BAKE_FILTER_GLOSSY;//ccl::BakePassFilterCombos::BAKE_FILTER_COMBINED;//ccl::BAKE_FILTER_DIFFUSE | ccl::BAKE_FILTER_INDIRECT | ccl::BAKE_FILTER_DIRECT;
				}
				break;
			case RenderMode::BakeNormals:
				shaderType = ccl::ShaderEvalType::SHADER_EVAL_NORMAL;
				bake_pass_filter = 0;
				break;
			}
			bake_pass_filter = 255;
			ccl::Pass::add(ccl::PASS_COMBINED, m_scene.passes, "Combined");
			m_session->read_bake_tile_cb = [this](ccl::RenderTile &tile)
			{
				//std::cout<<"read tile"<<std::endl;}
				m_tileManager.UpdateRenderTile(tile,true);
			};
			m_session->write_render_tile_cb = [](ccl::RenderTile&) {};//std::cout<<"write tile"<<std::endl;};
			aoTarget->GetCyclesObject()->name = aoTarget->GetName();

			m_scene.bake_manager->set(&m_scene,aoTarget ? aoTarget->GetName() : "",shaderType,bake_pass_filter);

			m_session->params.progressive_refine = false;

			ccl::BufferParams buffer_params = GetBufferParameters();

			// TODO
			//buffer_params.width = imgWidth;//m_session->display->draw_width;
			//buffer_params.height = imgHeight;//m_session->display->draw_height;
			//buffer_params.passes = m_scene.passes;

			/* Update session. */
			//m_session->tile_manager.set_samples(m_session->params.samples);
			m_session->reset(buffer_params, m_session->params.samples);

			m_session->progress.set_update_callback([]() {});
			m_session->start();
			m_session->wait();

			validate_session(m_scene);
#endif
			auto *aoTarget = FindObject("bake_target");
			if(aoTarget == nullptr)
			{
				worker.SetStatus(util::JobStatus::Failed,"Invalid bake target!");
				return;
			}
  ccl::ShaderEvalType shader_type = ccl::ShaderEvalType::SHADER_EVAL_AO;
  int bake_pass_filter = 255;

  /* Initialize bake manager, before we load the baking kernels. */
  m_scene.bake_manager->set(&m_scene, aoTarget->GetName(), shader_type, bake_pass_filter);

  /* Passes are identified by name, so in order to return the combined pass we need to set the
   * name. */
  ccl::Pass::add(ccl::PASS_COMBINED, m_scene.passes, "Combined");

  m_session->read_bake_tile_cb = [this](ccl::RenderTile &tile)
			{
				//std::cout<<"read tile"<<std::endl;}
				m_tileManager.UpdateRenderTile(tile,true);
			};
  m_session->write_render_tile_cb = [this](ccl::RenderTile &tile) {m_tileManager.UpdateRenderTile(tile,true);};

  if (!m_session->progress.get_cancel()) {
    /* Sync scene. */
  }
#if 0
  if (object_found && !session->progress.get_cancel()) {
    /* Get session and buffer parameters. */
    SessionParams session_params = BlenderSync::get_session_params(
        b_engine, b_userpref, b_scene, background);
    session_params.progressive_refine = false;

    BufferParams buffer_params;
    buffer_params.width = bake_width;
    buffer_params.height = bake_height;
    buffer_params.passes = scene->passes;

    /* Update session. */


#endif
	
		auto resolution = GetResolution();
    ccl::BufferParams buffer_params;
    buffer_params.width = resolution.x;
    buffer_params.height = resolution.y;
    buffer_params.passes = m_scene.passes;

	// TODO
    m_session->tile_manager.set_samples(m_session->params.samples);
    m_session->reset(buffer_params, m_session->params.samples);

    m_session->progress.set_update_callback([]() {});
  /* Perform bake. Check cancel to avoid crash with incomplete scene data. */
  //if (object_found && !session->progress.get_cancel()) {
    m_session->start();
    m_session->wait();
  //}
			}
			else
			{
				validate_session(m_scene);
				m_session->start();
			}
			// End






			//validate_session(m_scene);
			//m_session->start();

			// Render image with lighting
			auto progressMultiplier = (GetDenoiseMode() == DenoiseMode::Detailed) ? 0.95f : 1.f;
			WaitForRenderStage(worker,0.f,progressMultiplier,[this,&worker,stage,eyeStage]() mutable -> RenderStageResult {
				if(IsProgressiveRefine() == false)
					m_session->wait();
				else if(m_progressiveRunning)
				{
					std::unique_lock<std::mutex> lock {m_progressiveMutex};
					m_progressiveCondition.wait(lock);
				}
				auto &resultImageBuffer = GetResultImageBuffer(eyeStage);
				resultImageBuffer = FinalizeCyclesScene();
				// ApplyPostProcessing(*resultImageBuffer,m_renderMode);

				if(UpdateStereo(worker,stage,eyeStage))
				{
					worker.Start(); // Lighting stage for the left eye is triggered by the user, but we have to start it manually for the right eye
					return RenderStageResult::Continue;
				}

				if(ShouldDenoise() == false)
					return StartNextRenderImageStage(worker,ImageRenderStage::FinalizeImage,eyeStage);
				if(GetDenoiseMode() == DenoiseMode::Fast)
				{
					// Skip albedo/normal render passes and just go straight to denoising
					return StartNextRenderImageStage(worker,ImageRenderStage::Denoise,eyeStage);
				}
				return StartNextRenderImageStage(worker,ImageRenderStage::Albedo,eyeStage);
			});
		});
		break;
	}
	case ImageRenderStage::Albedo:
	{
		ReloadProgressiveRender();
		// Render albedo colors (required for denoising)
		m_renderMode = RenderMode::SceneAlbedo;
		if(eyeStage == StereoEye::Left || eyeStage == StereoEye::None)
			InitializeAlbedoPass(true);
		worker.AddThread([this,&worker,eyeStage,stage]() {
			validate_session(m_scene);
			m_session->start();
			WaitForRenderStage(worker,0.95f,0.025f,[this,&worker,eyeStage,stage]() mutable -> RenderStageResult {
				m_session->wait();
				auto &albedoImageBuffer = GetAlbedoImageBuffer(eyeStage);
				albedoImageBuffer = FinalizeCyclesScene();
				// ApplyPostProcessing(*albedoImageBuffer,m_renderMode);

				if(UpdateStereo(worker,stage,eyeStage))
					return RenderStageResult::Continue;

				return StartNextRenderImageStage(worker,ImageRenderStage::Normal,eyeStage);
			});
		});
		worker.Start();
		break;
	}
	case ImageRenderStage::Normal:
	{
		ReloadProgressiveRender();
		// Render normals (required for denoising)
		m_renderMode = RenderMode::SceneNormals;
		if(eyeStage == StereoEye::Left || eyeStage == StereoEye::None)
			InitializeNormalPass(true);
		worker.AddThread([this,&worker,eyeStage,stage]() {
			validate_session(m_scene);
			m_session->start();
			WaitForRenderStage(worker,0.975f,0.025f,[this,&worker,eyeStage,stage]() mutable -> RenderStageResult {
				m_session->wait();
				auto &normalImageBuffer = GetNormalImageBuffer(eyeStage);
				normalImageBuffer = FinalizeCyclesScene();
				// ApplyPostProcessing(*normalImageBuffer,m_renderMode);

				if(UpdateStereo(worker,stage,eyeStage))
					return RenderStageResult::Continue;

				return StartNextRenderImageStage(worker,ImageRenderStage::Denoise,eyeStage);
			});
		});
		worker.Start();
		break;
	}
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

		if(UpdateStereo(worker,stage,eyeStage))
			return RenderStageResult::Continue;

		return StartNextRenderImageStage(worker,ImageRenderStage::FinalizeImage,eyeStage);
	}
	case ImageRenderStage::FinalizeImage:
	{
		auto &resultImageBuffer = GetResultImageBuffer(eyeStage);
		if(m_colorTransformProcessor) // TODO: Should we really apply color transform if we're not denoising?
		{
			std::string err;
			if(m_colorTransformProcessor->Apply(*resultImageBuffer,err,0.f,GetGamma()) == false)
				HandleError("Unable to apply color transform: " +err);
		}
		resultImageBuffer->Convert(uimg::ImageBuffer::Format::RGBA_HDR);
		resultImageBuffer->ClearAlpha();
		if(IsProgressive() == false) // If progressive, our tile manager will have already flipped the image
			resultImageBuffer->Flip(true,true);
		if(UpdateStereo(worker,stage,eyeStage))
			return RenderStageResult::Continue;
		if(eyeStage == StereoEye::Left)
			return StartNextRenderImageStage(worker,ImageRenderStage::MergeStereoscopic,StereoEye::None);
		return StartNextRenderImageStage(worker,ImageRenderStage::Finalize,StereoEye::None);
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
		return StartNextRenderImageStage(worker,ImageRenderStage::Finalize,StereoEye::None);
	}
	case ImageRenderStage::Finalize:
		// We're done here
		CloseCyclesScene();
		return RenderStageResult::Complete;
	case ImageRenderStage::SceneAlbedo:
	case ImageRenderStage::SceneNormals:
	case ImageRenderStage::SceneDepth:
	{
		ReloadProgressiveRender();
		if(eyeStage != StereoEye::Right)
		{
			if(stage == ImageRenderStage::SceneAlbedo)
				InitializeAlbedoPass(true);
			else if(stage == ImageRenderStage::SceneNormals)
				InitializeNormalPass(true);
		}
		worker.AddThread([this,&worker,eyeStage,stage]() {
			validate_session(m_scene);
			m_session->start();
			WaitForRenderStage(worker,0.f,1.f,[this,&worker,eyeStage,stage]() mutable -> RenderStageResult {
				m_session->wait();
				auto &resultImageBuffer = GetResultImageBuffer(eyeStage);
				resultImageBuffer = FinalizeCyclesScene();
				// ApplyPostProcessing(*resultImageBuffer,m_renderMode);

				if(UpdateStereo(worker,stage,eyeStage))
				{
					worker.Start(); // Initial stage for the left eye is triggered by the user, but we have to start it manually for the right eye
					return RenderStageResult::Continue;
				}

				return StartNextRenderImageStage(worker,ImageRenderStage::FinalizeImage,eyeStage);
			});
		});
		break;
	}
	}
	return RenderStageResult::Continue;
}

void raytracing::Scene::SetupRenderSettings(
	ccl::Scene &scene,ccl::Session &session,ccl::BufferParams &bufferParams,raytracing::Scene::RenderMode renderMode,
	uint32_t maxTransparencyBounces
) const
{
	// Default parameters taken from Blender
	auto &integrator = *scene.integrator;
	integrator.set_min_bounce(0);
	integrator.set_max_bounce(m_sceneInfo.maxBounces);
	integrator.set_max_diffuse_bounce(m_sceneInfo.maxDiffuseBounces);
	integrator.set_max_glossy_bounce(m_sceneInfo.maxGlossyBounces);
	integrator.set_max_transmission_bounce(m_sceneInfo.maxTransmissionBounces);
	integrator.set_max_volume_bounce(0);

	integrator.set_transparent_min_bounce(0);
	integrator.set_transparent_max_bounce(maxTransparencyBounces);

	integrator.set_volume_max_steps(1024);
	integrator.set_volume_step_rate(0.1);

	integrator.set_caustics_reflective(true);
	integrator.set_caustics_refractive(true);
	integrator.set_filter_glossy(0.f);
	integrator.set_seed(0);
	integrator.set_sampling_pattern(ccl::SamplingPattern::SAMPLING_PATTERN_SOBOL);

	integrator.set_sample_clamp_direct(0.f);
	integrator.set_sample_clamp_indirect(0.f);
	integrator.set_motion_blur(false);
	integrator.set_method(ccl::Integrator::Method::PATH);
	integrator.set_sample_all_lights_direct(true);
	integrator.set_sample_all_lights_indirect(true);
	integrator.set_light_sampling_threshold(0.f);

	integrator.set_diffuse_samples(1);
	integrator.set_glossy_samples(1);
	integrator.set_transmission_samples(1);
	integrator.set_ao_samples(1);
	integrator.set_mesh_light_samples(1);
	integrator.set_subsurface_samples(1);
	integrator.set_volume_samples(1);
	
	integrator.set_ao_bounces(0);
	integrator.tag_update(&scene);

	// Film
	auto &film = *scene.film;
	film.set_exposure(1.f);
	film.set_filter_type(ccl::FilterType::FILTER_GAUSSIAN);
	film.set_filter_width(1.5f);
	if(renderMode == raytracing::Scene::RenderMode::RenderImage)
	{
		film.set_mist_start(5.f);
		film.set_mist_depth(25.f);
		film.set_mist_falloff(2.f);
	}
	film.tag_passes_update(&scene, scene.passes);

	film.set_cryptomatte_depth(3);
	film.set_cryptomatte_passes(ccl::CRYPT_NONE);

	session.params.pixel_size = 1;
	session.params.threads = 0;
	session.params.use_profiling = false;
	session.params.shadingsystem = ccl::ShadingSystem::SHADINGSYSTEM_SVM;

	ccl::vector<ccl::Pass> passes;
	auto displayPass = ccl::PassType::PASS_DIFFUSE_COLOR;
	switch(renderMode)
	{
	case raytracing::Scene::RenderMode::SceneAlbedo:
		// Note: PASS_DIFFUSE_COLOR would probably make more sense but does not seem to work
		// (just creates a black output).
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"Combined");
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case raytracing::Scene::RenderMode::SceneNormals:
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"Combined");
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case raytracing::Scene::RenderMode::SceneDepth:
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"Combined"); // TODO: Why do we need this?
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case raytracing::Scene::RenderMode::RenderImage:
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"Combined");
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case raytracing::Scene::RenderMode::BakeAmbientOcclusion:
		//ccl::Pass::add(ccl::PassType::PASS_AO,passes,"ao");
		//ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		ccl::Pass::add(ccl::PASS_COMBINED,passes,"Combined");
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case raytracing::Scene::RenderMode::BakeDiffuseLighting:
		ccl::Pass::add(ccl::PASS_COMBINED,passes,"Combined");
		//ccl::Pass::add(ccl::PassType::PASS_DIFFUSE_DIRECT,passes,"diffuse_direct");
		//ccl::Pass::add(ccl::PassType::PASS_DIFFUSE_INDIRECT,passes,"diffuse_indirect");
		//ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED; // TODO: Is this correct?
		break;
	}
	bufferParams.passes = passes;

	if(m_sceneInfo.motionBlurStrength > 0.f)
	{
		// ccl::Pass::add(ccl::PassType::PASS_MOTION,passes);
		scene.integrator->set_motion_blur(true);
	}

	film.set_pass_alpha_threshold(0.5);

	if(IsBakingSceneMode(m_renderMode))
	{
		film.set_exposure(1.f);
		film.set_denoising_data_pass(false);
		film.set_denoising_clean_pass(false);
		film.set_denoising_prefiltered_pass(false);
		film.set_denoising_flags(0);
		film.set_pass_alpha_threshold(0.f);
		film.set_display_pass(ccl::PassType::PASS_COMBINED);
		film.set_filter_type(ccl::FilterType::FILTER_BLACKMAN_HARRIS);
		film.set_filter_width(1.5f);
		film.set_mist_start(5.f);
		film.set_mist_depth(25.f);
		film.set_mist_falloff(2.f);
		film.set_use_light_visibility(false);
		film.set_cryptomatte_passes(ccl::CryptomatteType::CRYPT_NONE);
		film.set_cryptomatte_depth(0);
		film.set_use_adaptive_sampling(false);
	}

	film.tag_passes_update(&scene, passes);
	film.set_display_pass(displayPass);
	scene.integrator->tag_update(&scene);

	// Camera
	/*auto &cam = *scene.camera;
	cam.shuttertime	= 0.500000000;
	cam.motion_position=	ccl::Camera::MotionPosition::MOTION_POSITION_CENTER;
	cam.shutter_table_offset=	18446744073709551615;
	cam.rolling_shutter_type=	ccl::Camera::RollingShutterType::ROLLING_SHUTTER_NONE;
	cam.rolling_shutter_duration=	0.100000001	;
	cam.focaldistance=	2.49260306	;
	cam.aperturesize=	0.00625000009	;
	cam.blades=	0;
	cam.bladesrotation=	0.000000000	;
	cam.type=	ccl::CAMERA_PERSPECTIVE ;
	cam.fov=	0.503379941	;
	cam.panorama_type=	ccl::PANORAMA_FISHEYE_EQUISOLID ;
	cam.fisheye_fov=	3.14159274	;
	cam.fisheye_lens=	10.5000000	;
	cam.latitude_min=	-1.57079637	;
	cam.latitude_max=	1.57079637	;
	cam.longitude_min=	-3.14159274	;
	cam.longitude_max=	3.14159274	;
	cam.stereo_eye=	ccl::Camera::STEREO_NONE ;
	cam.use_spherical_stereo=	false	;
	cam.interocular_distance=	0.0649999976	;
	cam.convergence_distance=	1.94999993	;
	cam.use_pole_merge	=false	;
	cam.pole_merge_angle_from=	1.04719758	;
	cam.pole_merge_angle_to=	1.30899692	;
	cam.aperture_ratio=	1.00000000	;
	cam.sensorwidth=	0.0359999985	;
	cam.sensorheight=	0.0240000002	;
	cam.nearclip=	0.100000001	;
	cam.farclip	=100.000000	;
	cam.width	=3840	;
	cam.height=	2160	;
	cam.resolution=	1	;

	cam.viewplane.left=	-1.77777779	;
	cam.viewplane.right=	1.77777779	;
	cam.viewplane.bottom=	-1.00000000	;
	cam.viewplane.top=	1.00000000	;


	cam.full_width=	3840	;
	cam.full_height=	2160	;
	cam.offscreen_dicing_scale	=4.00000000	;
	cam.border.left = 0.f;
	cam.border.right = 1.f;
	cam.border.bottom = 0.f;
	cam.border.top = 1.f;
	cam.viewport_camera_border .left = 0.f;
	cam.viewport_camera_border.right = 1.f;
	cam.viewport_camera_border.bottom = 0.f;
	cam.viewport_camera_border.top = 1.f;

	cam.matrix.x.x=	1.00000000	;
	cam.matrix.x.y=	1.63195708e-07	;
	cam.matrix.x.z=	3.42843151e-07	;
	cam.matrix.x.w=	17.5277958	;

	cam.matrix.y.x=	-3.47716451e-07	;
	cam.matrix.y.y	=0.0308625121	;
	cam.matrix.y.z=	0.999523640	;
	cam.matrix.y.w=	-2.77792454	;

	cam.matrix.z.x=	-1.52536970e-07	;
	cam.matrix.z.y=	0.999523640	;
	cam.matrix.z.z=	-0.0308625121	;
	cam.matrix.z.w=	0.846632719	;

	cam.use_perspective_motion=	false	;
	cam.fov_pre=	0.503379941	;
	cam.fov_post	=0.503379941	;

	cam.tag_update();*/
}
#pragma optimize("",on)
