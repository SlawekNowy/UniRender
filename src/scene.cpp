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
#ifdef ENABLE_CYCLES_LOGGING
#define GLOG_NO_ABBREVIATED_SEVERITIES
#include <util/util_logging.h>
#include <glog/logging.h>
#endif
#include <optional>
#include <fsys/filesystem.h>
#include <sharedutils/datastream.h>
#include <sharedutils/util_file.h>
#include <sharedutils/util.h>
#include <sharedutils/util_path.hpp>
#include <util_image.hpp>
#include <util_image_buffer.hpp>
#include <util_texture_info.hpp>
#include <util_ocio.hpp>
#include <random>

#ifdef ENABLE_CYCLES_LOGGING
#pragma comment(lib,"shlwapi.lib")
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ccl happens to have the same include guard name as sharedutils, so we have to undef it here
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>

#pragma optimize("",off)

raytracing::SceneWorker::SceneWorker(Scene &scene)
	: util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>{},m_scene{scene.shared_from_this()}
{}
void raytracing::SceneWorker::DoCancel(const std::string &resultMsg)
{
	util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::DoCancel(resultMsg);
	m_scene->OnParallelWorkerCancelled();
}
void raytracing::SceneWorker::Wait()
{
	util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::Wait();
	m_scene->Wait();
}
std::shared_ptr<uimg::ImageBuffer> raytracing::SceneWorker::GetResult() {return m_scene->GetResultImageBuffer();}

///////////////////

void raytracing::Scene::CreateInfo::Serialize(DataStream &ds) const
{
	ds->Write(reinterpret_cast<const uint8_t*>(this),offsetof(CreateInfo,colorTransform));
	ds->Write<bool>(colorTransform.has_value());
	if(colorTransform.has_value())
	{
		ds->WriteString(colorTransform->config);
		ds->Write<bool>(colorTransform->lookName.has_value());
		if(colorTransform->lookName.has_value())
			ds->WriteString(*colorTransform->lookName);
	}
}
void raytracing::Scene::CreateInfo::Deserialize(DataStream &ds)
{
	ds->Read(this,offsetof(CreateInfo,colorTransform));
	auto hasColorTransform = ds->Read<bool>();
	if(hasColorTransform == false)
		return;
	colorTransform = ColorTransformInfo{};
	colorTransform->config = ds->ReadString();
	auto hasLookName = ds->Read<bool>();
	if(hasLookName == false)
		return;
	colorTransform->lookName = ds->ReadString();
}

///////////////////

void raytracing::Scene::ApplyPostProcessing(uimg::ImageBuffer &imgBuffer,raytracing::Scene::RenderMode renderMode)
{
	// For some reason the image is flipped horizontally when rendering an image,
	// so we'll just flip it the right way here
	auto flipHorizontally = IsRenderSceneMode(renderMode);
	if(m_scene.camera->type == ccl::CameraType::CAMERA_PANORAMA)
	{
		switch(m_scene.camera->panorama_type)
		{
		case ccl::PanoramaType::PANORAMA_EQUIRECTANGULAR:
		case ccl::PanoramaType::PANORAMA_FISHEYE_EQUIDISTANT:
			flipHorizontally = false; // I have no idea why some types have to be flipped and others don't
			break;
		}
	}
	if(flipHorizontally)
		imgBuffer.FlipHorizontally();

	// We will also always have to flip the image vertically, since the data seems to be bottom->top and we need it top->bottom
	imgBuffer.FlipVertically();
	imgBuffer.ClearAlpha();
}

bool raytracing::Scene::IsRenderSceneMode(RenderMode renderMode)
{
	switch(renderMode)
	{
	case RenderMode::RenderImage:
	case RenderMode::SceneAlbedo:
	case RenderMode::SceneNormals:
	case RenderMode::SceneDepth:
		return true;
	}
	return false;
}

static std::optional<std::string> KERNEL_PATH {};
void raytracing::Scene::SetKernelPath(const std::string &kernelPath) {KERNEL_PATH = kernelPath;}
static void init_cycles()
{
	static auto isInitialized = false;
	if(isInitialized)
		return;
	isInitialized = true;

	std::string kernelPath;
	if(KERNEL_PATH.has_value())
		kernelPath = *KERNEL_PATH;
	else
		kernelPath = util::get_program_path();

	ccl::path_init(kernelPath,kernelPath);

	putenv(("CYCLES_KERNEL_PATH=" +kernelPath).c_str());
	putenv(("CYCLES_SHADER_PATH=" +kernelPath).c_str());
#ifdef ENABLE_CYCLES_LOGGING
	google::SetLogDestination(google::GLOG_INFO,(kernelPath +"/log/info.log").c_str());
	google::SetLogDestination(google::GLOG_WARNING,(kernelPath +"/log/warning.log").c_str());
	FLAGS_log_dir = kernelPath +"/log";

	ccl::util_logging_init("util_raytracing");
	ccl::util_logging_verbosity_set(2);
	ccl::util_logging_start();
	FLAGS_logtostderr = false;
	FLAGS_alsologtostderr = true; // Doesn't seem to work properly?
	FLAGS_stderrthreshold = google::GLOG_WARNING|google::GLOG_ERROR|google::GLOG_INFO|google::GLOG_FATAL;
	FLAGS_v = 5; // Setting the log level any other way doesn't seem to work properly

	// Test output
	/*google::LogAtLevel(google::GLOG_INFO,"Info test");
	google::LogAtLevel(google::GLOG_WARNING,"Warning test");
	VLOG(1) << "Colorspace " << 5 << " is no-op";
	google::FlushLogFiles(google::GLOG_INFO);
	google::FlushLogFiles(google::GLOG_WARNING);*/
#endif
}

static bool is_device_type_available(ccl::DeviceType type)
{
	using namespace ccl;
	return ccl::Device::available_devices(DEVICE_MASK(type)).empty() == false;
}
bool raytracing::Scene::ReadHeaderInfo(DataStream &ds,RenderMode &outRenderMode,CreateInfo &outCreateInfo,SerializationData &outSerializationData,uint32_t &outVersion,SceneInfo *optOutSceneInfo)
{
	return ReadSerializationHeader(ds,outRenderMode,outCreateInfo,outSerializationData,outVersion,optOutSceneInfo);
}
std::shared_ptr<raytracing::Scene> raytracing::Scene::Create(NodeManager &nodeManager,DataStream &dsIn,const std::string &rootDir,RenderMode renderMode,const CreateInfo &createInfo)
{
	auto scene = Create(nodeManager,renderMode,createInfo);
	if(scene == nullptr || scene->Load(dsIn,rootDir) == false)
		return nullptr;
	return scene;
}
std::shared_ptr<raytracing::Scene> raytracing::Scene::Create(NodeManager &nodeManager,DataStream &dsIn,const std::string &rootDir)
{
	RenderMode renderMode;
	CreateInfo createInfo;
	SerializationData serializationData;
	uint32_t version;
	if(ReadSerializationHeader(dsIn,renderMode,createInfo,serializationData,version) == false)
		return nullptr;
	return Create(nodeManager,dsIn,rootDir,renderMode,createInfo);
}

