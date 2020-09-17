/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/color_management.hpp"
#include <util_ocio.hpp>
#include <sharedutils/util.h>
#include <sharedutils/util_path.hpp>

std::shared_ptr<util::ocio::ColorProcessor> raytracing::create_color_transform_processor(ColorTransform transform,std::string &outErr)
{
	auto ocioConfigLocation = util::Path::CreatePath(util::get_program_path());
	ocioConfigLocation += "modules/open_color_io/configs/";
	ocioConfigLocation.Canonicalize();
	return util::ocio::ColorProcessor::Create(static_cast<util::ocio::Config>(transform),ocioConfigLocation.GetString(),outErr);
}
