/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing.hpp"
#include "util_raytracing/scene.hpp"
#include "util_raytracing/mesh.hpp"
#include "util_raytracing/camera.hpp"
#include "util_raytracing/shader.hpp"
#include "util_raytracing/object.hpp"
#include "util_raytracing/light.hpp"
#include "util_raytracing/denoise.hpp"
#include "util_raytracing/model_cache.hpp"
#include "util_raytracing/color_management.hpp"
#include "util_raytracing/renderer.hpp"
#include <optional>
#include <fsys/filesystem.h>
#include <sharedutils/datastream.h>
#include <sharedutils/util_file.h>
#include <sharedutils/util.h>
#include <sharedutils/util_path.hpp>
#include <sharedutils/magic_enum.hpp>
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


void unirender::serialize_udm_property(DataStream &dsOut,const udm::Property &prop)
{
	std::stringstream ss;
	ufile::OutStreamFile f {std::move(ss)};
	prop.Write(f);
	dsOut->Write<size_t>(f.GetSize());

	std::vector<uint8_t> data;
	data.resize(f.GetSize());
	ss = f.MoveStream();
	ss.seekg(0,std::ios_base::beg);
	ss.read(reinterpret_cast<char*>(data.data()),data.size());
	dsOut->Write(data.data(),data.size());
}
void unirender::deserialize_udm_property(DataStream &dsIn,udm::Property &prop)
{
	auto size = dsIn->Read<size_t>();
	ufile::VectorFile f {size};
	dsIn->Read(f.GetData(),size);
	prop.Read(f);
}

void unirender::Scene::CreateInfo::Serialize(DataStream &ds) const
{
	auto prop = udm::Property::Create<udm::Element>();
	auto &udmEl = prop->GetValue<udm::Element>();
	udm::LinkedPropertyWrapper udm {*prop};

	udm["renderer"] = renderer;
	if(samples.has_value())
		udm["samples"] = *samples;
	udm["hdrOutput"] = hdrOutput;
	udm["denoiseMode"] = udm::enum_to_string(denoiseMode);
	udm["progressive"] = progressive;
	udm["progressiveRefine"] = progressiveRefine;
	udm["deviceType"] = udm::enum_to_string(deviceType);
	udm["exposure"] = exposure;
	udm["preCalculateLight"] = preCalculateLight;

	if(colorTransform.has_value())
	{
		auto udmColorTransform = udm["colorTransform"];
		udmColorTransform["config"] = colorTransform->config;
		if(colorTransform->lookName.has_value())
			udmColorTransform["lookName"] = *colorTransform->lookName;
	}
	serialize_udm_property(ds,*prop);
}
void unirender::Scene::CreateInfo::Deserialize(DataStream &ds,uint32_t version)
{
	auto prop = udm::Property::Create<udm::Element>();
	deserialize_udm_property(ds,*prop);

	auto &udmEl = prop->GetValue<udm::Element>();
	udm::LinkedPropertyWrapper udm {*prop};

	udm["renderer"](renderer);
	if(udm["samples"])
	{
		samples = uint32_t{};
		udm["samples"](*samples);
	}
	udm["hdrOutput"](hdrOutput);
	denoiseMode = udm::string_to_enum(udm["denoiseMode"],util::declvalue(&CreateInfo::denoiseMode));
	udm["progressive"](progressive);
	udm["progressiveRefine"](progressiveRefine);
	deviceType = udm::string_to_enum(udm["deviceType"],util::declvalue(&CreateInfo::deviceType));
	udm["exposure"](exposure);
	udm["preCalculateLight"](preCalculateLight);

	auto udmColorTransform = udm["colorTransform"];
	if(udmColorTransform)
	{
		colorTransform = ColorTransformInfo{};
		udmColorTransform["config"](colorTransform->config);
		auto udmLookName = udmColorTransform["lookName"];
		if(udmLookName)
		{
			colorTransform->lookName = std::string{};
			udmLookName(*colorTransform->lookName);
		}
	}
}

///////////////////

