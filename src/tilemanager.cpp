/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/tilemanager.hpp"
#include "util_raytracing/denoise.hpp"
#include "util_raytracing/color_management.hpp"
#include <util_ocio.hpp>
#include <util_image_buffer.hpp>
#include <sharedutils/util.h>

#pragma optimize("",off)
bool unirender::TileManager::TileData::IsFloatData() const {return !IsHDRData();}
bool unirender::TileManager::TileData::IsHDRData() const {return umath::is_flag_set(flags,Flags::HDRData);}

unirender::TileManager::~TileManager()
{
	StopAndWait();
}

void unirender::TileManager::StopAndWait()
{
	SetState(State::Stopped);
	Wait();
}

void unirender::TileManager::SetState(State state)
{
	m_state = state;
	m_threadWaitCondition.notify_all();
}

void unirender::TileManager::NotifyPendingWork()
{
	m_hasPendingWork = true;
	m_threadWaitCondition.notify_all();
}

void unirender::TileManager::Cancel() {SetState(State::Cancelled);}
void unirender::TileManager::Wait()
{
	for(auto &threadHandle : m_ppThreadPoolHandles)
	{
		if(threadHandle.valid())
			threadHandle.wait();
	}
}

void unirender::TileManager::SetExposure(float exposure) {m_exposure = exposure;}
void unirender::TileManager::SetGamma(float gamma) {m_gamma = gamma;}
void unirender::TileManager::SetUseFloatData(bool b) {m_useFloatData = b;}

void unirender::TileManager::Initialize(uint32_t w,uint32_t h,uint32_t wTile,uint32_t hTile,bool cpuDevice,float exposure,float gamma,util::ocio::ColorProcessor *optColorProcessor)
{
	m_cpuDevice = cpuDevice;
	if(optColorProcessor)
		m_colorTransformProcessor = optColorProcessor->shared_from_this();
	m_numTilesPerAxis = {
		(w /wTile) +((w %wTile) > 0 ? 1 : 0),
		(h /hTile) +((h %hTile) > 0 ? 1 : 0)
	};
	auto numTiles = m_numTilesPerAxis.x *m_numTilesPerAxis.y;
	m_numTiles = numTiles;
	m_inputTiles.resize(numTiles);
	m_completedTiles.resize(numTiles);
	m_progressiveImage = uimg::ImageBuffer::Create(w,h,uimg::Format::RGBA_FLOAT);
	m_tileSize = {wTile,hTile};
	m_exposure = exposure;
	m_gamma = gamma;
	Reload(false);
}

