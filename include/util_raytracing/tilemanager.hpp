/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __UTIL_RAYTRACING_TILEMANAGER_HPP__
#define __UTIL_RAYTRACING_TILEMANAGER_HPP__

#include <cinttypes>
#include <vector>
#include <mutex>
#include <array>
#include <queue>
#include <optional>
#include <sharedutils/ctpl_stl.h>
#include <mathutil/uvec.h>

namespace uimg {class ImageBuffer;};
namespace ccl {class RenderTile;};
namespace util::ocio {class ColorProcessor;};
namespace raytracing
{
	enum class ColorTransform : uint8_t;
	class TileManager
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
		void Initialize(uint32_t w,uint32_t h,uint32_t wTile,uint32_t hTile,bool cpuDevice,util::ocio::ColorProcessor *optColorProcessor=nullptr);
		void SetExposure(float exposure);
		void Reload(bool waitForCompletion);
		void Cancel();
		void Wait();
		void StopAndWait();
		std::shared_ptr<uimg::ImageBuffer> UpdateFinalImage();
		std::vector<TileData> GetRenderedTileBatch();
		Vector2i GetTileSize() const {return m_tileSize;}
		uint32_t GetTileCount() const {return m_numTiles;}
		int32_t GetCurrentTileSampleCount(uint32_t tileIndex) const;
		uint32_t GetTilesWithRenderedSamplesCount() const {return m_numTilesWithRenderedSamples;}
		bool AllTilesHaveRenderedSamples() const {return GetTilesWithRenderedSamplesCount() == GetTileCount();}
		void SetFlipImage(bool flipHorizontally,bool flipVertically);

		void UpdateRenderTile(const ccl::RenderTile &tile,bool param);
		void WriteRenderTile(const ccl::RenderTile &tile);
	private:
		void ApplyRectData(const TileData &data);
		void InitializeTileData(TileData &data);
		void ApplyPostProcessingForProgressiveTile(TileData &data);
		void SetState(State state);
		void NotifyPendingWork();

		Vector2i m_tileSize;
		uint32_t m_numTiles = 0;
		Vector2i m_numTilesPerAxis;
		std::vector<std::atomic<uint32_t>> m_renderedSampleCountPerTile;
		std::atomic<uint32_t> m_numTilesWithRenderedSamples = 0;

		std::shared_ptr<util::ocio::ColorProcessor> m_colorTransformProcessor = nullptr;

		float m_exposure = 1.f;
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
		ctpl::thread_pool m_ppThreadPool {m_ppThreadPoolHandles.size()};
		std::condition_variable m_threadWaitCondition {};
		std::mutex m_threadWaitMutex {};
		std::atomic<State> m_state = State::Initial;

		std::mutex m_completedTileMutex;
		std::vector<TileData> m_completedTiles;
		std::shared_ptr<uimg::ImageBuffer> m_progressiveImage = nullptr;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(raytracing::TileManager::TileData::Flags)

#endif
