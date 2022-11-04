/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __UTIL_RAYTRACING_TILEMANAGER_HPP__
#define __UTIL_RAYTRACING_TILEMANAGER_HPP__

#include "definitions.hpp"
#include <cinttypes>
#include <vector>
#include <mutex>
#include <array>
#include <queue>
#include <optional>
#include <sharedutils/ctpl_stl.h>
#include <mathutil/uvec.h>
#include "util_raytracing.hpp"

namespace uimg {class ImageBuffer;};
namespace util::ocio {class ColorProcessor;};
namespace unirender
{
	enum class ColorTransform : uint8_t;
	class DLLRTUTIL TileManager
	{
	public:
		struct TileData
		{
			enum class Flags : uint8_t
			{
				None = 0,
				HDRData = 1,
				Initialized = HDRData<<1u
			};
			uint16_t x = 0;
			uint16_t y = 0;
			uint16_t w = 0;
			uint16_t h = 0;
			uint16_t sample = std::numeric_limits<uint16_t>::max();
			uint16_t index = std::numeric_limits<uint16_t>::max();
			Flags flags = Flags::None;
			std::vector<uint8_t> data;
			bool IsFloatData() const;
			bool IsHDRData() const;
		};
		struct ThreadData
		{

		};
		enum class State : uint8_t
		{
			Initial = 0,
			Running,
			Cancelled,
			Stopped
		};
		~TileManager();
		void Initialize(uint32_t w,uint32_t h,uint32_t wTile,uint32_t hTile,bool cpuDevice,float exposure=0.f,float gamma=DEFAULT_GAMMA,util::ocio::ColorProcessor *optColorProcessor=nullptr);
		void Reload(bool waitForCompletion);
		void Cancel();
		void Wait();
		void StopAndWait();
		std::shared_ptr<uimg::ImageBuffer> UpdateFinalImage();
		std::vector<TileData> GetRenderedTileBatch();
		void AddRenderedTile(TileData &&tile);
		Vector2i GetTileSize() const {return m_tileSize;}
		uint32_t GetTileCount() const {return m_numTiles;}
		Vector2i GetTilesPerAxisCount() const {return m_numTilesPerAxis;}
		float GetExposure() const {return m_exposure;}
		float GetGamma() const {return m_gamma;}
		bool IsCpuDevice() const {return m_cpuDevice;}
		int32_t GetCurrentTileSampleCount(uint32_t tileIndex) const;
		uint32_t GetTilesWithRenderedSamplesCount() const {return m_numTilesWithRenderedSamples;}
		bool AllTilesHaveRenderedSamples() const {return GetTilesWithRenderedSamplesCount() == GetTileCount();}
		void SetFlipImage(bool flipHorizontally,bool flipVertically);
		void SetExposure(float exposure);
		void SetGamma(float gamma);
		void SetUseFloatData(bool b);
		
		void ApplyPostProcessingForProgressiveTile(TileData &data);

		// For internal use only
		std::vector<TileData> &GetInputTiles() {return m_inputTiles;}
		std::mutex &GetInputTileMutex() {return m_inputTileMutex;}
		std::queue<size_t> &GetInputTileQueue() {return m_inputTileQueue;}
		void NotifyPendingWork();
	private:
		void ApplyRectData(const TileData &data);
		void InitializeTileData(TileData &data);
		void SetState(State state);

		Vector2i m_tileSize;
		uint32_t m_numTiles = 0;
		Vector2i m_numTilesPerAxis;
		std::vector<std::atomic<uint32_t>> m_renderedSampleCountPerTile;
		std::atomic<uint32_t> m_numTilesWithRenderedSamples = 0;
		float m_exposure = 0.f;
		float m_gamma = DEFAULT_GAMMA;

		std::shared_ptr<util::ocio::ColorProcessor> m_colorTransformProcessor = nullptr;

		bool m_useFloatData = false;
		bool m_cpuDevice = false;
		std::atomic<bool> m_hasPendingWork = false;
		std::mutex m_inputTileMutex;
		std::vector<TileData> m_inputTiles; // Tiles that have been updated by Cycles, but still require post-processing
		std::queue<size_t> m_inputTileQueue;
		bool m_flipHorizontally = false;
		bool m_flipVertically = false;

		std::mutex m_renderedTileMutex;
		std::vector<TileData> m_renderedTiles;
		std::array<std::future<void>,10> m_ppThreadPoolHandles;
		ctpl::thread_pool m_ppThreadPool {static_cast<int32_t>(m_ppThreadPoolHandles.size())};
		std::condition_variable m_threadWaitCondition {};
		std::mutex m_threadWaitMutex {};
		std::atomic<State> m_state = State::Initial;

		std::mutex m_completedTileMutex;
		std::vector<TileData> m_completedTiles;
		std::shared_ptr<uimg::ImageBuffer> m_progressiveImage = nullptr;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(unirender::TileManager::TileData::Flags)

#endif