std::shared_ptr<raytracing::Scene> raytracing::Scene::Create(NodeManager &nodeManager,RenderMode renderMode,const CreateInfo &createInfo)
{
	init_cycles();

	auto cclDeviceType = ccl::DeviceType::DEVICE_CPU;
	switch(createInfo.deviceType)
	{
	case DeviceType::GPU:
	{
		if(is_device_type_available(ccl::DeviceType::DEVICE_CUDA))
		{
			cclDeviceType = ccl::DeviceType::DEVICE_CUDA;
			break;
		}
		if(is_device_type_available(ccl::DeviceType::DEVICE_OPENCL))
		{
			// Note: In some cases Cycles has to rebuild OpenCL shaders, but by default Cycles tries to do so using Blender's python implementation.
			// Since this isn't Blender, Cycles will create several instances of Pragma and the process will get stuck.
			// To fix the issue, a change in the Cycles library is required: OpenCLDevice::OpenCLProgram::compile_separate has
			// to always return false! This will make it fall back to an internal build function that doesn't require Blender / Python.
			cclDeviceType = ccl::DeviceType::DEVICE_OPENCL;
			break;
		}
		// No break is intended!
	}
	case DeviceType::CPU:
		cclDeviceType = ccl::DeviceType::DEVICE_CPU;
		break;
	}
	static_assert(umath::to_integral(DeviceType::Count) == 2);

	std::optional<ccl::DeviceInfo> device = {};
	for(auto &devInfo : ccl::Device::available_devices(ccl::DeviceTypeMask::DEVICE_MASK_CUDA | ccl::DeviceTypeMask::DEVICE_MASK_OPENCL | ccl::DeviceTypeMask::DEVICE_MASK_CPU))
	{
		if(devInfo.type == cclDeviceType)
		{
			device = devInfo;
			break;
		}
		if(devInfo.type == ccl::DeviceType::DEVICE_CPU)
			device = devInfo; // Fallback / Default device type
	}

	if(device.has_value() == false)
		return nullptr; // No device available

	auto deviceType = createInfo.deviceType;
	if(device->type == ccl::DeviceType::DEVICE_CPU)
		deviceType = DeviceType::CPU;

	ccl::SessionParams sessionParams {};
	sessionParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;
	sessionParams.device = *device;
	sessionParams.progressive = true; // TODO: This should be set to false(?), but doing so causes a crash during rendering
	sessionParams.background = true;
	sessionParams.progressive_refine = createInfo.progressiveRefine;
	sessionParams.display_buffer_linear = createInfo.hdrOutput;

	switch(deviceType)
	{
	case DeviceType::GPU:
		sessionParams.tile_size = {256,256};
		break;
	default:
		sessionParams.tile_size = {16,16};
		break;
	}
	sessionParams.tile_order = ccl::TileOrder::TILE_HILBERT_SPIRAL;

	/*if(createInfo.denoiseMode != DenoiseMode::None && renderMode == RenderMode::RenderImage)
	{
		sessionParams.full_denoising = true;
		sessionParams.run_denoising = true;
	}*/
	// We'll handle denoising ourselves
	sessionParams.full_denoising = false;
	sessionParams.run_denoising = false;

	sessionParams.start_resolution = 64;
	if(createInfo.samples.has_value())
		sessionParams.samples = *createInfo.samples;
	else
	{
		switch(renderMode)
		{
		case RenderMode::BakeAmbientOcclusion:
		case RenderMode::BakeNormals:
		case RenderMode::BakeDiffuseLighting:
			sessionParams.samples = 1'225u;
			break;
		default:
			sessionParams.samples = 1'024u;
			break;
		}
	}

	if(createInfo.progressive)
	{
		sessionParams.write_render_cb = nullptr;
		if(createInfo.progressiveRefine)
			sessionParams.samples = 50'000;
	}
	else if(IsRenderSceneMode(renderMode) || renderMode == RenderMode::BakeDiffuseLighting || renderMode == RenderMode::BakeNormals || renderMode == RenderMode::BakeAmbientOcclusion)
	{
		// We need to define a write callback, otherwise the session's display object will not be initialized.
		sessionParams.write_render_cb = [](const ccl::uchar *pixels,int w,int h,int channels) -> bool {return true;};
	}

#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	if(IsRenderSceneMode(renderMode) == false)
	{
		//sessionParams.background = true;
		sessionParams.progressive_refine = false;
		sessionParams.progressive = false;
		sessionParams.experimental = false;
		sessionParams.tile_size = {256,256};
		sessionParams.tile_order = ccl::TileOrder::TILE_BOTTOM_TO_TOP;
		sessionParams.start_resolution = 2147483647;
		sessionParams.pixel_size = 1;
		sessionParams.threads = 0;
		sessionParams.use_profiling = false;
		sessionParams.display_buffer_linear = true;
		sessionParams.run_denoising = false;
		sessionParams.write_denoising_passes = false;
		sessionParams.full_denoising = false;
		sessionParams.progressive_update_timeout = 1.0000000000000000;
		sessionParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;
	}
#endif
	auto session = std::make_unique<ccl::Session>(sessionParams);

	ccl::SceneParams sceneParams {};
	sceneParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;

	auto *cclScene = new ccl::Scene{sceneParams,session->device}; // Object will be removed automatically by cycles
	cclScene->params.bvh_type = ccl::SceneParams::BVH_STATIC;
	cclScene->params.persistent_data = true;

	auto *pSession = session.get();
	auto scene = std::shared_ptr<Scene>{new Scene{nodeManager,std::move(session),*cclScene,renderMode,deviceType}};

	auto *pScene = scene.get();
	if(createInfo.progressive)
	{
		pSession->update_render_tile_cb = [pScene](ccl::RenderTile tile,bool param) {
			pScene->m_tileManager.UpdateRenderTile(tile,param);
		};
		pSession->write_render_tile_cb = [pScene](ccl::RenderTile &tile) {
			pScene->m_tileManager.WriteRenderTile(tile);
		};
	}

	scene->m_camera = Camera::Create(*scene);
	scene->m_createInfo = createInfo;
	umath::set_flag(scene->m_stateFlags,StateFlags::OutputResultWithHDRColors,createInfo.hdrOutput);
	return scene;
}

raytracing::Scene::Scene(NodeManager &nodeManager,std::unique_ptr<ccl::Session> session,ccl::Scene &scene,RenderMode renderMode,DeviceType deviceType)
	: m_session{std::move(session)},m_scene{scene},m_renderMode{renderMode},m_deviceType{deviceType},m_nodeManager{nodeManager.shared_from_this()}
{}

raytracing::Scene::~Scene()
{
	Wait();
	FinalizeAndCloseCyclesScene();
}

std::shared_ptr<uimg::ImageBuffer> raytracing::Scene::FinalizeCyclesScene()
{
	// Note: We want the HDR output values from cycles which haven't been tonemapped yet, but Cycles
	// makes this impossible to do, so we'll have to use this work-around.
	class SessionWrapper
		: ccl::Session
	{
	public:
		void Finalize(bool hdr,bool progressive)
		{
			// This part is the same code as the respective part in Session::~Session()
			if (session_thread) {
				/* wait for session thread to end */
				progress.set_cancel("Exiting");

				gpu_need_display_buffer_update = false;
				gpu_need_display_buffer_update_cond.notify_all();

				{
					ccl::thread_scoped_lock pause_lock(pause_mutex);
					pause = false;
				}
				pause_cond.notify_all();

				wait();
			}
			//

			if(progressive == false)
			{
				/* tonemap and write out image if requested */
				delete display;

				display = new ccl::DisplayBuffer(device, hdr);
				display->reset(buffers->params);
				copy_to_display_buffer(params.samples);
			}
		}
	};
	auto &session = reinterpret_cast<SessionWrapper&>(*m_session);
	auto outputWithHDR = umath::is_flag_set(m_stateFlags,StateFlags::OutputResultWithHDRColors);
	session.Finalize(outputWithHDR,m_createInfo.progressive);

	std::shared_ptr<uimg::ImageBuffer> imgBuffer = nullptr;
	if(m_createInfo.progressive == false)
	{
		auto w = m_session->display->draw_width;
		auto h = m_session->display->draw_height;
		if(outputWithHDR)
		{
			auto *pixels = m_session->display->rgba_half.copy_from_device(0, w, h);
			imgBuffer = uimg::ImageBuffer::Create(pixels,w,h,uimg::ImageBuffer::Format::RGBA_HDR,false);
		}
		else
		{
			if(m_sceneInfo.exposure == 1.f)
			{
				auto *pixels = m_session->display->rgba_byte.copy_from_device(0, w, h);
				imgBuffer = uimg::ImageBuffer::Create(pixels,w,h,uimg::ImageBuffer::Format::RGBA_LDR,false);
			}
			else
			{
				auto *pixels = m_session->display->rgba_half.copy_from_device(0, w, h);
				imgBuffer = uimg::ImageBuffer::Create(pixels,w,h,uimg::ImageBuffer::Format::RGBA_HDR,false);
				imgBuffer->ApplyExposure(m_sceneInfo.exposure);
				imgBuffer->Convert(uimg::ImageBuffer::Format::RGBA_LDR);
			}
		}
	}
	else
		imgBuffer = m_tileManager.UpdateFinalImage()->Copy();
	return imgBuffer;
}

void raytracing::Scene::FinalizeAndCloseCyclesScene()
{
	if(m_session && IsRenderSceneMode(m_renderMode) && umath::is_flag_set(m_stateFlags,StateFlags::HasRenderingStarted))
		GetResultImageBuffer() = FinalizeCyclesScene();
	CloseCyclesScene();
}

void raytracing::Scene::CloseCyclesScene()
{
	m_mdlCaches.clear();
	m_renderData = {};
	m_camera = nullptr;
	m_tileManager.StopAndWait();

	if(m_session == nullptr)
		return;
	m_session = nullptr;
}

raytracing::Camera &raytracing::Scene::GetCamera() {return *m_camera;}
bool raytracing::Scene::IsValid() const {return m_session != nullptr;}

bool raytracing::Scene::IsValidTexture(const std::string &filePath) const
{
	std::string ext;
	if(ufile::get_extension(filePath,&ext) == false || ustring::compare(ext,"dds",false) == false)
		return false;
	return FileManager::Exists(filePath,fsys::SearchFlags::Local);
}

raytracing::PMesh raytracing::Scene::FindRenderMeshByHash(const util::MurmurHash3 &hash) const
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
raytracing::PObject raytracing::Scene::FindRenderObjectByHash(const util::MurmurHash3 &hash) const
{
	return nullptr;
}

std::shared_ptr<raytracing::CCLShader> raytracing::Scene::GetCachedShader(const GroupNodeDesc &desc) const
{
	auto it = m_shaderCache.find(&desc);
	if(it == m_shaderCache.end())
		return nullptr;
	auto idx = it->second;
	return m_cclShaders.at(idx);
}

void raytracing::Scene::AddShader(CCLShader &shader,const GroupNodeDesc *optDesc)
{
	m_cclShaders.push_back(shader.shared_from_this());
	if(optDesc)
		m_shaderCache[optDesc] = m_cclShaders.size() -1;
}

static std::optional<std::string> get_abs_sky_path(const std::string &skyTex)
{
	std::string absPath = skyTex;
	if(FileManager::ExistsSystem(absPath) == false && FileManager::FindAbsolutePath("materials/" +skyTex,absPath) == false)
		return {};
	return absPath;
}