void unirender::TileManager::Reload(bool waitForCompletion)
{
	if(waitForCompletion)
		StopAndWait();
	else
		SetState(State::Cancelled);
	m_hasPendingWork = false;
	m_renderedTileMutex.lock();
		m_renderedTiles.clear();
		//for(auto &tile : m_renderedTiles)
		//	tile = {};

		m_numTilesWithRenderedSamples = 0;
		m_renderedSampleCountPerTile = std::vector<std::atomic<uint32_t>>(m_numTiles);
		for(auto &v : m_renderedSampleCountPerTile)
			v = 0;
	m_renderedTileMutex.unlock();

	m_completedTileMutex.lock();
		for(auto &tile : m_completedTiles)
			tile.sample = std::numeric_limits<uint16_t>::max();
	m_completedTileMutex.unlock();
	// Test
	/*{
		Wait();
		auto numTiles = m_inputTiles.size();

		m_inputTiles.clear();
		m_inputTiles.resize(numTiles);

		m_completedTiles.clear();
		m_completedTiles.resize(numTiles);

		auto w = m_progressiveImage->GetWidth();
		auto h = m_progressiveImage->GetHeight();
		m_progressiveImage = uimg::ImageBuffer::Create(w,h,uimg::Format::RGBA16);
		m_progressiveImage->Clear(Color::Red);
	}*/

	m_inputTileMutex.lock();
		for(auto &tile : m_inputTiles)
			tile.sample = std::numeric_limits<decltype(tile.sample)>::max();
	m_inputTileMutex.unlock();

	Wait();
	SetState(State::Running);
	for(auto i=decltype(m_ppThreadPool.size()){0u};i<m_ppThreadPool.size();++i)
	{
		m_ppThreadPoolHandles.at(i) = m_ppThreadPool.push([this](int threadId) {
			std::unique_lock<std::mutex> mlock(m_threadWaitMutex);
			for(;;)
			{
				// TODO
				// m_threadWaitCondition.wait(mlock,[this]() {return m_state != State::Running || m_hasPendingWork;});
				// TODO: ALso see sleep

				while(m_hasPendingWork)
				{
					m_inputTileMutex.lock();
						if(m_state == State::Cancelled)
						{
							m_inputTileMutex.unlock();
							goto endThread;
						}
						if(m_inputTileQueue.empty())
						{
							m_inputTileMutex.unlock();
							break;
						}
						auto tileIndex = m_inputTileQueue.front();
						m_inputTileQueue.pop();
						if(m_inputTileQueue.empty())
							m_hasPendingWork = false;
						auto tile = m_inputTiles[tileIndex];
					m_inputTileMutex.unlock();

					if(m_state == State::Cancelled)
						goto endThread;

					InitializeTileData(tile);

					if(m_state == State::Cancelled)
						goto endThread;

					m_completedTileMutex.lock();
						if(m_completedTiles[tileIndex].sample == std::numeric_limits<uint16_t>::max() || tile.sample > m_completedTiles[tileIndex].sample)
							m_completedTiles[tileIndex] = tile; // Completed tile data is float data WITHOUT color correction (color correction will be applied after denoising)
					m_completedTileMutex.unlock();

					ApplyPostProcessingForProgressiveTile(tile);
					// Progressive tile is HDR 16-bit data WITH color correction (tile will be discarded when rendering is complete and 'm_completedTiles' tile will be used instead)
					m_renderedTileMutex.lock();
						if(m_state == State::Cancelled)
						{
							m_renderedTileMutex.unlock();
							goto endThread;
						}
						if(m_renderedTiles.size() == m_renderedTiles.capacity())
							m_renderedTiles.reserve(m_renderedTiles.size() *1.5 +100);
						m_renderedTiles.push_back(std::move(tile));
						//if(m_renderedSampleCountPerTile.at(tile.index) == 0)
						//	++m_numTilesWithRenderedSamples;
						//m_renderedSampleCountPerTile.at(tile.index) = tile.sample +1;

						uint32_t curSampleCount = m_renderedSampleCountPerTile.at(tile.index);
						static uint32_t test = 3;
						if((tile.sample +1) >= test)
						{
							m_renderedSampleCountPerTile.at(tile.index) = tile.sample +1;
							if(curSampleCount == 0)
								++m_numTilesWithRenderedSamples;
						}

					m_renderedTileMutex.unlock();
				}
				if(m_hasPendingWork == false)
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if((m_state == State::Stopped && m_hasPendingWork == false) || m_state == State::Cancelled)
					goto endThread;
			}
		endThread:
			;
		});
		// Thread priority is excessively high due to Cycles, so we'll reset it to normal here
		// util::set_thread_priority(m_ppThreadPool.get_thread(i),util::ThreadPriority::Normal);
	}
}
std::shared_ptr<uimg::ImageBuffer> unirender::TileManager::UpdateFinalImage()
{
	StopAndWait();
	constexpr auto verify = false;
	auto sample = m_completedTiles.empty() ? -1 : m_completedTiles.front().sample;
	uint32_t tileIdx = 0;
	for(auto &tile : m_completedTiles)
	{
		ApplyRectData(tile);
		if constexpr(verify)
		{
			if(tileIdx < m_numTiles && tile.sample != sample)
				std::cout<<"Sample mismatch: "<<sample<<","<<tile.sample<<std::endl;
			++tileIdx;
		}
	}
	return m_progressiveImage;
}
void unirender::TileManager::ApplyRectData(const TileData &tile)
{
	if(tile.index == std::numeric_limits<decltype(tile.index)>::max())
		return;
	auto *srcData = reinterpret_cast<const uint8_t*>(tile.data.data());
	auto *dstData = static_cast<uint8_t*>(m_progressiveImage->GetData());

	constexpr auto sizePerPixel = sizeof(float) *4;
	auto srcSizePerRow = tile.w *sizePerPixel;
	auto dstSizePerRow = m_progressiveImage->GetWidth() *sizePerPixel;
	uint64_t srcOffset = 0;
	uint64_t dstOffset = (tile.y *m_progressiveImage->GetWidth() +tile.x) *sizePerPixel;
	for(auto y=tile.y;y<(tile.y +tile.h);++y)
	{
		memcpy(dstData +dstOffset,srcData +srcOffset,tile.w *sizePerPixel);
		srcOffset += srcSizePerRow;
		dstOffset += dstSizePerRow;
	}
}
std::vector<unirender::TileManager::TileData> unirender::TileManager::GetRenderedTileBatch()
{
	m_renderedTileMutex.lock();
		auto tiles = std::move(m_renderedTiles);
		m_renderedTiles.clear();
	m_renderedTileMutex.unlock();
	return tiles;
}

