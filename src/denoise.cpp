/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#include "util_raytracing/scene.hpp"
#include "util_raytracing/denoise.hpp"
#include <util_image_buffer.hpp>
#include <OpenImageDenoise/oidn.hpp>
#include <iostream>

static std::optional<oidn::Format> get_oidn_format(uimg::Format format)
{
	switch(format) {
	case uimg::Format::R32:
	case uimg::Format::RG32:
	case uimg::Format::RGB32:
	case uimg::Format::RGBA32:
		return oidn::Format::Float3;
	case uimg::Format::R16:
	case uimg::Format::RG16:
	case uimg::Format::RGB16:
	case uimg::Format::RGBA16:
		return oidn::Format::Half3;
	}
	return {};
}

unirender::denoise::Denoiser::Denoiser()
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

bool unirender::denoise::Denoiser::Denoise(const Info &denoise, const ImageInputs &inputImages, const ImageData &outputImage, const std::function<bool(float)> &fProgressCallback)
{
	if(m_device == nullptr)
		return false;
	oidn::FilterRef filter = m_device->newFilter(denoise.lightmap ? "RTLightmap" : "RT");

	auto beautyFormat = get_oidn_format(inputImages.beautyImage.format);
	if(!beautyFormat)
		return false;
	filter.setImage("color", inputImages.beautyImage.data, *beautyFormat, denoise.width, denoise.height, 0u, uimg::ImageBuffer::GetPixelSize(inputImages.beautyImage.format));
	if(denoise.lightmap == false) {
		if(inputImages.albedoImage.data) {
			auto albedoFormat = get_oidn_format(inputImages.albedoImage.format);
			if(!albedoFormat)
				return false;
			filter.setImage("albedo", inputImages.albedoImage.data, *albedoFormat, denoise.width, denoise.height, 0u, uimg::ImageBuffer::GetPixelSize(inputImages.albedoImage.format));
		}
		if(inputImages.normalImage.data) {
			auto normalFormat = get_oidn_format(inputImages.normalImage.format);
			if(!normalFormat)
				return false;
			filter.setImage("normal", inputImages.normalImage.data, *normalFormat, denoise.width, denoise.height, 0u, uimg::ImageBuffer::GetPixelSize(inputImages.normalImage.format));
		}

		filter.set("hdr", denoise.hdr);
	}
	auto outputFormat = get_oidn_format(outputImage.format);
	if(!outputFormat)
		return false;
	filter.setImage("output", outputImage.data, *outputFormat, denoise.width, denoise.height, 0u, uimg::ImageBuffer::GetPixelSize(outputImage.format));

	std::unique_ptr<std::function<bool(float)>> ptrProgressCallback = nullptr;
	if(fProgressCallback) {
		ptrProgressCallback = std::make_unique<std::function<bool(float)>>(fProgressCallback);
		filter.setProgressMonitorFunction(
		  [](void *userPtr, double n) -> bool {
			  auto *ptrProgressCallback = static_cast<std::function<bool(float)> *>(userPtr);
			  if(ptrProgressCallback)
				  return (*ptrProgressCallback)(n);
			  return true;
		  },
		  ptrProgressCallback.get());
	}

	filter.commit();

	filter.execute();

	const char *errorMessage;
	if(m_device->getError(errorMessage) != oidn::Error::None) {
		std::cout << "Denoising failed: " << errorMessage << std::endl;
		return false;
	}

	return true;
}

bool unirender::denoise::denoise(const Info &denoise, const ImageInputs &inputImages, const ImageData &outputImage, const std::function<bool(float)> &fProgressCallback)
{
	Denoiser denoiser {};
	return denoiser.Denoise(denoise, inputImages, outputImage, fProgressCallback);
}

bool unirender::denoise::denoise(const Info &denoiseInfo, uimg::ImageBuffer &imgBuffer, uimg::ImageBuffer *optImgBufferAlbedo, uimg::ImageBuffer *optImgBufferNormal, const std::function<bool(float)> &fProgressCallback)
{
	Denoiser denoiser {};
	ImageInputs inputs {};
	inputs.beautyImage.data = static_cast<uint8_t *>(imgBuffer.GetData());
	inputs.beautyImage.format = imgBuffer.GetFormat();

	if(optImgBufferAlbedo) {
		inputs.albedoImage.data = static_cast<uint8_t *>(optImgBufferAlbedo->GetData());
		inputs.albedoImage.format = optImgBufferAlbedo->GetFormat();
	}
	if(optImgBufferNormal) {
		inputs.normalImage.data = static_cast<uint8_t *>(optImgBufferNormal->GetData());
		inputs.normalImage.format = optImgBufferNormal->GetFormat();
	}

	ImageData output {};
	output.data = static_cast<uint8_t *>(imgBuffer.GetData());
	output.format = imgBuffer.GetFormat();

	return denoiser.Denoise(denoiseInfo, inputs, output, fProgressCallback);
}
