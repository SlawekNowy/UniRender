/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#ifndef __RT_COLOR_MANAGEMENT_HPP__
#define __RT_COLOR_MANAGEMENT_HPP__

#include "definitions.hpp"
#include <cinttypes>
#include <functional>
#include <string>
#include <memory>
#include <optional>

namespace uimg {class ImageBuffer;};
namespace util::ocio {class ColorProcessor;};
namespace unirender
{
	struct DLLRTUTIL ColorTransformProcessorCreateInfo
	{
		enum class BitDepth : uint8_t
		{
			Float32 = 0,
			Float16,
			UInt8
		};
		std::string config = "filmic-blender";
		std::optional<std::string> lookName {};
		BitDepth bitDepth = BitDepth::Float32;
	};
	DLLRTUTIL std::shared_ptr<util::ocio::ColorProcessor> create_color_transform_processor(const ColorTransformProcessorCreateInfo &createInfo,std::string &outErr,float exposure=0.f,float gamma=2.2f);
	DLLRTUTIL bool apply_color_transform(uimg::ImageBuffer &imgBuf,const ColorTransformProcessorCreateInfo &createInfo,std::string &outErr,float exposure=0.f,float gamma=2.2f);
};

#endif