void unirender::Scene::PrintLogInfo()
{
	auto &logHandler = unirender::get_log_handler();
	if(logHandler == nullptr)
		return;
	std::stringstream ss;

	auto &sceneInfo = GetSceneInfo();
	ss<<"Scene Info\n";
	ss<<"Sky: "<<sceneInfo.sky<<"\n";
	ss<<"Sky angles: "<<sceneInfo.skyAngles<<"\n";
	ss<<"Sky strength: "<<sceneInfo.skyStrength<<"\n";
	ss<<"Transparent sky: "<<sceneInfo.transparentSky<<"\n";
	ss<<"Emission strength: "<<sceneInfo.emissionStrength<<"\n";
	ss<<"Light intensity factor: "<<sceneInfo.lightIntensityFactor<<"\n";
	ss<<"Motion blur strength: "<<sceneInfo.motionBlurStrength<<"\n";
	ss<<"Max transparency bounces: "<<sceneInfo.maxTransparencyBounces<<"\n";
	ss<<"Max bounces: "<<sceneInfo.maxBounces<<"\n";
	ss<<"Max diffuse bounces: "<<sceneInfo.maxDiffuseBounces<<"\n";
	ss<<"Max glossy bounces: "<<sceneInfo.maxGlossyBounces<<"\n";
	ss<<"Max transmission bounces: "<<sceneInfo.maxTransmissionBounces<<"\n";
	ss<<"Exposure: "<<sceneInfo.exposure<<"\n";
	logHandler(ss.str());

	ss = {};
	auto &createInfo = GetCreateInfo();
	ss<<"Create Info\n";
	ss<<"Renderer: "<<createInfo.renderer<<"\n";
	ss<<"Samples: ";
	if(createInfo.samples.has_value())
		ss<<*createInfo.samples;
	else
		ss<<"-";
	ss<<"\n";
	ss<<"HDR output: "<<createInfo.hdrOutput<<"\n";
	ss<<"Denoise mode: "<<magic_enum::enum_name(createInfo.denoiseMode)<<"\n";
	ss<<"Progressive: "<<createInfo.progressive<<"\n";
	ss<<"Progressive refine: "<<createInfo.progressiveRefine<<"\n";
	ss<<"Device type: "<<magic_enum::enum_name(createInfo.deviceType)<<"\n";
	ss<<"Exposure: "<<createInfo.exposure<<"\n";
	ss<<"Color transform: ";
	if(createInfo.colorTransform.has_value())
	{
		ss<<createInfo.colorTransform->config;
		if(createInfo.colorTransform->lookName.has_value())
			ss<<"; Look: "<<*createInfo.colorTransform->lookName;
	}
	else
		ss<<"-";
	ss<<"\n";
	ss<<"Render mode: "<<magic_enum::enum_name(m_renderMode)<<"\n";
	logHandler(ss.str());

	ss = {};
	auto &cam = GetCamera();
	uint32_t w,h;
	cam.GetResolution(w,h);
	ss<<"Camera:\n";
	ss<<"Name: "<<cam.GetName()<<"\n";
	ss<<"Resolution: "<<w<<"x"<<h<<"\n";
	ss<<"FarZ: "<<cam.GetFarZ()<<"\n";
	ss<<"NearZ: "<<cam.GetNearZ()<<"\n";
	ss<<"Fov: "<<cam.GetFov()<<"\n";
	ss<<"Type: "<<magic_enum::enum_name(cam.GetType())<<"\n";
	ss<<"Panorama Type: "<<magic_enum::enum_name(cam.GetPanoramaType())<<"\n";
	ss<<"Depth of field enabled: "<<cam.IsDofEnabled()<<"\n";
	ss<<"Focal distance: "<<cam.GetFocalDistance()<<"\n";
	ss<<"Aperture size: "<<cam.GetApertureSize()<<"\n";
	ss<<"Bokeh ratio: "<<cam.GetApertureRatio()<<"\n";
	ss<<"Blae count: "<<cam.GetBladeCount()<<"\n";
	ss<<"Blades rotation: "<<cam.GetBladesRotation()<<"\n";
	ss<<"Stereoscopic: "<<cam.IsStereoscopic()<<"\n";
	ss<<"Interocular distance: "<<cam.GetInterocularDistance()<<"\n";
	ss<<"Aspect ratio: "<<cam.GetAspectRatio()<<"\n";
	ss<<"Longitude: "<<cam.GetLongitudeMin()<<","<<cam.GetLongitudeMax()<<"\n";
	ss<<"Latitude: "<<cam.GetLatitudeMin()<<","<<cam.GetLatitudeMax()<<"\n";
	logHandler(ss.str());

	ss = {};
	ss<<"Lights:\n";
	auto first = true;
	for(auto &l : GetLights())
	{
		if(first)
			first = false;
		else
			ss<<"\n";
		ss<<"Name: "<<l->GetName()<<"\n";
		ss<<"Type: "<<magic_enum::enum_name(l->GetType())<<"\n";
		ss<<"Outer cone angle: "<<l->GetOuterConeAngle()<<"\n";
		ss<<"Blend fraction: "<<l->GetBlendFraction()<<"\n";
		ss<<"Color: "<<l->GetColor()<<"\n";
		ss<<"Intensity: "<<l->GetIntensity()<<"\n";
		ss<<"Size: "<<l->GetSize()<<"\n";
		ss<<"U Axis: "<<l->GetAxisU()<<"\n";
		ss<<"V Axis: "<<l->GetAxisV()<<"\n";
		ss<<"U Size: "<<l->GetSizeU()<<"\n";
		ss<<"V Size: "<<l->GetSizeV()<<"\n";
		ss<<"Round: "<<l->IsRound()<<"\n";
	}
	logHandler(ss.str());
}

