/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include <util_ocio.hpp>
#include <sharedutils/util.h>
#include <sharedutils/util_path.hpp>

module pragma.scenekit;

import :color_management;

bool pragma::scenekit::apply_color_transform(uimg::ImageBuffer &imgBuf, const ColorTransformProcessorCreateInfo &createInfo, std::string &outErr, float exposure, float gamma)
{
	auto processor = create_color_transform_processor(createInfo, outErr, exposure, gamma);
	if(!processor)
		return false;
	return processor->Apply(imgBuf, outErr);
}
std::shared_ptr<util::ocio::ColorProcessor> pragma::scenekit::create_color_transform_processor(const ColorTransformProcessorCreateInfo &createInfo, std::string &outErr, float exposure, float gamma)
{
	auto ocioConfigLocation = util::Path::CreatePath(util::get_program_path());
	ocioConfigLocation += "modules/open_color_io/configs/";
	ocioConfigLocation.Canonicalize();

	util::ocio::ColorProcessor::CreateInfo ocioCreateInfo {};
	ocioCreateInfo.configLocation = ocioConfigLocation.GetString();
	ocioCreateInfo.config = createInfo.config;
	ocioCreateInfo.lookName = createInfo.lookName;
	ocioCreateInfo.bitDepth = static_cast<util::ocio::ColorProcessor::CreateInfo::BitDepth>(createInfo.bitDepth);
	return util::ocio::ColorProcessor::Create(ocioCreateInfo, outErr, exposure, gamma);
}
