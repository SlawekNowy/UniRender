/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
*/

#include "util_raytracing/scene.hpp"
#include "util_raytracing/denoise.hpp"
#include <util_image_buffer.hpp>
#include <OpenImageDenoise/oidn.hpp>
#include <util/util_half.h>
#include <iostream>

#pragma optimize("",off)
unirender::Denoiser::Denoiser()
{
	auto device = oidn::newDevice();
	const char *errMsg;
	if(device.getError(errMsg) != oidn::Error::None)
		return;
	/*device.setErrorFunction([](void *userPtr,oidn::Error code,const char *message) {
		std::cout<<"Error: "<<message<<std::endl;
		});
	device.set("verbose",true);*/
	device.commit();
	m_device = std::make_unique<oidn::DeviceRef>(device);
}

bool unirender::Denoiser::Denoise(
	const DenoiseInfo &denoise,float *inOutData,
	float *optAlbedoData,float *optInNormalData,
	const std::function<bool(float)> &fProgressCallback,
	float *optImgOutData
)
{
	if(m_device == nullptr)
		return false;
	oidn::FilterRef filter = m_device->newFilter(denoise.lightmap ? "RTLightmap" : "RT");

	filter.setImage("color",inOutData,oidn::Format::Float3,denoise.width,denoise.height);
	if(denoise.lightmap == false)
	{
		if(optAlbedoData)
			filter.setImage("albedo",optAlbedoData,oidn::Format::Float3,denoise.width,denoise.height);
		if(optInNormalData)
			filter.setImage("normal",optInNormalData,oidn::Format::Float3,denoise.width,denoise.height);

		filter.set("hdr",denoise.hdr);
	}
	if(optImgOutData)
		filter.setImage("output",optImgOutData,oidn::Format::Float3,denoise.width,denoise.height);
	else
		filter.setImage("output",inOutData,oidn::Format::Float3,denoise.width,denoise.height);

	std::unique_ptr<std::function<bool(float)>> ptrProgressCallback = nullptr;
	if(fProgressCallback)
	{
		ptrProgressCallback = std::make_unique<std::function<bool(float)>>(fProgressCallback);
		filter.setProgressMonitorFunction([](void *userPtr,double n) -> bool {
			auto *ptrProgressCallback = static_cast<std::function<bool(float)>*>(userPtr);
			if(ptrProgressCallback)
				return (*ptrProgressCallback)(n);
			return true;
		},ptrProgressCallback.get());
	}

	filter.commit();

	filter.execute();
	return true;
}
bool unirender::Denoiser::Denoise(
	const DenoiseInfo &denoiseInfo,uimg::ImageBuffer &imgBuffer,
	uimg::ImageBuffer *optImgBufferAlbedo,
	uimg::ImageBuffer *optImgBufferNormal,
	const std::function<bool(float)> &fProgressCallback,
	uimg::ImageBuffer *optImgBufferDst
)
{
	if(imgBuffer.GetFormat() == uimg::ImageBuffer::Format::RGB_FLOAT && (optImgBufferAlbedo == nullptr || optImgBufferAlbedo->GetFormat() == imgBuffer.GetFormat()) && (optImgBufferNormal == nullptr || optImgBufferNormal->GetFormat() == imgBuffer.GetFormat()))
	{
		 // Image is already in the right format, we can just denoise and be done with it
		return Denoise(
			denoiseInfo,static_cast<float*>(imgBuffer.GetData()),
			optImgBufferAlbedo ? static_cast<float*>(optImgBufferAlbedo->GetData()) : nullptr,optImgBufferNormal ? static_cast<float*>(optImgBufferNormal->GetData()) : nullptr,
			fProgressCallback,optImgBufferDst ? static_cast<float*>(optImgBufferDst->GetData()) : nullptr
		);
	}

	// Image is in the wrong format, we'll need a temporary copy
	auto pImgDenoise = imgBuffer.Copy(uimg::ImageBuffer::Format::RGB_FLOAT);
	auto pImgAlbedo = optImgBufferAlbedo ? optImgBufferAlbedo->Copy(uimg::ImageBuffer::Format::RGB_FLOAT) : nullptr;
	auto pImgNormals = optImgBufferNormal ? optImgBufferNormal->Copy(uimg::ImageBuffer::Format::RGB_FLOAT) : nullptr;
	if(Denoise(
		denoiseInfo,static_cast<float*>(pImgDenoise->GetData()),
		pImgAlbedo ? static_cast<float*>(pImgAlbedo->GetData()) : nullptr,pImgNormals ? static_cast<float*>(pImgNormals->GetData()) : nullptr,
		fProgressCallback,optImgBufferDst ? static_cast<float*>(optImgBufferDst->GetData()) : nullptr
	) == false)
		return false;

	// Copy denoised data back to result buffer
	auto itSrc = pImgDenoise->begin();
	auto itDst = imgBuffer.begin();
	auto numChannels = umath::to_integral(uimg::ImageBuffer::Channel::Count) -1; // -1, because we don't want to overwrite the old alpha channel values
	for(;itSrc != pImgDenoise->end();++itSrc,++itDst)
	{
		auto &pxSrc = *itSrc;
		auto &pxDst = *itDst;
		for(auto i=decltype(numChannels){0u};i<numChannels;++i)
			pxDst.CopyValue(static_cast<uimg::ImageBuffer::Channel>(i),pxSrc);
	}
	return true;
}

bool unirender::denoise(
	const DenoiseInfo &denoise,
	float *inOutData,float *optAlbedoData,float *optInNormalData,
	const std::function<bool(float)> &fProgressCallback
)
{
	Denoiser denoiser {};
	return denoiser.Denoise(denoise,inOutData,optAlbedoData,optInNormalData,fProgressCallback);
}

bool unirender::denoise(
	const DenoiseInfo &denoiseInfo,uimg::ImageBuffer &imgBuffer,
	uimg::ImageBuffer *optImgBufferAlbedo,uimg::ImageBuffer *optImgBufferNormal,
	const std::function<bool(float)> &fProgressCallback
)
{
	Denoiser denoiser {};
	return denoiser.Denoise(denoiseInfo,imgBuffer,optImgBufferAlbedo,optImgBufferNormal,fProgressCallback);
}
#pragma optimize("",on)
