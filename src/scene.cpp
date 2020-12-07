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
void unirender::Scene::CreateInfo::Serialize(DataStream &ds) const
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
void unirender::Scene::CreateInfo::Deserialize(DataStream &ds)
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
	if(ufile::get_extension(filePath,&ext) == false || ustring::compare(ext,"dds",false) == false)
		return false;
	return FileManager::Exists(filePath,fsys::SearchFlags::Local);
}

std::optional<std::string> unirender::Scene::GetAbsSkyPath(const std::string &skyTex)
{
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

void unirender::Scene::Close()
{
	m_mdlCaches.clear();
	m_camera = nullptr;
}

float unirender::Scene::GetGamma() const {return m_createInfo.hdrOutput ? 1.f : DEFAULT_GAMMA;}

const std::vector<unirender::PLight> &unirender::Scene::GetLights() const {return const_cast<Scene*>(this)->GetLights();}
std::vector<unirender::PLight> &unirender::Scene::GetLights() {return m_lights;}

void unirender::Scene::Finalize()
{
	m_camera->Finalize(*this);
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

	auto absSky = GetAbsSkyPath(m_sceneInfo.sky);
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
void unirender::Scene::SetAOBakeTarget(Object &o) {o.SetName("bake_target");}
Vector2i unirender::Scene::GetResolution() const
{
	return {m_camera->GetWidth(),m_camera->GetHeight()};
}

ccl::ShaderOutput *unirender::Scene::FindShaderNodeOutput(ccl::ShaderNode &node,const std::string &output)
{
	auto it = std::find_if(node.outputs.begin(),node.outputs.end(),[&output](const ccl::ShaderOutput *shOutput) {
		return ccl::string_iequals(shOutput->socket_type.name.string(),output);
		});
	return (it != node.outputs.end()) ? *it : nullptr;
}

ccl::ShaderNode *unirender::Scene::FindShaderNode(ccl::ShaderGraph &graph,const std::string &nodeName)
{
	auto it = std::find_if(graph.nodes.begin(),graph.nodes.end(),[&nodeName](const ccl::ShaderNode *node) {
		return node->name == nodeName;
		});
	return (it != graph.nodes.end()) ? *it : nullptr;
}

ccl::float3 unirender::Scene::ToCyclesVector(const Vector3 &v)
{
	return ccl::float3{v.x,v.y,v.z};
}

Vector3 unirender::Scene::ToPragmaPosition(const ccl::float3 &pos)
{
	auto scale = util::pragma::units_to_metres(1.f);
	Vector3 prPos {pos.x,pos.z,-pos.y};
	prPos /= scale;
	return prPos;
}

ccl::float3 unirender::Scene::ToCyclesPosition(const Vector3 &pos)
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

ccl::float3 unirender::Scene::ToCyclesNormal(const Vector3 &n)
{
#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	return ccl::float3{n.x,-n.z,n.y};
#else
	return ccl::float3{-n.x,n.y,n.z};
#endif
}

ccl::float2 unirender::Scene::ToCyclesUV(const Vector2 &uv)
{
	return ccl::float2{uv.x,1.f -uv.y};
}

ccl::Transform unirender::Scene::ToCyclesTransform(const umath::ScaledTransform &t,bool applyRotOffset)
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

float unirender::Scene::ToCyclesLength(float len)
{
	auto scale = util::pragma::units_to_metres(1.f);
	return len *scale;
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
#pragma optimize("",on)
