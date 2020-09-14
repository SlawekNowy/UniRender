/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/tilemanager.hpp"
#include "util_raytracing/denoise.hpp"
#include <util_image_buffer.hpp>
#include <render/buffers.h>

#pragma optimize("",off)
bool raytracing::TileManager::TileData::IsFloatData() const {return !IsHDRData();}
bool raytracing::TileManager::TileData::IsHDRData() const {return umath::is_flag_set(flags,Flags::HDRData);}

raytracing::TileManager::~TileManager()
{
	StopAndWait();
}

void raytracing::TileManager::StopAndWait()
{
	m_state = State::Stopped;
	Wait();
}

void raytracing::TileManager::Cancel() {m_state = State::Cancelled;}
void raytracing::TileManager::Wait()
{
	for(auto &threadHandle : m_ppThreadPoolHandles)
	{
		if(threadHandle.valid())
			threadHandle.wait();
	}
}

void raytracing::TileManager::Initialize(uint32_t w,uint32_t h,uint32_t wTile,uint32_t hTile,bool cpuDevice)
{
	m_cpuDevice = cpuDevice;
	m_numTilesPerAxis = {
		(w /wTile) +((w %wTile) > 0 ? 1 : 0),
		(h /hTile) +((h %hTile) > 0 ? 1 : 0)
	};
	auto numTiles = m_numTilesPerAxis.x *m_numTilesPerAxis.y;
	m_numTiles = numTiles;
	m_inputTiles.resize(numTiles);
	m_completedTiles.resize(numTiles);
	m_progressiveImage = uimg::ImageBuffer::Create(w,h,uimg::ImageBuffer::Format::RGBA16);
	m_tileSize = {wTile,hTile};
	Reload();
}

void raytracing::TileManager::Reload()
{
	m_state = State::Cancelled;
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
		m_progressiveImage = uimg::ImageBuffer::Create(w,h,uimg::ImageBuffer::Format::RGBA16);
		m_progressiveImage->Clear(Color::Red);
	}*/

	m_inputTileMutex.lock();
		for(auto &tile : m_inputTiles)
			tile.sample = std::numeric_limits<decltype(tile.sample)>::max();
	m_inputTileMutex.unlock();

	Wait();
	m_state = State::Running;
	for(auto i=decltype(m_ppThreadPool.size()){0u};i<m_ppThreadPool.size();++i)
	{
		m_ppThreadPoolHandles.at(i) = m_ppThreadPool.push([this](int threadId) {
			while(m_state == State::Running)
			{
				while(m_state != State::Cancelled && m_hasPendingWork && (m_state != State::Stopped || m_hasPendingWork))
				{
					m_inputTileMutex.lock();
						if(m_state == State::Cancelled)
						{
							m_inputTileMutex.unlock();
							break;
						}
						if(m_inputTileQueue.empty())
						{
							m_inputTileMutex.unlock();
							continue;
						}
						auto tileIndex = m_inputTileQueue.front();
						m_inputTileQueue.pop();
						if(m_inputTileQueue.empty())
							m_hasPendingWork = false;
						auto tile = m_inputTiles[tileIndex];
					m_inputTileMutex.unlock();

					if(m_state == State::Cancelled)
						break;

					ApplyPostProcessing(tile);

					if(m_state == State::Cancelled)
						break;

					m_completedTileMutex.lock();
						m_completedTiles[tileIndex] = tile;
					m_completedTileMutex.unlock();

					m_renderedTileMutex.lock();
						if(m_state == State::Cancelled)
						{
							m_renderedTileMutex.unlock();
							break;
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
			}
		});
	}
}
std::shared_ptr<uimg::ImageBuffer> raytracing::TileManager::UpdateFinalImage()
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
void raytracing::TileManager::ApplyRectData(const TileData &tile)
{
	if(tile.index == std::numeric_limits<decltype(tile.index)>::max())
		return;
	auto *srcData = reinterpret_cast<const uint8_t*>(tile.data.data());
	auto *dstData = static_cast<uint8_t*>(m_progressiveImage->GetData());

	constexpr auto sizePerPixel = sizeof(uint16_t) *4;
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
std::vector<raytracing::TileManager::TileData> raytracing::TileManager::GetRenderedTileBatch()
{
	m_renderedTileMutex.lock();
		auto tiles = std::move(m_renderedTiles);
		m_renderedTiles.clear();
	m_renderedTileMutex.unlock();
	return tiles;
}

int32_t raytracing::TileManager::GetCurrentTileSampleCount(uint32_t tileIndex) const
{
	if(tileIndex >= m_renderedSampleCountPerTile.size())
		return 0;
	return m_renderedSampleCountPerTile.at(tileIndex);
}

void raytracing::TileManager::ApplyPostProcessing(TileData &data)
{
	if(data.IsHDRData())
		return;
	data.x = m_progressiveImage->GetWidth() -data.x -data.w;
	data.y = m_progressiveImage->GetHeight() -data.y -data.h;

	auto img = uimg::ImageBuffer::Create(data.data.data(),data.w,data.h,uimg::ImageBuffer::Format::RGBA_FLOAT);
	img->Flip(true,true);

	if(m_state == State::Cancelled)
		return;

	std::vector<uint8_t> newData;
	newData.resize(data.w *data.h *sizeof(uint16_t) *4);

	if(m_state == State::Cancelled)
		return;

	auto imgCnv = uimg::ImageBuffer::Create(newData.data(),data.w,data.h,uimg::ImageBuffer::Format::RGBA_HDR);
	img->Copy(*imgCnv,0,0,0,0,img->GetWidth(),img->GetHeight());

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
void raytracing::TileManager::UpdateRenderTile(const ccl::RenderTile &tile,bool param)
{
	assert((tile.x %m_tileSize.x) == 0 && (tile.y %m_tileSize.y) == 0);
	if((tile.x %m_tileSize.x) != 0 || (tile.y %m_tileSize.y) != 0)
		throw std::invalid_argument{"Unexpected tile size"};
	auto tileIndex = tile.x /m_tileSize.x +(tile.y /m_tileSize.y) *m_numTilesPerAxis.x;
	TileData data {};
	data.x = tile.x;
	data.y = tile.y;
	data.index = tileIndex; // tile.tile_index; // tile_index doesn't match expected tile index in some cases?
	data.sample = tile.sample;
	data.data.resize(tile.w *tile.h *sizeof(Vector4));
	data.w = tile.w;
	data.h = tile.h;
	if(m_cpuDevice == false)
		tile.buffers->copy_from_device(); // TODO: Is this the right way to do this?
	tile.buffers->get_pass_rect("combined",1.f /* exposure */,tile.sample,4,reinterpret_cast<float*>(data.data.data()));
	// We want to minimize the overhead on this thread as much as possible (to avoid stalling Cycles), so we'll continue with post-processing on yet another thread
	m_inputTileMutex.lock();
		auto &inputTile = m_inputTiles[tileIndex];
		if(tile.sample > inputTile.sample || inputTile.sample == std::numeric_limits<decltype(inputTile.sample)>::max())
		{
			m_hasPendingWork = true;
			inputTile = std::move(data);
			m_inputTileQueue.push(tileIndex);
		}
	m_inputTileMutex.unlock();
}
void raytracing::TileManager::WriteRenderTile(const ccl::RenderTile &tile)
{

}
#pragma optimize("",on)
