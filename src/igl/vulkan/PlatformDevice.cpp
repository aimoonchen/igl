/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/vulkan/Common.h>
#include <igl/vulkan/Device.h>
#include <igl/vulkan/PlatformDevice.h>
#include <igl/vulkan/Texture.h>
#include <igl/vulkan/VulkanContext.h>
#include <igl/vulkan/VulkanSwapchain.h>

#if defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
#include <android/hardware_buffer.h>
#include <igl/vulkan/android/NativeHWBuffer.h>
#endif // defined(IGL_ANDROID_HWBUFFER_SUPPORTED)

namespace igl::vulkan {

PlatformDevice::PlatformDevice(Device& device) : device_(device) {}

std::shared_ptr<ITexture> PlatformDevice::createTextureFromNativeDepth(uint32_t width,
                                                                       uint32_t height,
                                                                       Result* outResult) {
  IGL_PROFILER_FUNCTION();

  const auto& ctx = device_.getVulkanContext();
  const auto& swapChain = ctx.swapchain_;

  if (!ctx.hasSwapchain()) {
    nativeDepthTexture_ = nullptr;
    Result::setResult(outResult, Result::Code::Ok);
    return nullptr;
  };

  std::shared_ptr<VulkanTexture> vkTex = swapChain->getCurrentDepthTexture();

  if (!IGL_DEBUG_VERIFY(vkTex != nullptr)) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "Swapchain has no valid texture");
    return nullptr;
  }

  IGL_DEBUG_ASSERT(vkTex->image_.imageFormat_ != VK_FORMAT_UNDEFINED, "Invalid image format");

  const auto iglFormat = vkFormatToTextureFormat(vkTex->image_.imageFormat_);
  if (!IGL_DEBUG_VERIFY(iglFormat != igl::TextureFormat::Invalid)) {
    Result::setResult(outResult, Result::Code::RuntimeError, "Invalid surface depth format");
    return nullptr;
  }

  // allocate new drawable textures if its null or mismatches in size or format
  if (!nativeDepthTexture_ || width != nativeDepthTexture_->getDimensions().width ||
      height != nativeDepthTexture_->getDimensions().height ||
      iglFormat != nativeDepthTexture_->getFormat()) {
    const TextureDesc desc = TextureDesc::new2D(iglFormat,
                                                width,
                                                height,
                                                TextureDesc::TextureUsageBits::Attachment |
                                                    TextureDesc::TextureUsageBits::Sampled,
                                                "SwapChain Texture");
    nativeDepthTexture_ = std::make_shared<Texture>(device_, std::move(vkTex), desc);
  }

  Result::setResult(outResult, Result::Code::Ok);

  return nativeDepthTexture_;
}

std::shared_ptr<ITexture> PlatformDevice::createTextureFromNativeDrawable(Result* outResult) {
  IGL_PROFILER_FUNCTION();

  const auto& ctx = device_.getVulkanContext();

  if (!ctx.hasSwapchain()) {
    nativeDrawableTextures_.clear();
    Result::setResult(outResult, Result::Code::Ok);
    return nullptr;
  };

  const auto& swapChain = ctx.swapchain_;

  auto vkTex = swapChain->getCurrentVulkanTexture();

  if (!IGL_DEBUG_VERIFY(vkTex != nullptr)) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "Swapchain has no valid texture");
    return nullptr;
  }

  IGL_DEBUG_ASSERT(vkTex->image_.imageFormat_ != VK_FORMAT_UNDEFINED, "Invalid image format");

  const igl::TextureFormat iglFormat = vkFormatToTextureFormat(vkTex->image_.imageFormat_);
  if (!IGL_DEBUG_VERIFY(iglFormat != igl::TextureFormat::Invalid)) {
    Result::setResult(outResult, Result::Code::RuntimeError, "Invalid surface color format");
    return nullptr;
  }

  const auto width = (size_t)swapChain->getWidth();
  const auto height = (size_t)swapChain->getHeight();
  const auto currentImageIndex = swapChain->getCurrentImageIndex();

  // resize nativeDrawableTextures_ pushing null pointers
  // null pointers will be allocated later as needed
  if (currentImageIndex >= nativeDrawableTextures_.size()) {
    nativeDrawableTextures_.resize((size_t)currentImageIndex + 1, nullptr);
  }

  const auto result = nativeDrawableTextures_[currentImageIndex];

  // allocate new drawable textures if its null or mismatches in size or format
  if (!result || width != result->getDimensions().width ||
      height != result->getDimensions().height || iglFormat != result->getFormat()) {
    const TextureDesc desc = TextureDesc::new2D(
        iglFormat, width, height, TextureDesc::TextureUsageBits::Attachment, "SwapChain Texture");
    nativeDrawableTextures_[currentImageIndex] =
        std::make_shared<Texture>(device_, std::move(vkTex), desc);
  }

  Result::setResult(outResult, Result::Code::Ok);

  return nativeDrawableTextures_[currentImageIndex];
}

