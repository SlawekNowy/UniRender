/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __RT_DENOISE_HPP__
#define __RT_DENOISE_HPP__

#include "definitions.hpp"
#include <cinttypes>
#include <functional>

namespace uimg {class ImageBuffer;};
namespace oidn {class DeviceRef;};
namespace unirender
{
	struct DLLRTUTIL DenoiseInfo
	{
		uint32_t numThreads = 16;
		uint32_t width = 0;
		uint32_t height = 0;
		bool hdr = false;
		bool lightmap = false;
	};
	class DLLRTUTIL Denoiser
	{
	public:
		Denoiser();
		bool Denoise(
			const DenoiseInfo &denoise,float *inOutData,
			float *optAlbedoData=nullptr,float *optInNormalData=nullptr,
			const std::function<bool(float)> &fProgressCallback=nullptr,
			float *optImgOutData=nullptr
		);
		bool Denoise(
			const DenoiseInfo &denoise,uimg::ImageBuffer &imgBuffer,
			uimg::ImageBuffer *optImgBufferAlbedo=nullptr,
			uimg::ImageBuffer *optImgBufferNormal=nullptr,
			const std::function<bool(float)> &fProgressCallback=nullptr,
			uimg::ImageBuffer *optImgBufferDst=nullptr
		);
	private:
		std::shared_ptr<oidn::DeviceRef> m_device = nullptr;
	};
	DLLRTUTIL bool denoise(
		const DenoiseInfo &denoise,float *inOutData,
		float *optAlbedoData=nullptr,float *optInNormalData=nullptr,
		const std::function<bool(float)> &fProgressCallback=nullptr
	);
	DLLRTUTIL bool denoise(
		const DenoiseInfo &denoise,uimg::ImageBuffer &imgBuffer,
		uimg::ImageBuffer *optImgBufferAlbedo=nullptr,
		uimg::ImageBuffer *optImgBufferNormal=nullptr,
		const std::function<bool(float)> &fProgressCallback=nullptr
	);
};

#endif