bool unirender::Scene::IsLightmapRenderMode(RenderMode renderMode)
{
	return umath::to_integral(renderMode) >= umath::to_integral(RenderMode::LightmapBakingStart) &&
		umath::to_integral(renderMode) <= umath::to_integral(RenderMode::LightmapBakingEnd);
}

bool unirender::Scene::IsBakingRenderMode(RenderMode renderMode)
{
	return umath::to_integral(renderMode) >= umath::to_integral(RenderMode::BakingStart) &&
		umath::to_integral(renderMode) <= umath::to_integral(RenderMode::BakingEnd);
}

bool unirender::Scene::IsRenderSceneMode(RenderMode renderMode)
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

bool unirender::Scene::ReadHeaderInfo(DataStream &ds,RenderMode &outRenderMode,CreateInfo &outCreateInfo,SerializationData &outSerializationData,uint32_t &outVersion,SceneInfo *optOutSceneInfo)
{
	return ReadSerializationHeader(ds,outRenderMode,outCreateInfo,outSerializationData,outVersion,optOutSceneInfo);
}
std::shared_ptr<unirender::Scene> unirender::Scene::Create(NodeManager &nodeManager,DataStream &dsIn,const std::string &rootDir,RenderMode renderMode,const CreateInfo &createInfo)
{
	auto scene = Create(nodeManager,renderMode,createInfo);
	if(scene == nullptr || scene->Load(dsIn,rootDir) == false)
		return nullptr;
	return scene;
}
std::shared_ptr<unirender::Scene> unirender::Scene::Create(NodeManager &nodeManager,DataStream &dsIn,const std::string &rootDir)
{
	RenderMode renderMode;
	CreateInfo createInfo;
	SerializationData serializationData;
	uint32_t version;
	if(ReadSerializationHeader(dsIn,renderMode,createInfo,serializationData,version) == false)
		return nullptr;
	return Create(nodeManager,dsIn,rootDir,renderMode,createInfo);
}

std::shared_ptr<unirender::Scene> unirender::Scene::Create(NodeManager &nodeManager,RenderMode renderMode,const CreateInfo &createInfo)
{
	auto scene = std::shared_ptr<Scene>{new Scene{nodeManager,renderMode}};

	scene->m_camera = Camera::Create(*scene);
	scene->m_createInfo = createInfo;
	umath::set_flag(scene->m_stateFlags,StateFlags::OutputResultWithHDRColors,createInfo.hdrOutput);
	return scene;
}

unirender::Scene::Scene(NodeManager &nodeManager,RenderMode renderMode)
	: m_renderMode{renderMode},m_nodeManager{nodeManager.shared_from_this()}
{}

unirender::Scene::~Scene() {}

unirender::Camera &unirender::Scene::GetCamera() {return *m_camera;}

bool unirender::Scene::IsValidTexture(const std::string &filePath) const
{
	std::string ext;
	if(ufile::get_extension(filePath,&ext) == false || ustring::compare<std::string>(ext,"dds",false) == false)
		return false;
	return FileManager::Exists(filePath,fsys::SearchFlags::Local);
}