void raytracing::Scene::AddSkybox(const std::string &texture)
{
	if(umath::is_flag_set(m_stateFlags,StateFlags::SkyInitialized))
		return;
	umath::set_flag(m_stateFlags,StateFlags::SkyInitialized);

	if(m_renderMode == RenderMode::SceneDepth)
	{
		auto desc = raytracing::GroupNodeDesc::Create(*m_nodeManager);
		auto &nodeOutput = desc->AddNode(NODE_OUTPUT);
		auto &nodeBg = desc->AddNode(NODE_BACKGROUND_SHADER);
		nodeBg.SetProperty(nodes::background_shader::IN_STRENGTH,1'000.f);

		auto col = desc->CombineRGB(1.f,1.f,1.f);
		desc->Link(col,nodeBg.GetInputSocket(nodes::background_shader::IN_COLOR));
		desc->Link(nodeBg,nodes::background_shader::OUT_BACKGROUND,nodeOutput,nodes::output::IN_SURFACE);
		
		AddShader(*CCLShader::Create(*this,*m_scene.default_background,*desc));
		return;
	}

	auto skyTex = (m_sceneInfo.sky.empty() == false) ? m_sceneInfo.sky : texture;

	// Note: m_sky can be absolute or relative path
	auto absPath = get_abs_sky_path(skyTex);
	if(absPath.has_value() == false)
		return;

	// Setup the skybox as a background shader
	auto desc = raytracing::GroupNodeDesc::Create(*m_nodeManager);
	auto &nodeOutput = desc->AddNode(NODE_OUTPUT);
	auto &nodeBg = desc->AddNode(NODE_BACKGROUND_SHADER);
	nodeBg.SetProperty(nodes::background_shader::IN_STRENGTH,m_sceneInfo.skyStrength);
	
	auto &nodeTex = desc->AddImageTextureNode(*absPath,TextureType::EquirectangularImage);
	desc->Link(nodeTex,nodes::environment_texture::OUT_COLOR,nodeBg,nodes::background_shader::IN_COLOR);
	desc->Link(nodeBg,nodes::background_shader::OUT_BACKGROUND,nodeOutput,nodes::output::IN_SURFACE);

	auto skyAngles = m_sceneInfo.skyAngles;
	// skyAngles.p -= 90.f;
	skyAngles = {
		-skyAngles.p,
		skyAngles.r,
		-skyAngles.y
	};

	auto &nodeTexCoord = desc->AddNode(NODE_TEXTURE_COORDINATE);
	auto &nodeMapping = desc->AddNode(NODE_MAPPING);
	nodeMapping.SetProperty(nodes::mapping::IN_TYPE,ccl::NodeMappingType::NODE_MAPPING_TYPE_POINT);
	nodeMapping.SetProperty(nodes::mapping::IN_ROTATION,skyAngles);
	desc->Link(nodeTexCoord,nodes::texture_coordinate::OUT_GENERATED,nodeMapping,nodes::mapping::IN_VECTOR);

	desc->Link(nodeMapping,nodes::mapping::OUT_VECTOR,nodeTex,nodes::environment_texture::IN_VECTOR);
	AddShader(*CCLShader::Create(*this,*m_scene.default_background,*desc));

	// Add the light source for the background
	auto *light = new ccl::Light{}; // Object will be removed automatically by cycles
	light->tfm = ccl::transform_identity();

	m_scene.lights.push_back(light);
	light->type = ccl::LightType::LIGHT_BACKGROUND;
	light->map_resolution = 2'048;
	light->shader = m_scene.default_background;
	light->use_mis = true;
	light->max_bounces = 1'024;
	light->samples = 4;
	light->tag_update(&m_scene);
}

static uint32_t calc_pixel_offset(uint32_t imgWidth,uint32_t xOffset,uint32_t yOffset)
{
	return yOffset *imgWidth +xOffset;
}

static bool row_contains_visible_pixels(const float *inOutImgData,uint32_t pxStartOffset,uint32_t w)
{
	for(auto x=decltype(w){0u};x<w;++x)
	{
		if(inOutImgData[(pxStartOffset +x) *4 +3] > 0.f)
			return true;
	}
	return false;
}

static bool col_contains_visible_pixels(const float *inOutImgData,uint32_t pxStartOffset,uint32_t h,uint32_t imgWidth)
{
	for(auto y=decltype(h){0u};y<h;++y)
	{
		if(inOutImgData[(pxStartOffset +(y *imgWidth)) *4 +3] > 0.f)
			return true;
	}
	return false;
}

static void shrink_area_to_fit(const float *inOutImgData,uint32_t imgWidth,uint32_t &xOffset,uint32_t &yOffset,uint32_t &w,uint32_t &h)
{
	while(h > 0 && row_contains_visible_pixels(inOutImgData,calc_pixel_offset(imgWidth,xOffset,yOffset),w) == false)
	{
		++yOffset;
		--h;
	}
	while(h > 0 && row_contains_visible_pixels(inOutImgData,calc_pixel_offset(imgWidth,xOffset,yOffset +h -1),w) == false)
		--h;

	while(w > 0 && col_contains_visible_pixels(inOutImgData,calc_pixel_offset(imgWidth,xOffset,yOffset),h,imgWidth) == false)
	{
		++xOffset;
		--w;
	}
	while(w > 0 && col_contains_visible_pixels(inOutImgData,calc_pixel_offset(imgWidth,xOffset +w -1,yOffset),h,imgWidth) == false)
		--w;
}

void raytracing::Scene::DenoiseHDRImageArea(uimg::ImageBuffer &imgBuffer,uint32_t imgWidth,uint32_t imgHeight,uint32_t xOffset,uint32_t yOffset,uint32_t w,uint32_t h) const
{
	// In some cases the borders may not contain any image data (i.e. fully transparent) if the pixels are not actually
	// being used by any geometry. Since the denoiser does not know transparency, we have to shrink the image area to exclude the
	// transparent borders to avoid artifacts.
	auto *imgData = static_cast<float*>(imgBuffer.GetData());
	shrink_area_to_fit(imgData,imgWidth,xOffset,yOffset,w,h);

	if(w == 0 || h == 0)
		return; // Nothing for us to do

				// Sanity check
	auto pxStartOffset = calc_pixel_offset(imgWidth,xOffset,yOffset);
	for(auto y=decltype(h){0u};y<h;++y)
	{
		for(auto x=decltype(w){0u};x<w;++x)
		{
			auto srcPxIdx = pxStartOffset +y *imgWidth +x;
			auto a = imgData[srcPxIdx *4 +3];
			if(a < 1.f)
			{
				// This should be unreachable, but just in case...
				// If this case does occur, that means there are transparent pixels WITHIN the image area, which are not
				// part of a transparent border!
				std::cerr<<"ERROR: Image area for denoising contains transparent pixel at ("<<x<<","<<y<<") with alpha of "<<a<<"! This is not allowed!"<<std::endl;
			}
		}
	}

	// White areas
	/*for(auto y=decltype(h){0u};y<h;++y)
	{
	for(auto x=decltype(w){0u};x<w;++x)
	{
	auto srcPxIdx = pxStartOffset +y *imgWidth +x;
	auto dstPxIdx = y *w +x;
	if(inOutImgData[srcPxIdx *4 +3] == 0.f)
	{
	inOutImgData[srcPxIdx *4 +0] = 0.f;
	inOutImgData[srcPxIdx *4 +1] = 0.f;
	inOutImgData[srcPxIdx *4 +2] = 0.f;
	inOutImgData[srcPxIdx *4 +3] = 1.f;
	}
	else
	{
	inOutImgData[srcPxIdx *4 +0] = 1.f;
	inOutImgData[srcPxIdx *4 +1] = 1.f;
	inOutImgData[srcPxIdx *4 +2] = 1.f;
	inOutImgData[srcPxIdx *4 +3] = 1.f;
	}
	}
	}*/

	std::vector<float> imgAreaData {};
	imgAreaData.resize(w *h *3);
	// Extract the area from the image data
	for(auto y=decltype(h){0u};y<h;++y)
	{
		for(auto x=decltype(w){0u};x<w;++x)
		{
			auto srcPxIdx = pxStartOffset +y *imgWidth +x;
			auto dstPxIdx = y *w +x;
			for(uint8_t i=0;i<3;++i)
				imgAreaData.at(dstPxIdx *3 +i) = imgData[srcPxIdx *4 +i];
		}
	}

	// Denoise the extracted area
	DenoiseInfo denoiseInfo {};
	denoiseInfo.hdr = true;
	denoiseInfo.width = w;
	denoiseInfo.height = h;
	denoise(denoiseInfo,imgAreaData.data());

	// Copy the denoised area back into the original image
	for(auto y=decltype(h){0u};y<h;++y)
	{
		for(auto x=decltype(w){0u};x<w;++x)
		{
			auto srcPxIdx = pxStartOffset +y *imgWidth +x;
			//if(inOutImgData[srcPxIdx *4 +3] == 0.f)
			//	continue; // Alpha is zero; Skip this one
			auto dstPxIdx = y *w +x;
			//for(uint8_t i=0;i<3;++i)
			//	inOutImgData[srcPxIdx *4 +i] = imgAreaData.at(dstPxIdx *3 +i);
			/*if(inOutImgData[srcPxIdx *4 +3] == 0.f)
			{
			inOutImgData[srcPxIdx *4 +0] = 0.f;
			inOutImgData[srcPxIdx *4 +1] = 0.f;
			inOutImgData[srcPxIdx *4 +2] = 0.f;
			inOutImgData[srcPxIdx *4 +3] = 1.f;
			}
			else
			{
			inOutImgData[srcPxIdx *4 +0] = 1.f;
			inOutImgData[srcPxIdx *4 +1] = 1.f;
			inOutImgData[srcPxIdx *4 +2] = 1.f;
			inOutImgData[srcPxIdx *4 +3] = 1.f;
			}*/
		}
	}
}

void raytracing::Scene::SetupRenderSettings(
	ccl::Scene &scene,ccl::Session &session,ccl::BufferParams &bufferParams,raytracing::Scene::RenderMode renderMode,
	uint32_t maxTransparencyBounces
) const
{
	// Default parameters taken from Blender
	auto &integrator = *scene.integrator;
	integrator.min_bounce = 0;
	integrator.max_bounce = m_sceneInfo.maxBounces;
	integrator.max_diffuse_bounce = m_sceneInfo.maxDiffuseBounces;
	integrator.max_glossy_bounce = m_sceneInfo.maxGlossyBounces;
	integrator.max_transmission_bounce = m_sceneInfo.maxTransmissionBounces;
	integrator.max_volume_bounce = 0;

	integrator.transparent_min_bounce = 0;
	integrator.transparent_max_bounce = maxTransparencyBounces;

	integrator.volume_max_steps = 1024;
	integrator.volume_step_size = 0.1;

	integrator.caustics_reflective = true;
	integrator.caustics_refractive = true;
	integrator.filter_glossy = 0.f;
	integrator.seed = 0;
	integrator.sampling_pattern = ccl::SamplingPattern::SAMPLING_PATTERN_SOBOL;

	integrator.sample_clamp_direct = 0.f;
	integrator.sample_clamp_indirect = 0.f;
	integrator.motion_blur = false;
	integrator.method = ccl::Integrator::Method::PATH;
	integrator.sample_all_lights_direct = true;
	integrator.sample_all_lights_indirect = true;
	integrator.light_sampling_threshold = 0.f;

	integrator.diffuse_samples = 1;
	integrator.glossy_samples = 1;
	integrator.transmission_samples = 1;
	integrator.ao_samples = 1;
	integrator.mesh_light_samples = 1;
	integrator.subsurface_samples = 1;
	integrator.volume_samples = 1;

	integrator.ao_bounces = 0;
	integrator.tag_update(&scene);

	// Film
	auto &film = *scene.film;
	film.exposure = 1.f;
	film.filter_type = ccl::FilterType::FILTER_GAUSSIAN;
	film.filter_width = 1.5f;
	if(renderMode == raytracing::Scene::RenderMode::RenderImage)
	{
		film.mist_start = 5.f;
		film.mist_depth = 25.f;
		film.mist_falloff = 2.f;
	}
	film.tag_update(&scene);
	film.tag_passes_update(&scene, film.passes);

	film.cryptomatte_depth = 3;
	film.cryptomatte_passes = ccl::CRYPT_NONE;

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
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"combined");
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case raytracing::Scene::RenderMode::SceneNormals:
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"combined");
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case raytracing::Scene::RenderMode::SceneDepth:
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"combined"); // TODO: Why do we need this?
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case raytracing::Scene::RenderMode::RenderImage:
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"combined");
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case raytracing::Scene::RenderMode::BakeAmbientOcclusion:
		ccl::Pass::add(ccl::PassType::PASS_AO,passes,"ao");
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_AO;
		break;
	case raytracing::Scene::RenderMode::BakeDiffuseLighting:
		ccl::Pass::add(ccl::PassType::PASS_DIFFUSE_DIRECT,passes,"diffuse_direct");
		ccl::Pass::add(ccl::PassType::PASS_DIFFUSE_INDIRECT,passes,"diffuse_indirect");
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
		displayPass = ccl::PassType::PASS_COMBINED; // TODO: Is this correct?
		break;
	}
	bufferParams.passes = passes;

	if(m_sceneInfo.motionBlurStrength > 0.f)
	{
		// ccl::Pass::add(ccl::PassType::PASS_MOTION,passes);
		scene.integrator->motion_blur = true;
	}

	film.pass_alpha_threshold = 0.5;
	film.tag_passes_update(&scene, passes);
	film.display_pass = displayPass;
	film.tag_update(&scene);
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

