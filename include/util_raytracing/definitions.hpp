/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
*/

#ifndef __UTIL_RAYTRACING_DEFINITIONS_HPP__
#define __UTIL_RAYTRACING_DEFINITIONS_HPP__

#ifdef RTUTIL_STATIC
#define DLLRTUTIL
#elif RTUTIL_DLL
#ifdef __linux__
#define DLLRTUTIL __attribute__((visibility("default")))
#else
#define DLLRTUTIL __declspec(dllexport)
#endif
#else
#ifdef __linux__
#define DLLRTUTIL
#else
#define DLLRTUTIL __declspec(dllimport)
#endif
#endif

#endif