std::optional<std::string> unirender::Scene::GetAbsSkyPath(const std::string &skyTex)
{
	if(skyTex.empty())
		return {};
	std::string absPath = skyTex;
	if(FileManager::ExistsSystem(absPath) == false && FileManager::FindAbsolutePath("materials/" +skyTex,absPath) == false)
		return {};
	return absPath;
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

void unirender::Scene::DenoiseHDRImageArea(uimg::ImageBuffer &imgBuffer,uint32_t imgWidth,uint32_t imgHeight,uint32_t xOffset,uint32_t yOffset,uint32_t w,uint32_t h) const
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
	denoise::Info denoiseInfo {};
	denoiseInfo.width = w;
	denoiseInfo.height = h;

	denoise::ImageData denoiseImgData {};
	denoiseImgData.data = reinterpret_cast<uint8_t*>(imgAreaData.data());
	denoiseImgData.format = uimg::Format::RGB32;

	denoise::ImageInputs inputs {};
	inputs.beautyImage = denoiseImgData;
	denoise::denoise(denoiseInfo,inputs,denoiseImgData);

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

void unirender::Scene::Close()
{
	m_mdlCaches.clear();
	m_camera = nullptr;
}

float unirender::Scene::GetGamma() const {return m_createInfo.hdrOutput ? 1.f : DEFAULT_GAMMA;}

void unirender::Scene::AddActorToActorMap(std::unordered_map<size_t,WorldObject*> &map,WorldObject &obj)
{
	map[util::get_uuid_hash(obj.GetUuid())] = &obj;
}
std::unordered_map<size_t,unirender::WorldObject*> unirender::Scene::BuildActorMap() const
{
	std::unordered_map<size_t,unirender::WorldObject*> map;
	auto addActor = [&map](unirender::WorldObject &obj) {
		map[util::get_uuid_hash(obj.GetUuid())] = &obj;
	};
	uint32_t numActors = m_lights.size() +1u /* camera */;
	for(auto &mdlCache : m_mdlCaches)
	{
		for(auto &chunk : mdlCache->GetChunks())
			numActors += chunk.GetObjects().size();
	}
	map.reserve(numActors);
	for(auto &light : m_lights)
		addActor(*light);
	addActor(*m_camera);
	for(auto &mdlCache : m_mdlCaches)
	{
		for(auto &chunk : mdlCache->GetChunks())
		{
			for(auto &obj : chunk.GetObjects())
				addActor(*obj);
		}
	}
	return map;
}

const std::vector<unirender::PLight> &unirender::Scene::GetLights() const {return const_cast<Scene*>(this)->GetLights();}
std::vector<unirender::PLight> &unirender::Scene::GetLights() {return m_lights;}

void unirender::Scene::Finalize()
{
	m_camera->Finalize(*this);

	m_camera->SetId(0);
	uint32_t id = 0;
	for(auto &light : m_lights)
		light->SetId(id++);

	uint32_t meshId = 0;
	uint32_t objId = 0;
	uint32_t shaderId = 0;
	for(auto &mdlCache : GetModelCaches())
	{
		for(auto &chunk : mdlCache->GetChunks())
		{
			for(auto &mesh : chunk.GetMeshes())
				mesh->SetId(meshId++);
			for(auto &obj : chunk.GetObjects())
				obj->SetId(objId++);
			for(auto &shader : chunk.GetShaderCache().GetShaders())
				shader->SetId(shaderId++);
		}
	}
}

bool unirender::Scene::IsProgressive() const {return m_createInfo.progressive;}
bool unirender::Scene::IsProgressiveRefine() const {return m_createInfo.progressiveRefine;}
//unirender::Scene::RenderMode unirender::Scene::GetRenderMode() const {return m_renderMode;}

void unirender::Scene::SetLightIntensityFactor(float f) {m_sceneInfo.lightIntensityFactor = f;}
float unirender::Scene::GetLightIntensityFactor() const {return m_sceneInfo.lightIntensityFactor;}

static bool g_verbose = false;
void unirender::Scene::SetVerbose(bool verbose) {g_verbose = verbose;}
bool unirender::Scene::IsVerbose() {return g_verbose;}

static constexpr std::array<char,3> SERIALIZATION_HEADER = {'R','T','D'};
static constexpr std::array<char,4> MODEL_CACHE_HEADER = {'R','T','M','C'};
void unirender::Scene::Save(DataStream &dsOut,const std::string &rootDir,const SerializationData &serializationData) const
{
	auto modelCachePath = rootDir +"cache/";
	FileManager::CreateSystemDirectory(modelCachePath.c_str());

	dsOut->SetOffset(0);
	dsOut->Write(reinterpret_cast<const uint8_t*>(SERIALIZATION_HEADER.data()),SERIALIZATION_HEADER.size() *sizeof(SERIALIZATION_HEADER.front()));
	dsOut->Write(SERIALIZATION_VERSION);
	m_createInfo.Serialize(dsOut);
	dsOut->Write(m_renderMode);
	dsOut->WriteString(serializationData.outputFileName);

	auto prop = udm::Property::Create<udm::Element>();
	auto &udmEl = prop->GetValue<udm::Element>();
	udm::LinkedPropertyWrapper udm {*prop};

	auto udmScene = udm["sceneInfo"];
	auto udmSky = udmScene["sky"];
	if(!m_sceneInfo.sky.empty())
	{
		auto absSky = GetAbsSkyPath(m_sceneInfo.sky);
		if(absSky.has_value())
			udmSky["absTexture"] = *absSky;
		else
			udmSky["relTexture"] = m_sceneInfo.sky;
	}
	udmSky["angles"] = m_sceneInfo.skyAngles;
	udmSky["strength"] = m_sceneInfo.skyStrength;
	udmSky["transparent"] = m_sceneInfo.transparentSky;

	udmScene["emissionStrength"] = m_sceneInfo.emissionStrength;
	udmScene["lightIntensityFactor"] = m_sceneInfo.lightIntensityFactor;
	udmScene["motionBlurStrength"] = m_sceneInfo.motionBlurStrength;
	
	auto udmLimits = udmScene["limits"];
	udmLimits["maxTransparencyBounces"] = m_sceneInfo.maxTransparencyBounces;
	udmLimits["maxBounces"] = m_sceneInfo.maxBounces;
	udmLimits["maxDiffuseBounces"] = m_sceneInfo.maxDiffuseBounces;
	udmLimits["maxGlossyBounces"] = m_sceneInfo.maxGlossyBounces;
	udmLimits["maxTransmissionBounces"] = m_sceneInfo.maxTransmissionBounces;

	udmScene["exposure"] = m_sceneInfo.exposure;
	udmScene["useAdaptiveSampling"] = m_sceneInfo.useAdaptiveSampling;
	udmScene["adaptiveSamplingThreshold"] = m_sceneInfo.adaptiveSamplingThreshold;
	udmScene["adaptiveMinSamples"] = m_sceneInfo.adaptiveMinSamples;
	serialize_udm_property(dsOut,*prop);

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

	dsOut->Write<bool>(m_bakeTargetName.has_value());
	if(m_bakeTargetName.has_value())
		dsOut->WriteString(*m_bakeTargetName);
}
bool unirender::Scene::ReadSerializationHeader(DataStream &dsIn,RenderMode &outRenderMode,CreateInfo &outCreateInfo,SerializationData &outSerializationData,uint32_t &outVersion,SceneInfo *optOutSceneInfo)
{
	std::array<char,3> header {};
	dsIn->Read(reinterpret_cast<uint8_t*>(&header),sizeof(header));
	if(header != SERIALIZATION_HEADER)
		return false;
	auto version = dsIn->Read<uint32_t>();
	if(version > SERIALIZATION_VERSION || version < 3)
		return false;
	outVersion = version;
	outCreateInfo.Deserialize(dsIn,version);
	outRenderMode = dsIn->Read<RenderMode>();
	outSerializationData.outputFileName = dsIn->ReadString();

	if(optOutSceneInfo)
	{
		auto prop = udm::Property::Create<udm::Element>();
		deserialize_udm_property(dsIn,*prop);

		auto &udmEl = prop->GetValue<udm::Element>();
		udm::LinkedPropertyWrapper udm {*prop};
		
		auto &sceneInfo = *optOutSceneInfo;
		auto udmScene = udm["sceneInfo"];
		auto udmSky = udmScene["sky"];

		auto absTex = udmSky["absTexture"];
		auto relTex = udmSky["relTexture"];
		if(absTex)
			absTex(sceneInfo.sky);
		else if(relTex)
		{
			std::string skyTex;
			relTex(skyTex);
			sceneInfo.sky = ToAbsolutePath(skyTex);
		}

		udmSky["angles"](sceneInfo.skyAngles);
		udmSky["strength"](sceneInfo.skyStrength);
		udmSky["transparent"](sceneInfo.transparentSky);

		udmScene["emissionStrength"](sceneInfo.emissionStrength);
		udmScene["lightIntensityFactor"](sceneInfo.lightIntensityFactor);
		udmScene["motionBlurStrength"](sceneInfo.motionBlurStrength);
	
		auto udmLimits = udmScene["limits"];
		udmLimits["maxTransparencyBounces"](sceneInfo.maxTransparencyBounces);
		udmLimits["maxBounces"](sceneInfo.maxBounces);
		udmLimits["maxDiffuseBounces"](sceneInfo.maxDiffuseBounces);
		udmLimits["maxGlossyBounces"](sceneInfo.maxGlossyBounces);
		udmLimits["maxTransmissionBounces"](sceneInfo.maxTransmissionBounces);

		udmScene["exposure"](sceneInfo.exposure);
		udmScene["useAdaptiveSampling"](sceneInfo.useAdaptiveSampling);
		udmScene["adaptiveSamplingThreshold"](sceneInfo.adaptiveSamplingThreshold);
		udmScene["adaptiveMinSamples"](sceneInfo.adaptiveMinSamples);
	}
	return true;
}
bool unirender::Scene::Load(DataStream &dsIn,const std::string &rootDir)
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

	auto hasBakeTarget = dsIn->Read<bool>();
	if(hasBakeTarget)
		m_bakeTargetName = dsIn->ReadString();
	return true;
}

