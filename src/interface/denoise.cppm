/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include "definitions.hpp"
#include <util_image_types.hpp>
#include <cinttypes>
#include <functional>
#include <memory>

export module pragma.scenekit:denoise;

export namespace pragma::scenekit::denoise {
	struct DLLRTUTIL Info {
		uint32_t numThreads = 16;
		uint32_t width = 0;
		uint32_t height = 0;
		bool lightmap = false;
		bool hdr = true;
	};

	struct DLLRTUTIL ImageData {
		uint8_t *data = nullptr;
		uimg::Format format = uimg::Format::RGB32;
	};
	struct DLLRTUTIL ImageInputs {
		ImageData beautyImage;
		ImageData albedoImage;
		ImageData normalImage;
	};

	class DLLRTUTIL Denoiser {
	  public:
		Denoiser();
		bool Denoise(const Info &denoise, const ImageInputs &inputImages, const ImageData &outputImage, const std::function<bool(float)> &fProgressCallback = nullptr);
	  private:
		std::shared_ptr<oidn::DeviceRef> m_device = nullptr;
	};
	DLLRTUTIL bool denoise(const Info &denoise, const ImageInputs &inputImages, const ImageData &outputImage, const std::function<bool(float)> &fProgressCallback = nullptr);
	DLLRTUTIL bool denoise(const Info &denoise, uimg::ImageBuffer &imgBuffer, uimg::ImageBuffer *optImgBufferAlbedo = nullptr, uimg::ImageBuffer *optImgBufferNormal = nullptr, const std::function<bool(float)> &fProgressCallback = nullptr);
};