std::shared_ptr<uimg::ImageBuffer> &raytracing::Scene::GetResultImageBuffer(StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	return m_resultImageBuffer.at(umath::to_integral(eye));
}

std::shared_ptr<uimg::ImageBuffer> &raytracing::Scene::GetAlbedoImageBuffer(StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	return m_albedoImageBuffer.at(umath::to_integral(eye));
}

std::shared_ptr<uimg::ImageBuffer> &raytracing::Scene::GetNormalImageBuffer(StereoEye eye)
{
	if(eye == StereoEye::None)
		eye = StereoEye::Left;
	return m_normalImageBuffer.at(umath::to_integral(eye));
}

ccl::BufferParams raytracing::Scene::GetBufferParameters() const
{
	ccl::BufferParams bufferParams {};
	bufferParams.width = m_scene.camera->width;
	bufferParams.height = m_scene.camera->height;
	bufferParams.full_width = m_scene.camera->width;
	bufferParams.full_height = m_scene.camera->height;
	SetupRenderSettings(m_scene,*m_session,bufferParams,m_renderMode,m_sceneInfo.maxTransparencyBounces);
	return bufferParams;
}

void raytracing::Scene::InitializePassShaders(const std::function<std::shared_ptr<GroupNodeDesc>(const Shader&)> &fGetPassDesc)
{
	std::unordered_map<Shader*,std::shared_ptr<CCLShader>> shaderCache;
	for(auto &chunk : m_renderData.modelCache->GetChunks())
	{
		for(auto &o : chunk.GetObjects())
		{
			auto &mesh = o->GetMesh();
			auto &subMeshShaders = mesh.GetSubMeshShaders();
			auto numShaders = subMeshShaders.size();
			std::unordered_map<uint32_t,uint32_t> oldShaderIndexToNewIndex {};
			for(auto i=decltype(numShaders){0u};i<numShaders;++i)
			{
				auto &shader = subMeshShaders.at(i);
				auto albedoPass = fGetPassDesc(*shader);
				if(albedoPass == nullptr)
					continue;

				std::shared_ptr<CCLShader> cclShader = nullptr;
				auto it = shaderCache.find(shader.get());
				if(it != shaderCache.end())
					cclShader = it->second;
				else
				{
					cclShader = CCLShader::Create(*this,*albedoPass);
					cclShader->Finalize(*this);
					shaderCache[shader.get()] = cclShader;
				}
				mesh->used_shaders.at(i) = **cclShader;
			}
			mesh->tag_update(&m_scene,false);
		}
	}
}

void raytracing::Scene::InitializeAlbedoPass(bool reloadShaders)
{
	auto bufferParams = GetBufferParameters();
	uint32_t sampleCount = 1;
	if(m_createInfo.progressive)
	{
		// TODO: We should only need one sample, but for whatever reason some tiles will not get rendered properly if the sample count is too low. The reason for this is unknown
		// UPDATE: This should be fixed now, so this line should no longer be needed!
		sampleCount = 4;
	}
	m_session->params.samples = sampleCount;
	m_session->reset(bufferParams,sampleCount); // We only need the normals and albedo colors for the first sample

	m_scene.lights.clear();

	if(reloadShaders == false)
		return;
	InitializePassShaders([](const Shader &shader) -> std::shared_ptr<GroupNodeDesc> {return shader.albedoPass;});
#if 0
	// Note: For denoising the scene has to be rendered three times:
	// 1) With lighting
	// 2) Albedo colors only
	// 3) Normals only
	// However, Cycles doesn't allow rendering to multiple outputs at once, so we
	// need three separate render passes. For the additional render passes
	// we have to replace the shaders, which is also impossible to do with Cycles.
	// Instead, we have to create an additional set of shaders for each object and
	// re-assign the shader indices of the mesh.
	std::unordered_map<Shader*,std::shared_ptr<CCLShader>> shaderCache;
	for(auto &chunk : m_renderData.modelCache->GetChunks())
	{
		for(auto &o : chunk.GetObjects())
		{
			auto &mesh = o->GetMesh();
			auto &subMeshShaders = mesh.GetSubMeshShaders();
			auto numShaders = subMeshShaders.size();
			std::unordered_map<uint32_t,uint32_t> oldShaderIndexToNewIndex {};
			for(auto i=decltype(numShaders){0u};i<numShaders;++i)
			{
				auto &shader = subMeshShaders.at(i);
				auto &albedoPass = shader->albedoPass;
				if(albedoPass == nullptr)
					continue;

				std::shared_ptr<CCLShader> cclShader = nullptr;
				auto it = shaderCache.find(shader.get());
				if(it != shaderCache.end())
					cclShader = it->second;
				else
				{
					cclShader = CCLShader::Create(*this,*albedoPass);
					cclShader->Finalize(*this);
					shaderCache[shader.get()] = cclShader;
				}

				if(mesh->used_shaders.size() == mesh->used_shaders.capacity())
					mesh->used_shaders.reserve(mesh->used_shaders.size() *1.1 +50);
				mesh->used_shaders.push_back(**cclShader);
				oldShaderIndexToNewIndex[i] = mesh->used_shaders.size() -1;
			}
			auto &origShaderIndexTable = mesh.GetOriginalShaderIndexTable();
			if(origShaderIndexTable.empty())
			{
				// Note: The albedo and normal pass need to overwrite the original shaders for each mesh.
				// We keep a table with the original shader indices so the next pass (usually the normal pass)
				// has a reference to the original shader.

				// TODO: There's no need to store ALL shader indices, this can be optimized!
				origShaderIndexTable.resize(mesh->shader.size());
				for(auto i=decltype(mesh->shader.size()){0};i<mesh->shader.size();++i)
					origShaderIndexTable[i] = mesh->shader[i];
			}
			for(auto i=decltype(mesh->shader.size()){0};i<mesh->shader.size();++i)
			{
				auto &shaderIdx = mesh->shader[i];
				auto it = oldShaderIndexToNewIndex.find(shaderIdx);
				if(it == oldShaderIndexToNewIndex.end())
					continue;
				auto oldShaderIdx = shaderIdx;
				shaderIdx = it->second;
			}
			mesh->tag_update(&m_scene,false);
		}
	}
#endif
}

