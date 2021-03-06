/*
 * (C) Copyright 2015 ETH Zurich Systems Group (http://www.systems.ethz.ch/) and others.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Contributors:
 *     Markus Pilman <mpilman@inf.ethz.ch>
 *     Simon Loesing <sloesing@inf.ethz.ch>
 *     Thomas Etter <etterth@gmail.com>
 *     Kevin Bocksrocker <kevin.bocksrocker@gmail.com>
 *     Lucas Braun <braunl@inf.ethz.ch>
 */
#include <crossbow/infinio/InfinibandBuffer.hpp>

#include "DeviceContext.hpp"

#include <crossbow/logger.hpp>

#include <sys/mman.h>

namespace crossbow {
namespace infinio {

void ScatterGatherBuffer::add(const LocalMemoryRegion& region, const void* addr, uint32_t length) {
    // Range and access checks are made when sending the buffer so we skip them here
    struct ibv_sge element;
    element.addr = reinterpret_cast<uintptr_t>(addr);
    element.length = length;
    element.lkey = region.lkey();
    mHandle.push_back(element);
    mLength += length;
}

void ScatterGatherBuffer::add(const InfinibandBuffer& buffer, size_t offset, uint32_t length) {
    if (offset + length > buffer.length()) {
        // TODO Error handling
        return;
    }

    struct ibv_sge element;
    element.addr = reinterpret_cast<uintptr_t>(buffer.data()) + offset;
    element.length = length;
    element.lkey = buffer.lkey();
    mHandle.push_back(element);
    mLength += length;
}

LocalMemoryRegion::LocalMemoryRegion(const ProtectionDomain& domain, void* data, size_t length, int access)
        : mDataRegion(ibv_reg_mr(domain.get(), data, length, access)) {
    if (mDataRegion == nullptr) {
        throw std::system_error(errno, std::generic_category());
    }
    LOG_TRACE("Created memory region at %1%", data);
}

LocalMemoryRegion::~LocalMemoryRegion() {
    if (mDataRegion != nullptr && ibv_dereg_mr(mDataRegion)) {
        std::error_code ec(errno, std::generic_category());
        LOG_ERROR("Failed to deregister memory region [error = %1% %2%]", ec, ec.message());
    }
}

LocalMemoryRegion& LocalMemoryRegion::operator=(LocalMemoryRegion&& other) {
    if (mDataRegion != nullptr && ibv_dereg_mr(mDataRegion)) {
        throw std::system_error(errno, std::generic_category());
    }

    mDataRegion = other.mDataRegion;
    other.mDataRegion = nullptr;
    return *this;
}

InfinibandBuffer LocalMemoryRegion::acquireBuffer(uint16_t id, size_t offset, uint32_t length) {
    if (offset + length > mDataRegion->length) {
        return InfinibandBuffer(InfinibandBuffer::INVALID_ID);
    }

    InfinibandBuffer buffer(id);
    buffer.handle()->addr = reinterpret_cast<uintptr_t>(mDataRegion->addr) + offset;
    buffer.handle()->length = length;
    buffer.handle()->lkey = mDataRegion->lkey;
    return buffer;
}

void LocalMemoryRegion::deregisterRegion() {
    if (mDataRegion != nullptr && ibv_dereg_mr(mDataRegion)) {
        throw std::system_error(errno, std::generic_category());
    }
    mDataRegion = nullptr;
}

AllocatedMemoryRegion::AllocatedMemoryRegion(const ProtectionDomain& domain, size_t length, int access)
        : mRegion(domain, allocateMemory(length), length, access) {
}

AllocatedMemoryRegion::~AllocatedMemoryRegion() {
    if (!mRegion.valid()) {
        return;
    }

    auto data = reinterpret_cast<void*>(mRegion.address());
    auto length = mRegion.length();

    // We have to release the memory region with the Infiniband adapter first
    try {
        mRegion.deregisterRegion();
    } catch (std::system_error& e) {
        LOG_ERROR("Failed to deregister memory region [error = %1% %2%]", e.code(), e.what());
    }

    if (munmap(data, length)) {
        std::error_code ec(errno, std::generic_category());
        LOG_ERROR("Failed to unmap memory region [error = %1% %2%]", ec, ec.message());
    }
}

AllocatedMemoryRegion& AllocatedMemoryRegion::operator=(AllocatedMemoryRegion&& other) {
    if (!mRegion.valid()) {
        mRegion = std::move(other.mRegion);
        return *this;
    }

    auto data = reinterpret_cast<void*>(mRegion.address());
    auto length = mRegion.length();

    // We have to release the memory region with the Infiniband adapter first
    mRegion = std::move(other.mRegion);

    if (munmap(data, length)) {
        throw std::system_error(errno, std::generic_category());
    }

    return *this;
}

void* AllocatedMemoryRegion::allocateMemory(size_t length) {
    // TODO Size might have to be a multiple of the page size
    auto data = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);

    if (data == MAP_FAILED) {
        throw std::system_error(errno, std::generic_category());
    }
    LOG_TRACE("Mapped %1% bytes of buffer space", length);

    return data;
}

} // namespace infinio
} // namespace crossbow