void unirender::TileManager::AddRenderedTile(TileData &&tile)
{
	m_renderedTileMutex.lock();
		m_numTilesWithRenderedSamples = GetTileCount();; // TODO: This is wrong and will only work if tile count is 1!
		m_renderedTiles.push_back(std::move(tile));
	m_renderedTileMutex.unlock();
}

void unirender::TileManager::SetFlipImage(bool flipHorizontally,bool flipVertically)
{
	m_flipHorizontally = flipHorizontally;
	m_flipVertically = flipVertically;
}

int32_t unirender::TileManager::GetCurrentTileSampleCount(uint32_t tileIndex) const
{
	if(tileIndex >= m_renderedSampleCountPerTile.size())
		return 0;
	return m_renderedSampleCountPerTile.at(tileIndex);
}

void unirender::TileManager::InitializeTileData(TileData &data)
{
	if(umath::is_flag_set(data.flags,TileData::Flags::Initialized))
		return;
	umath::set_flag(data.flags,TileData::Flags::Initialized);
	if(m_flipHorizontally)
		data.x = m_progressiveImage->GetWidth() -data.x -data.w;
	if(m_flipVertically)
		data.y = m_progressiveImage->GetHeight() -data.y -data.h;

	auto img = uimg::ImageBuffer::Create(data.data.data(),data.w,data.h,uimg::Format::RGBA_FLOAT);
	img->Flip(m_flipHorizontally,m_flipVertically);
	img->ClearAlpha(uimg::ImageBuffer::FULLY_OPAQUE);
}

void unirender::TileManager::ApplyPostProcessingForProgressiveTile(TileData &data)
{
	if(data.IsFloatData() == false)
		return;
	auto img = uimg::ImageBuffer::Create(data.data.data(),data.w,data.h,uimg::Format::RGBA_FLOAT);
	if(m_colorTransformProcessor)
	{
		std::string err;
		auto result = m_colorTransformProcessor->Apply(*img,err,0.f,m_gamma);
		if(result == false)
			std::cout<<"Unable to apply color transform: "<<err<<std::endl;
	}

	if(m_state == State::Cancelled)
		return;

	if(m_useFloatData)
		return;

	std::vector<uint8_t> newData;
	auto numPixels = data.w *data.h;
	auto numValues = numPixels *4;
	newData.resize(numValues *sizeof(uint16_t));

	if(m_state == State::Cancelled)
		return;

	for(auto i=decltype(numValues){0u};i<numValues;++i)
	{
		auto *data = reinterpret_cast<uint16_t*>(newData.data()) +i;
		*data = umath::float32_to_float16_glm(*(static_cast<float*>(img->GetData()) +i));
	}

#if 0 // Commented, since Cycles already appears to crop the tiles
	// Crop the tile
	auto w = img->GetWidth();
	auto h = img->GetHeight();
	int32_t xDt = static_cast<int32_t>(data.x +w) -static_cast<int32_t>(m_progressiveImage->GetWidth());
	int32_t yDt = static_cast<int32_t>(data.y +h) -static_cast<int32_t>(m_progressiveImage->GetHeight());
	if(xDt > 0 || yDt > 0)
	{
		// Tile exceeds image bounds; Crop it
		w -= xDt;
		h -= yDt;
		img->Crop(0,0,w,h,newData.data());
		newData.resize(w *h *img->GetPixelStride());
		data.w = w;
		data.h = h;
	}
#endif
	data.data = std::move(newData);
	data.flags |= TileData::Flags::HDRData;
}
#pragma optimize("",on)