void raytracing::Scene::InitializeNormalPass(bool reloadShaders)
{
	// Also see raytracing::Scene::CreateShader
	auto bufferParams = GetBufferParameters();
	uint32_t sampleCount = 1;
	if(m_createInfo.progressive)
	{
		// TODO: We should only need one sample, but for whatever reason some tiles will not get rendered properly if the sample count is too low. The reason for this is unknown
		// UPDATE: This should be fixed now, so this line should no longer be needed!
		sampleCount = 4;
	}
	m_session->params.samples = sampleCount;
	m_session->reset(bufferParams,sampleCount); // We only need the normals and albedo colors for the first sample

	// Disable the sky (by making it black)
	auto shader = raytracing::GroupNodeDesc::Create(GetShaderNodeManager());

	auto &nodeOutput = shader->AddNode(NODE_OUTPUT);
	auto &nodeBg = shader->AddNode(NODE_BACKGROUND_SHADER);
	nodeBg.SetProperty(nodes::background_shader::IN_STRENGTH,0.f);

	auto col = shader->CombineRGB(0.f,0.f,0.f);
	shader->Link(col,nodeBg.GetInputSocket(nodes::background_shader::IN_COLOR));
	shader->Link(nodeBg,nodes::background_shader::OUT_BACKGROUND,nodeOutput,nodes::output::IN_SURFACE);

	auto cclShader = CCLShader::Create(*this,*shader);
	cclShader->Finalize(*this);
	m_scene.default_background = **cclShader;
	(*cclShader)->tag_update(&m_scene);

	if(reloadShaders == false)
		return;
	InitializePassShaders([](const Shader &shader) -> std::shared_ptr<GroupNodeDesc> {return shader.normalPass;});
#if 0
	// Note: For denoising the scene has to be rendered three times:
	// 1) With lighting
	// 2) Albedo colors only
	// 3) Normals only
	// However, Cycles doesn't allow rendering to multiple outputs at once, so we
	// need three separate render passes. For the additional render passes
	// we have to replace the shaders, which is also impossible to do with Cycles.
	// Instead, we have to create an additional set of shaders for each object and
	// re-assign the shader indices of the mesh.
	for(auto &chunk : m_renderData.modelCache->GetChunks())
	{
		for(auto &o : chunk.GetObjects())
		{
			auto &mesh = o->GetMesh();
			auto &subMeshShaders = mesh.GetSubMeshShaders();
			auto numShaders = subMeshShaders.size();
			std::unordered_map<uint32_t,uint32_t> oldShaderIndexToNewIndex {};
			for(auto i=decltype(numShaders){0u};i<numShaders;++i)
			{
				auto &shader = subMeshShaders.at(i);
				if(shader->normalPass == nullptr)
					continue;
				auto cclShader = CCLShader::Create(*this,*shader->normalPass);
				cclShader->Finalize(*this);

				if(mesh->used_shaders.size() == mesh->used_shaders.capacity())
					mesh->used_shaders.reserve(mesh->used_shaders.size() *1.1 +50);
				mesh->used_shaders.push_back(**cclShader);
				oldShaderIndexToNewIndex[i] = mesh->used_shaders.size() -1;
			}
			auto &origShaderIndexTable = mesh.GetOriginalShaderIndexTable();
			for(auto i=decltype(mesh->shader.size()){0};i<mesh->shader.size();++i)
			{
				auto shaderIdx = (i < origShaderIndexTable.size()) ? origShaderIndexTable.at(i) : mesh->shader[i];
				auto it = oldShaderIndexToNewIndex.find(shaderIdx);
				if(it == oldShaderIndexToNewIndex.end())
					continue;
				mesh->shader[i] = it->second;
			}
			mesh->tag_update(&m_scene,false);
		}
	}
#endif
}

bool raytracing::Scene::UpdateStereo(raytracing::SceneWorker &worker,ImageRenderStage stage,raytracing::StereoEye &eyeStage)
{
	if(eyeStage == StereoEye::Left)
	{
		// Switch to right eye
		m_camera->SetStereoscopicEye(raytracing::StereoEye::Right);
		ReloadProgressiveRender(false);
		StartNextRenderImageStage(worker,stage,StereoEye::Right);
		return true;
	}
	else if(eyeStage == StereoEye::Right)
	{
		// Switch back to left eye and continue with next stage
		m_camera->SetStereoscopicEye(raytracing::StereoEye::Left);
		eyeStage = StereoEye::Left;
	}
	return false;
}

void raytracing::Scene::Reset()
{
	m_restartState = 1;
	SetCancelled("Cancelled by user");
	m_session->wait();
	m_session->progress.reset();
}
void raytracing::Scene::Restart()
{
	if(m_createInfo.progressive)
		m_tileManager.Reload(false);

	m_session->start();
	m_restartState = 2;
}