#if defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
/// returns a android::NativeHWTextureBuffer on platforms supporting it
/// this texture allows CPU and GPU to both read/write memory
std::shared_ptr<ITexture> PlatformDevice::createTextureWithSharedMemory(const TextureDesc& desc,
                                                                        Result* outResult) const {
  Result subResult;

  auto texture =
      std::make_shared<igl::vulkan::android::NativeHWTextureBuffer>(device_, desc.format);
  subResult = texture->createHWBuffer(desc, false, false);
  Result::setResult(outResult, subResult.code, subResult.message);
  if (!subResult.isOk()) {
    return nullptr;
  }

  return std::move(texture);
}

std::shared_ptr<ITexture> PlatformDevice::createTextureWithSharedMemory(
    struct AHardwareBuffer* buffer,
    Result* outResult) const {
  Result subResult;

  AHardwareBuffer_Desc hwbDesc;
  AHardwareBuffer_describe(buffer, &hwbDesc);

  auto texture = std::make_shared<igl::vulkan::android::NativeHWTextureBuffer>(
      device_, igl::android::getIglFormat(hwbDesc.format));
  subResult = texture->createWithHWBuffer(buffer);
  Result::setResult(outResult, subResult.code, subResult.message);
  if (!subResult.isOk()) {
    return nullptr;
  }

  return std::move(texture);
}
#endif // defined(IGL_ANDROID_HWBUFFER_SUPPORTED)

VkFence PlatformDevice::getVkFenceFromSubmitHandle(SubmitHandle handle) const {
  if (handle == 0) {
    IGL_LOG_ERROR("Invalid submit handle passed to getVkFenceFromSubmitHandle");
    return VK_NULL_HANDLE;
  }
  const auto& ctx = device_.getVulkanContext();
  const auto& immediateCommands = ctx.immediate_;

  VkFence vkFence =
      immediateCommands->getVkFenceFromSubmitHandle(VulkanImmediateCommands::SubmitHandle(handle));

  return vkFence;
}

bool PlatformDevice::waitOnSubmitHandle(SubmitHandle handle, uint64_t timeoutNanoseconds) const {
  if (handle == 0) {
    IGL_LOG_ERROR("Invalid submit handle passed to waitOnSubmitHandle");
    return false;
  }

  const auto& ctx = device_.getVulkanContext();
  const auto& immediateCommands = ctx.immediate_;

  return immediateCommands->wait(VulkanImmediateCommands::SubmitHandle(handle),
                                 timeoutNanoseconds) != VK_TIMEOUT;
}

void PlatformDevice::deferredTask(std::packaged_task<void()>&& task, SubmitHandle handle) const {
  if (!handle) {
    IGL_LOG_ERROR("Invalid submit handle passed to PlatformDevice::deferredTask()");
    return;
  }

  device_.getVulkanContext().deferredTask(std::move(task),
                                          VulkanImmediateCommands::SubmitHandle(handle));
}

#if defined(IGL_PLATFORM_ANDROID) && defined(VK_KHR_external_fence_fd)
int PlatformDevice::getFenceFdFromSubmitHandle(SubmitHandle handle) const {
  if (handle == 0) {
    IGL_LOG_ERROR("Invalid submit handle passed to getFenceFDFromSubmitHandle");
    return -1;
  }

  VkFence vkFence = getVkFenceFromSubmitHandle(handle);
  IGL_DEBUG_ASSERT(vkFence != VK_NULL_HANDLE);

  VkFenceGetFdInfoKHR getFdInfo = {};
  getFdInfo.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
  getFdInfo.fence = vkFence;
  getFdInfo.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

  int fenceFd = -1;
  const auto& ctx = device_.getVulkanContext();
  VkDevice vkDevice = ctx.device_->getVkDevice();
  const VkResult result = ctx.vf_.vkGetFenceFdKHR(vkDevice, &getFdInfo, &fenceFd);
  if (result != VK_SUCCESS) {
    IGL_LOG_ERROR("Unable to get fence fd from submit handle: %lu", handle);
  }

  return fenceFd;
}
#endif // defined(IGL_PLATFORM_ANDROID)

} // namespace igl::vulkan