void unirender::Scene::HandleError(const std::string &errMsg) const
{
	std::cerr<<errMsg<<std::endl;
}
unirender::NodeManager &unirender::Scene::GetShaderNodeManager() const {return *m_nodeManager;}

void unirender::Scene::SetSky(const std::string &skyPath) {m_sceneInfo.sky = skyPath;}
void unirender::Scene::SetSkyAngles(const EulerAngles &angSky) {m_sceneInfo.skyAngles = angSky;}
void unirender::Scene::SetSkyStrength(float strength) {m_sceneInfo.skyStrength = strength;}
void unirender::Scene::SetEmissionStrength(float strength) {m_sceneInfo.emissionStrength = strength;}
float unirender::Scene::GetEmissionStrength() const {return m_sceneInfo.emissionStrength;}
void unirender::Scene::SetMaxTransparencyBounces(uint32_t maxBounces) {m_sceneInfo.maxTransparencyBounces = maxBounces;}
void unirender::Scene::SetMaxBounces(uint32_t maxBounces) {m_sceneInfo.maxBounces = maxBounces;}
void unirender::Scene::SetMaxDiffuseBounces(uint32_t bounces) {m_sceneInfo.maxDiffuseBounces = bounces;}
void unirender::Scene::SetMaxGlossyBounces(uint32_t bounces) {m_sceneInfo.maxGlossyBounces = bounces;}
void unirender::Scene::SetMaxTransmissionBounces(uint32_t bounces) {m_sceneInfo.maxTransmissionBounces = bounces;}
void unirender::Scene::SetMotionBlurStrength(float strength) {m_sceneInfo.motionBlurStrength = strength;}
void unirender::Scene::SetAdaptiveSampling(bool enabled,float adaptiveSamplingThreshold,uint32_t adaptiveMinSamples)
{
	m_sceneInfo.useAdaptiveSampling = enabled;
	m_sceneInfo.adaptiveSamplingThreshold = adaptiveSamplingThreshold;
	m_sceneInfo.adaptiveMinSamples = adaptiveMinSamples;
}
const std::string *unirender::Scene::GetBakeTargetName() const {return m_bakeTargetName.has_value() ? &*m_bakeTargetName : nullptr;}
bool unirender::Scene::HasBakeTarget() const {return m_bakeTargetName.has_value();}
void unirender::Scene::SetBakeTarget(Object &o)
{
	o.SetName("bake_target");
	m_bakeTargetName = "bake_target";
}
Vector2i unirender::Scene::GetResolution() const
{
	return {m_camera->GetWidth(),m_camera->GetHeight()};
}

std::string unirender::Scene::ToRelativePath(const std::string &absPath)
{
	util::Path path {absPath};
	path.MakeRelative(FileManager::GetRootPath());
	while(path.GetFront() != "materials")
		path.PopFront();
	return path.GetString();
}

std::string unirender::Scene::ToAbsolutePath(const std::string &relPath)
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

void unirender::Scene::AddModelsFromCache(const ModelCache &cache)
{
	if(m_mdlCaches.size() == m_mdlCaches.capacity())
		m_mdlCaches.reserve(m_mdlCaches.size() *1.5 +10);
	m_mdlCaches.push_back(const_cast<ModelCache&>(cache).shared_from_this());
}
void unirender::Scene::AddLight(Light &light)
{
	if(m_lights.size() == m_lights.capacity())
		m_lights.reserve(m_lights.size() *1.5 +50);
	m_lights.push_back(light.shared_from_this());
}