void raytracing::Scene::PrepareCyclesSceneForRendering()
{
	if(m_sceneInfo.sky.empty() == false)
		AddSkybox(m_sceneInfo.sky);

	m_sceneInfo.exposure = m_createInfo.exposure;

	m_stateFlags |= StateFlags::HasRenderingStarted;
	auto bufferParams = GetBufferParameters();

	m_session->scene = &m_scene;
	m_session->reset(bufferParams,m_session->params.samples);
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
	m_scene.meshes.reserve(m_scene.meshes.size() +numMeshes);

	for(auto &chunk : mdlCache->GetChunks())
	{
		for(auto &o : chunk.GetObjects())
			m_scene.objects.push_back(**o);
		for(auto &m : chunk.GetMeshes())
			m_scene.meshes.push_back(**m);
	}

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

	if(m_createInfo.progressive)
	{
		auto w = m_scene.camera->width;
		auto h = m_scene.camera->height;
		m_tileManager.Initialize(w,h,GetTileSize().x,GetTileSize().y,m_deviceType == DeviceType::CPU,m_createInfo.exposure,GetGamma(),m_colorTransformProcessor.get());
		bool flipHorizontally = true;
		if(m_scene.camera->type == ccl::CameraType::CAMERA_PANORAMA)
		{
			switch(m_scene.camera->panorama_type)
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

void raytracing::Scene::StartTextureBaking(SceneWorker &worker)
{
	// Baking cannot be done with cycles directly, we will have to
	// do some additional steps first.
	worker.AddThread([this,&worker]() {
		// InitializeAlbedoPass(true);
		//auto bufferParams = m_session->buffers->params;
		auto resolution = GetResolution();
		auto imgWidth = resolution.x;
		auto imgHeight = resolution.y;
		m_scene.bake_manager->set_baking(true);
		m_session->load_kernels();

		static auto albedoOnly = false;
		switch(m_renderMode)
		{
		case RenderMode::BakeAmbientOcclusion:
		case RenderMode::BakeDiffuseLighting:
			if(albedoOnly)
				ccl::Pass::add(ccl::PASS_DIFFUSE_COLOR,m_scene.film->passes);
			else
				ccl::Pass::add(ccl::PASS_LIGHT,m_scene.film->passes);
			break;
		case RenderMode::BakeNormals:
			break;
		}
		
		auto bufferParams = GetBufferParameters();
		if(albedoOnly)
		{
			ccl::vector<ccl::Pass> passes;
			auto displayPass = ccl::PassType::PASS_DIFFUSE_COLOR;
			ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"combined");
			displayPass = ccl::PassType::PASS_COMBINED;
			//bufferParams.passes = passes;
			auto &scene = m_scene;
			auto &film = *m_scene.film;
			film.tag_passes_update(&scene, passes);
			film.display_pass = displayPass;
			//film.tag_update(&scene);
			//scene.integrator->tag_update(&scene);
		}
		else
		{
			ccl::vector<ccl::Pass> passes;
			if(m_renderMode == RenderMode::BakeDiffuseLighting)
			{
				ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes,"combined");
				ccl::Pass::add(ccl::PassType::PASS_LIGHT,passes,"light");
			}
			else
			{
				auto &scene = m_scene;
				auto &film = *m_scene.film;
				ccl::Pass::add(ccl::PassType::PASS_AO,passes,"ao");
				ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
				film.display_pass = ccl::PassType::PASS_AO;
			}
			auto &scene = m_scene;
			auto &film = *m_scene.film;
			film.passes = passes;
			bufferParams.passes = passes;
			film.tag_passes_update(&scene, bufferParams.passes);
			// film.passes
			/*ccl::vector<ccl::Pass> passes;
			ccl::Pass::add(ccl::PassType::PASS_DIFFUSE_DIRECT,passes,"diffuse_direct");
			ccl::Pass::add(ccl::PassType::PASS_DIFFUSE_INDIRECT,passes,"diffuse_indirect");
			ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes,"depth");
			auto displayPass = ccl::PassType::PASS_COMBINED;
			
			bufferParams.passes = passes;
			auto &scene = m_scene;
			auto &film = *m_scene.film;
			film.tag_passes_update(&scene, passes);
			film.display_pass = displayPass;*/
		}

		m_scene.film->tag_update(&m_scene);
		m_scene.integrator->tag_update(&m_scene);
		
		// TODO: Shader limits are arbitrarily chosen, check how Blender does it?
		m_scene.bake_manager->set_shader_limit(256,256);
		m_session->tile_manager.set_samples(m_session->params.samples);
		
		ccl::ShaderEvalType shaderType;
		int bake_pass_filter;
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
		bake_pass_filter = ccl::BakeManager::shader_type_to_pass_filter(shaderType,bake_pass_filter);
		if(worker.IsCancelled())
			return;

		auto numPixels = imgWidth *imgHeight;
		auto *aoTarget = FindObject("bake_target");
		if(aoTarget == nullptr)
		{
			worker.SetStatus(util::JobStatus::Failed,"Invalid bake target!");
			return;
		}
		std::vector<raytracing::baking::BakePixel> pixelArray;
		pixelArray.resize(numPixels);
		auto bakeLightmaps = (m_renderMode == RenderMode::BakeDiffuseLighting);

		raytracing::baking::prepare_bake_data(*this,*aoTarget,pixelArray.data(),numPixels,imgWidth,imgHeight,bakeLightmaps);
		
		if(worker.IsCancelled())
			return;

		auto objectId = aoTarget ? FindCCLObjectId(*aoTarget) : std::optional<uint32_t>{};

		//FindObject
		assert(objectId.has_value());
		ccl::BakeData *bake_data = NULL;
		uint32_t triOffset = 0u;
		// Note: This has been commented because it can cause crashes in some cases. To fix the underlying issue, the mesh for
		// which ao should be baked is now moved to the front so it always has a triangle offset of 0 (See 'SetAOBakeTarget'.).
		/// It would be expected that the triangle offset is relative to the object, but that's not actually the case.
		/// Instead, it seems to be a global triangle offset, so we have to count the number of triangles for all objects
		/// before this one.
		///for(auto i=decltype(objectId){0u};i<objectId;++i)
		///	triOffset += m_objects.at(i)->GetMesh().GetTriangleCount();
		bake_data = m_scene.bake_manager->init(*objectId,triOffset /* triOffset */,numPixels);
		populate_bake_data(bake_data,*objectId,pixelArray.data(),numPixels);

		if(worker.IsCancelled())
			return;

		worker.UpdateProgress(0.2f);

		m_session->tile_manager.set_samples(m_session->params.samples);
		m_session->reset(const_cast<ccl::BufferParams&>(bufferParams), m_session->params.samples);
		m_session->update_scene();

		auto imgBuffer = uimg::ImageBuffer::Create(imgWidth,imgHeight,uimg::ImageBuffer::Format::RGBA_FLOAT);
		std::atomic<bool> running = true;
		//session->progress.set_update_callback(
		std::thread progressThread {[this,&worker,&running]() {
			while(running)
			{
				// Note: get_progress is technically not thread safe, but the worst we can expect is the progress to not be accurate once
				// in a while
				auto progress = m_session->progress.get_progress();
				worker.UpdateProgress(0.2f +(0.95f -0.2f) *progress);
				std::this_thread::sleep_for(std::chrono::seconds{1});
			}
		}};
		auto r = m_scene.bake_manager->bake(m_scene.device,&m_scene.dscene,&m_scene,m_session->progress,shaderType,bake_pass_filter,bake_data,static_cast<float*>(imgBuffer->GetData()));
		running = false;
		progressThread.join();
		if(r == false)
		{
			worker.SetStatus(util::JobStatus::Failed,"Cycles baking has failed for an unknown reason!");
			return;
		}

		if(worker.IsCancelled())
			return;

		worker.UpdateProgress(0.95f);

		if(worker.IsCancelled())
			return;

		// Note: Denoising may not work well with baked images, since we can't supply any geometry information,
		// but the result is decent enough as long as the sample count is high.
		if(ShouldDenoise())
		{
			/*if(m_renderMode == RenderMode::BakeDiffuseLighting)
			{
			auto lightmapInfo = m_lightmapTargetComponent.valid() ? m_lightmapTargetComponent->GetLightmapInfo() : nullptr;
			if(lightmapInfo)
			{
			auto originalResolution = lightmapInfo->atlasSize;
			// All of the lightmaps have to be denoised individually
			for(auto &rect : lightmapInfo->lightmapAtlas)
			{
			auto x = rect.x +lightmapInfo->borderSize;
			auto y = rect.y +lightmapInfo->borderSize;
			auto w = rect.w -lightmapInfo->borderSize *2;
			auto h = rect.h -lightmapInfo->borderSize *2;
			Vector2 offset {
			x /static_cast<float>(originalResolution),
			(originalResolution -y -h) /static_cast<float>(originalResolution) // Note: y is flipped!
			};
			Vector2 size {
			w /static_cast<float>(originalResolution),
			h /static_cast<float>(originalResolution)
			};

			DenoiseHDRImageArea(
			*imgBuffer,imgWidth,imgHeight,
			umath::clamp<float>(umath::round(offset.x *static_cast<float>(imgWidth)),0.f,imgWidth) +0.001f, // Add a small offset to make sure something like 2.9999 isn't truncated to 2
			umath::clamp<float>(umath::round(offset.y *static_cast<float>(imgHeight)),0.f,imgHeight) +0.001f,
			umath::clamp<float>(umath::round(size.x *static_cast<float>(imgWidth)),0.f,imgWidth) +0.001f,
			umath::clamp<float>(umath::round(size.y *static_cast<float>(imgHeight)),0.f,imgHeight) +0.001f
			);
			}
			}
			}
			else*/
			{
				// TODO: Check if denoise flag is set
				// Denoise the result. This has to be done before applying the margin! (Otherwise noise may flow into the margin)

				worker.SetResultMessage("Denoising...");
				DenoiseInfo denoiseInfo {};
				denoiseInfo.hdr = true;
				denoiseInfo.lightmap = bakeLightmaps;
				denoiseInfo.width = imgWidth;
				denoiseInfo.height = imgHeight;
				denoise(denoiseInfo,*imgBuffer,nullptr,nullptr,[this,&worker](float progress) -> bool {
					worker.UpdateProgress(0.95f +progress *0.2f);
					return !worker.IsCancelled();
				});
			}
		}

		if(worker.IsCancelled())
			return;

		// Applying color transform
		if(m_renderMode == RenderMode::BakeDiffuseLighting)
		{
			worker.SetResultMessage("Applying color transform...");
			if(m_createInfo.colorTransform.has_value())
			{
				std::string err;
				ColorTransformProcessorCreateInfo createInfo {};
				createInfo.config = m_createInfo.colorTransform->config;
				createInfo.lookName = m_createInfo.colorTransform->lookName;
				m_colorTransformProcessor = create_color_transform_processor(createInfo,err);
				if(m_colorTransformProcessor == nullptr)
					HandleError("Unable to initialize color transform processor: " +err);
				else
					m_colorTransformProcessor->Apply(*imgBuffer,err,10.f /* exposure */,1.f /* gamma correction */);
			}
			if(worker.IsCancelled())
				return;
		}

		// if(m_renderMode == RenderMode::BakeDiffuseLighting)
		{
			worker.SetResultMessage("Baking margin...");

			imgBuffer->Convert(uimg::ImageBuffer::Format::RGBA_FLOAT);
			raytracing::baking::ImBuf ibuf {};
			ibuf.x = imgWidth;
			ibuf.y = imgHeight;
			ibuf.rect = imgBuffer;

			// Apply margin
			// TODO: Margin only required for certain bake types?
			std::vector<uint8_t> mask_buffer {};
			mask_buffer.resize(numPixels);
			constexpr auto margin = 16u;
			RE_bake_mask_fill(pixelArray, numPixels, reinterpret_cast<char*>(mask_buffer.data()));
			RE_bake_margin(&ibuf, mask_buffer, margin);

			if(worker.IsCancelled())
				return;
		}

		ApplyPostProcessing(*imgBuffer,m_renderMode);

		if(worker.IsCancelled())
			return;

		if(umath::is_flag_set(m_stateFlags,StateFlags::OutputResultWithHDRColors) == false)
		{
			// Convert baked data to rgba8
			auto imgBufLDR = imgBuffer->Copy(uimg::ImageBuffer::Format::RGBA_LDR);
			auto numChannels = umath::to_integral(uimg::ImageBuffer::Channel::Count);
			auto itSrc = imgBuffer->begin();
			for(auto &pxViewDst : *imgBufLDR)
			{
				auto &pxViewSrc = *itSrc;
				for(auto i=decltype(numChannels){0u};i<numChannels;++i)
					pxViewDst.SetValue(static_cast<uimg::ImageBuffer::Channel>(i),baking::unit_float_to_uchar_clamp(pxViewSrc.GetFloatValue(static_cast<uimg::ImageBuffer::Channel>(i))));
				++itSrc;
			}
			imgBuffer = imgBufLDR;
			//auto f = FileManager::OpenFile<VFilePtrReal>("test_ao.png","wb");
			//uimg::save_image(f,*imgBuffer,uimg::ImageFormat::PNG);
			//f = nullptr;
		}
		else
		{
			// Image data is float data, but we only need 16 bits for our purposes
			auto imgBufHDR = imgBuffer->Copy(uimg::ImageBuffer::Format::RGBA_HDR);
			auto numChannels = umath::to_integral(uimg::ImageBuffer::Channel::Count);
			auto itSrc = imgBuffer->begin();
			for(auto &pxViewDst : *imgBufHDR)
			{
				auto &pxViewSrc = *itSrc;
				for(auto i=decltype(numChannels){0u};i<numChannels;++i)
					pxViewDst.SetValue(static_cast<uimg::ImageBuffer::Channel>(i),static_cast<uint16_t>(umath::float32_to_float16_glm(pxViewSrc.GetFloatValue(static_cast<uimg::ImageBuffer::Channel>(i)))));
				++itSrc;
			}
			imgBuffer = imgBufHDR;
			//auto f = FileManager::OpenFile<VFilePtrReal>("test_lightmap.png","wb");
			//uimg::save_image(f,*imgBuffer,uimg::ImageFormat::PNG);
			//f = nullptr;
		}

		if(worker.IsCancelled())
			return;

		GetResultImageBuffer() = imgBuffer;
		// m_session->params.write_render_cb(static_cast<ccl::uchar*>(imgBuffer->GetData()),imgWidth,imgHeight,4 /* channels */);
		m_session->params.write_render_cb = nullptr; // Make sure it's not called on destruction
		worker.SetStatus(util::JobStatus::Successful,"Baking has been completed successfully!");
		worker.UpdateProgress(1.f);
	});
}

void raytracing::Scene::ReloadProgressiveRender(bool clearExposure,bool waitForPreviousCompletion)
{
	if(m_createInfo.progressive == false)
		return;
	m_tileManager.Reload(waitForPreviousCompletion);
	if(clearExposure)
		m_tileManager.SetExposure(1.f);
	m_createInfo.progressiveRefine = false;
	m_session->progress.reset();
}

static void validate_session(ccl::Scene &scene)
{
	for(auto *shader : scene.shaders)
	{
		if(shader->graph == nullptr)
			throw std::logic_error{"Found shader with invalid graph!"};
	}
}
float raytracing::Scene::GetGamma() const {return m_createInfo.hdrOutput ? 1.f : DEFAULT_GAMMA;}
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
	case ImageRenderStage::Bake:
	{
		StartTextureBaking(worker);
		break;
	}
	case ImageRenderStage::Lighting:
	{
		if(IsProgressiveRefine())
			m_progressiveRunning = true;
		worker.AddThread([this,&worker,stage,eyeStage]() {
			validate_session(m_scene);
			m_session->start();

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

void raytracing::Scene::SetCancelled(const std::string &msg)
{
	if(m_session == nullptr)
		return;
	m_session->progress.set_cancel(msg);
	m_tileManager.Cancel();
}

void raytracing::Scene::WaitForRenderStage(SceneWorker &worker,float baseProgress,float progressMultiplier,const std::function<RenderStageResult()> &fOnComplete)
{
	for(;;)
	{
		worker.UpdateProgress(baseProgress +m_session->progress.get_progress() *progressMultiplier);

		if(worker.IsCancelled())
			SetCancelled("Cancelled by application.");

		if(m_restartState != 0)
		{
			while(m_restartState != 2);
			m_restartState = 0;
			continue;
		}
		if(IsProgressiveRefine() && m_progressiveRunning == false)
			break;
		if(m_session->progress.get_cancel())
		{
			if(m_restartState != 0)
				continue;
			std::cerr<<"WARNING: Cycles rendering has been cancelled: "<<m_session->progress.get_cancel_message()<<std::endl;
			worker.Cancel(m_session->progress.get_cancel_message());
			break;
		}
		if(m_session->progress.get_error())
		{
			std::string status;
			std::string subStatus;
			m_session->progress.get_status(status,subStatus);
			std::cerr<<"WARNING: Cycles rendering has failed at status '"<<status<<"' ("<<subStatus<<") with error: "<<m_session->progress.get_error_message()<<std::endl;
			worker.SetStatus(util::JobStatus::Failed,m_session->progress.get_error_message());
			break;
		}
		if(m_session->progress.get_progress() == 1.f)
			break;
		std::this_thread::sleep_for(std::chrono::seconds{1});
	}
	if(worker.GetStatus() == util::JobStatus::Pending && fOnComplete != nullptr && fOnComplete() == RenderStageResult::Continue)
		return;
	if(worker.GetStatus() == util::JobStatus::Pending)
		worker.SetStatus(util::JobStatus::Successful);
}

std::optional<uint32_t> raytracing::Scene::FindCCLObjectId(const Object &o) const
{
	auto it = std::find(m_scene.objects.begin(),m_scene.objects.end(),*o);
	return (it != m_scene.objects.end()) ? (it -m_scene.objects.begin()) : std::optional<uint32_t>{};
}

raytracing::Object *raytracing::Scene::FindObject(const std::string &objectName) const
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

const std::vector<raytracing::PLight> &raytracing::Scene::GetLights() const {return const_cast<Scene*>(this)->GetLights();}
std::vector<raytracing::PLight> &raytracing::Scene::GetLights() {return m_lights;}

util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> raytracing::Scene::Finalize()
{
	auto job = util::create_parallel_job<SceneWorker>(*this);
	auto &worker = static_cast<SceneWorker&>(job.GetWorker());
	StartNextRenderImageStage(worker,ImageRenderStage::InitializeScene,StereoEye::None);
	return job;
}

bool raytracing::Scene::IsProgressive() const {return m_createInfo.progressive;}
bool raytracing::Scene::IsProgressiveRefine() const {return m_createInfo.progressiveRefine;}
void raytracing::Scene::StopRendering()
{
	m_progressiveCondition.notify_one();
	m_progressiveRunning = false;
}
raytracing::Scene::RenderMode raytracing::Scene::GetRenderMode() const {return m_renderMode;}
float raytracing::Scene::GetProgress() const
{
	return m_session->progress.get_progress();
}
void raytracing::Scene::OnParallelWorkerCancelled()
{
	SetCancelled();
	// m_session->set_pause(true);
	// StopRendering();
}
void raytracing::Scene::Wait()
{
	if(m_session)
		m_session->wait();
}

void raytracing::Scene::SetLightIntensityFactor(float f) {m_sceneInfo.lightIntensityFactor = f;}
float raytracing::Scene::GetLightIntensityFactor() const {return m_sceneInfo.lightIntensityFactor;}

static bool g_verbose = false;
void raytracing::Scene::SetVerbose(bool verbose) {g_verbose = verbose;}
bool raytracing::Scene::IsVerbose() {return g_verbose;}

static constexpr std::array<char,3> SERIALIZATION_HEADER = {'R','T','D'};
static constexpr std::array<char,4> MODEL_CACHE_HEADER = {'R','T','M','C'};
void raytracing::Scene::Save(DataStream &dsOut,const std::string &rootDir,const SerializationData &serializationData) const
{
	auto modelCachePath = rootDir +"cache/";
	FileManager::CreateSystemDirectory(modelCachePath.c_str());

	dsOut->SetOffset(0);
	dsOut->Write(reinterpret_cast<const uint8_t*>(SERIALIZATION_HEADER.data()),SERIALIZATION_HEADER.size() *sizeof(SERIALIZATION_HEADER.front()));
	dsOut->Write(SERIALIZATION_VERSION);
	m_createInfo.Serialize(dsOut);
	dsOut->Write(m_renderMode);
	dsOut->WriteString(serializationData.outputFileName);

	auto absSky = get_abs_sky_path(m_sceneInfo.sky);
	dsOut->WriteString(absSky.has_value() ? ToRelativePath(*absSky) : "");
	dsOut->Write(reinterpret_cast<const uint8_t*>(&m_sceneInfo.skyAngles),sizeof(SceneInfo) -offsetof(SceneInfo,skyAngles));

	dsOut->Write(m_stateFlags);
	
	dsOut->Write<uint32_t>(m_mdlCaches.size());
	for(auto &mdlCache : m_mdlCaches)
	{
		// Try to create a reasonable has to identify the cache
		auto &chunks = mdlCache->GetChunks();
		size_t hash = 0;
		if(mdlCache->IsUnique())
		{
			std::random_device rd;
			std::default_random_engine generator(rd());
			std::uniform_int_distribution<long long unsigned> distribution(0,0xFFFFFFFFFFFFFFFF);
			hash = util::hash_combine<uint64_t>(0u,distribution(generator)); // Not the best solution, but extremely unlikely to cause collisions
		}
		else
		{
			hash = util::hash_combine<uint64_t>(0u,chunks.size());
			for(auto &chunk : chunks)
			{
				auto &objects = chunk.GetObjects();
				auto &meshes = chunk.GetMeshes();
				hash = util::hash_combine<uint64_t>(hash,objects.size());
				hash = util::hash_combine<uint64_t>(hash,meshes.size());
				for(auto &m : meshes)
				{
					hash = util::hash_combine<std::string>(hash,m->GetName());
					hash = util::hash_combine<uint64_t>(hash,m->GetVertexCount());
					hash = util::hash_combine<uint64_t>(hash,m->GetTriangleCount());
				}
			}
		}
		auto mdlCachePath = modelCachePath +std::to_string(hash) +".prtc";
		if(FileManager::ExistsSystem(mdlCachePath) == false)
		{
			DataStream mdlCacheStream {};
			mdlCacheStream->SetOffset(0);
			mdlCache->Serialize(mdlCacheStream);
			auto f = FileManager::OpenSystemFile(mdlCachePath.c_str(),"wb");
			if(f)
			{
				f->Write(reinterpret_cast<const uint8_t*>(MODEL_CACHE_HEADER.data()),MODEL_CACHE_HEADER.size() *sizeof(MODEL_CACHE_HEADER.front()));
				f->Write(SERIALIZATION_VERSION);
				f->Write(mdlCacheStream->GetData(),mdlCacheStream->GetInternalSize());
			}
		}
		dsOut->Write<size_t>(hash);
	}

	//for(auto &mdlCache : m_mdlCaches)
	//	m_renderData.modelCache->Merge(*mdlCache);
	//m_renderData.modelCache->Bake();

	dsOut->Write<uint32_t>(m_lights.size());
	for(auto &light : m_lights)
		light->Serialize(dsOut);

	m_camera->Serialize(dsOut);
}
bool raytracing::Scene::ReadSerializationHeader(DataStream &dsIn,RenderMode &outRenderMode,CreateInfo &outCreateInfo,SerializationData &outSerializationData,uint32_t &outVersion,SceneInfo *optOutSceneInfo)
{
	std::array<char,3> header {};
	dsIn->Read(reinterpret_cast<uint8_t*>(&header),sizeof(header));
	if(header != SERIALIZATION_HEADER)
		return false;
	auto version = dsIn->Read<uint32_t>();
	if(version > SERIALIZATION_VERSION || version < 3)
		return false;
	outVersion = version;
	outCreateInfo.Deserialize(dsIn);
	outRenderMode = dsIn->Read<RenderMode>();
	outSerializationData.outputFileName = dsIn->ReadString();

	if(optOutSceneInfo)
	{
		optOutSceneInfo->sky = ToAbsolutePath(dsIn->ReadString());
		dsIn->Read(&optOutSceneInfo->skyAngles,sizeof(SceneInfo) -offsetof(SceneInfo,skyAngles));
	}
	return true;
}
bool raytracing::Scene::Load(DataStream &dsIn,const std::string &rootDir)
{
	auto modelCachePath = rootDir +"cache/";
	dsIn->SetOffset(0);

	SerializationData serializationData;
	uint32_t version;
	CreateInfo createInfo {};
	if(ReadSerializationHeader(dsIn,m_renderMode,createInfo,serializationData,version,&m_sceneInfo) == false)
		return false;
	m_stateFlags = dsIn->Read<decltype(m_stateFlags)>();

	auto numCaches = dsIn->Read<uint32_t>();
	m_mdlCaches.reserve(numCaches);
	for(auto i=decltype(numCaches){0u};i<numCaches;++i)
	{
		auto hash = dsIn->Read<size_t>();
		auto mdlCachePath = modelCachePath +std::to_string(hash) +".prtc";
		auto f = FileManager::OpenSystemFile(mdlCachePath.c_str(),"rb");
		if(f)
		{
			std::array<char,4> header {};
			f->Read(reinterpret_cast<uint8_t*>(&header),sizeof(header));
			if(header != MODEL_CACHE_HEADER)
				continue;
			uint32_t version;
			f->Read(&version,sizeof(version));
			if(version > SERIALIZATION_VERSION || version < 3)
				continue;
			DataStream dsMdlCache {};
			dsMdlCache->Resize(f->GetSize() -f->Tell());
			f->Read(dsMdlCache->GetData(),f->GetSize() -f->Tell());
			auto mdlCache = ModelCache::Create(dsMdlCache,GetShaderNodeManager());
			if(mdlCache)
				m_mdlCaches.push_back(mdlCache);
		}
	}

	auto numLights = dsIn->Read<uint32_t>();
	m_lights.reserve(numLights);
	for(auto i=decltype(numLights){0u};i<numLights;++i)
		m_lights.push_back(Light::Create(version,dsIn));

	m_camera->Deserialize(version,dsIn);
	return true;
}

void raytracing::Scene::HandleError(const std::string &errMsg) const
{
	std::cerr<<errMsg<<std::endl;
}
raytracing::NodeManager &raytracing::Scene::GetShaderNodeManager() const {return *m_nodeManager;}

void raytracing::Scene::SetSky(const std::string &skyPath) {m_sceneInfo.sky = skyPath;}
void raytracing::Scene::SetSkyAngles(const EulerAngles &angSky) {m_sceneInfo.skyAngles = angSky;}
void raytracing::Scene::SetSkyStrength(float strength) {m_sceneInfo.skyStrength = strength;}
void raytracing::Scene::SetEmissionStrength(float strength) {m_sceneInfo.emissionStrength = strength;}
float raytracing::Scene::GetEmissionStrength() const {return m_sceneInfo.emissionStrength;}
void raytracing::Scene::SetMaxTransparencyBounces(uint32_t maxBounces) {m_sceneInfo.maxTransparencyBounces = maxBounces;}
void raytracing::Scene::SetMaxBounces(uint32_t maxBounces) {m_sceneInfo.maxBounces = maxBounces;}
void raytracing::Scene::SetMaxDiffuseBounces(uint32_t bounces) {m_sceneInfo.maxDiffuseBounces = bounces;}
void raytracing::Scene::SetMaxGlossyBounces(uint32_t bounces) {m_sceneInfo.maxGlossyBounces = bounces;}
void raytracing::Scene::SetMaxTransmissionBounces(uint32_t bounces) {m_sceneInfo.maxTransmissionBounces = bounces;}
void raytracing::Scene::SetMotionBlurStrength(float strength) {m_sceneInfo.motionBlurStrength = strength;}
void raytracing::Scene::SetAOBakeTarget(Object &o) {o.SetName("bake_target");}
std::vector<raytracing::TileManager::TileData> raytracing::Scene::GetRenderedTileBatch() {return m_tileManager.GetRenderedTileBatch();}
Vector2i raytracing::Scene::GetTileSize() const
{
	return {m_session->params.tile_size.x,m_session->params.tile_size.y};
}
Vector2i raytracing::Scene::GetResolution() const
{
	return {(*m_camera)->width,(*m_camera)->height};
}

ccl::Session *raytracing::Scene::GetCCLSession() {return m_session.get();}

ccl::Scene *raytracing::Scene::operator->() {return &m_scene;}
ccl::Scene *raytracing::Scene::operator*() {return &m_scene;}

ccl::ShaderOutput *raytracing::Scene::FindShaderNodeOutput(ccl::ShaderNode &node,const std::string &output)
{
	auto it = std::find_if(node.outputs.begin(),node.outputs.end(),[&output](const ccl::ShaderOutput *shOutput) {
		return ccl::string_iequals(shOutput->socket_type.name.string(),output);
		});
	return (it != node.outputs.end()) ? *it : nullptr;
}

ccl::ShaderNode *raytracing::Scene::FindShaderNode(ccl::ShaderGraph &graph,const std::string &nodeName)
{
	auto it = std::find_if(graph.nodes.begin(),graph.nodes.end(),[&nodeName](const ccl::ShaderNode *node) {
		return node->name == nodeName;
		});
	return (it != graph.nodes.end()) ? *it : nullptr;
}

ccl::float3 raytracing::Scene::ToCyclesVector(const Vector3 &v)
{
	return ccl::float3{v.x,v.y,v.z};
}

Vector3 raytracing::Scene::ToPragmaPosition(const ccl::float3 &pos)
{
	auto scale = util::pragma::units_to_metres(1.f);
	Vector3 prPos {pos.x,pos.z,-pos.y};
	prPos /= scale;
	return prPos;
}

ccl::float3 raytracing::Scene::ToCyclesPosition(const Vector3 &pos)
{
	auto scale = util::pragma::units_to_metres(1.f);
#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	ccl::float3 cpos {pos.x,-pos.z,pos.y};
#else
	ccl::float3 cpos {-pos.x,pos.y,pos.z};
#endif
	cpos *= scale;
	return cpos;
}

ccl::float3 raytracing::Scene::ToCyclesNormal(const Vector3 &n)
{
#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	return ccl::float3{n.x,-n.z,n.y};
#else
	return ccl::float3{-n.x,n.y,n.z};
#endif
}

ccl::float2 raytracing::Scene::ToCyclesUV(const Vector2 &uv)
{
	return ccl::float2{uv.x,1.f -uv.y};
}

ccl::Transform raytracing::Scene::ToCyclesTransform(const umath::ScaledTransform &t,bool applyRotOffset)
{
	Vector3 axis;
	float angle;
	uquat::to_axis_angle(t.GetRotation(),axis,angle);
	auto cclT = ccl::transform_identity();
	cclT = cclT *ccl::transform_rotate(angle,Scene::ToCyclesNormal(axis));
	if(applyRotOffset)
		cclT = cclT *ccl::transform_rotate(umath::deg_to_rad(90.f),ccl::float3{1.f,0.f,0.f});
	cclT = ccl::transform_translate(Scene::ToCyclesPosition(t.GetOrigin())) *cclT;
	cclT = cclT *ccl::transform_scale(Scene::ToCyclesVector(t.GetScale()));
	return cclT;
}

float raytracing::Scene::ToCyclesLength(float len)
{
	auto scale = util::pragma::units_to_metres(1.f);
	return len *scale;
}

std::string raytracing::Scene::ToRelativePath(const std::string &absPath)
{
	util::Path path {absPath};
	path.MakeRelative(FileManager::GetRootPath());
	while(path.GetFront() != "materials")
		path.PopFront();
	return path.GetString();
}

std::string raytracing::Scene::ToAbsolutePath(const std::string &relPath)
{
	std::string rpath;
	auto result = FileManager::FindAbsolutePath(relPath,rpath);
	if(IsVerbose())
	{
		if(result)
			std::cout<<"Resolved relative path '"<<relPath<<"' to absolute path '"<<rpath<<"'..."<<std::endl;
		//else
		//	std::cout<<"Unable to resolve relative path '"<<relPath<<"': File not found!"<<std::endl;
	}
	if(result == false)
		std::cout<<"WARNING: Unable to locate file '"<<relPath<<"': File not found!"<<std::endl;
	return result ? rpath : (FileManager::GetRootPath() +relPath);
}

void raytracing::Scene::AddModelsFromCache(const ModelCache &cache)
{
	if(m_mdlCaches.size() == m_mdlCaches.capacity())
		m_mdlCaches.reserve(m_mdlCaches.size() *1.5 +10);
	m_mdlCaches.push_back(const_cast<ModelCache&>(cache).shared_from_this());
}
void raytracing::Scene::AddLight(Light &light)
{
	if(m_lights.size() == m_lights.capacity())
		m_lights.reserve(m_lights.size() *1.5 +50);
	m_lights.push_back(light.shared_from_this());
}
#pragma optimize("",on)
