/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <memory>
#include <shell/shared/platform/android/PlatformAndroid.h>
#include <shell/shared/platform/ios/PlatformIos.h>
#include <shell/shared/platform/linux/PlatformLinux.h>
#include <shell/shared/platform/mac/PlatformMac.h>
#include <shell/shared/platform/win/PlatformWin.h>
#include <shell/shared/renderSession/ShellParams.h>
#include <shell/shared/testShell/TestShell.h>
#include <igl/tests/util/device/TestDevice.h>

namespace igl::shell {

namespace {

std::shared_ptr<IDevice> createTestDevice() {
  const std::string backend(IGL_BACKEND_TYPE);

  if (backend == "ogl") {
#ifdef IGL_UNIT_TESTS_GLES_VERSION
    return tests::util::device::createTestDevice(::igl::BackendType::OpenGL,
                                                 {.flavor = BackendFlavor::OpenGL_ES,
                                                  .majorVersion = IGL_UNIT_TESTS_GLES_VERSION,
                                                  .minorVersion = 0});
#else
    return tests::util::device::createTestDevice(::igl::BackendType::OpenGL);
#endif
  } else if (backend == "metal") {
    return tests::util::device::createTestDevice(::igl::BackendType::Metal);
  } else if (backend == "vulkan") {
    return tests::util::device::createTestDevice(::igl::BackendType::Vulkan);
  // @fb-only
    // @fb-only
  // @fb-only
    return nullptr;
  }
}

void ensureCommandLineArgsInitialized() {
  // Fake initialization of command line args so sessions don't assert when accessing them.
  // Only do it once, otherwise it triggers an internal assert.

#if IGL_PLATFORM_ANDROID
  static bool sInitialized = true; // Android prohibits initialization of command line args
#else
  static bool sInitialized = false;
#endif
  if (!sInitialized) {
    sInitialized = true;
    igl::shell::Platform::initializeCommandLineArgs(0, nullptr);
  }
}

} // namespace

void TestShellBase::setUpInternal(ScreenSize screenSize, bool needsRGBSwapchainSupport) {
  ensureCommandLineArgsInitialized();

  // Create igl device for requested backend
  std::shared_ptr<IDevice> iglDevice = createTestDevice();
  ASSERT_TRUE(iglDevice != nullptr);
  // Create platform shell to run the tests with
#if defined(IGL_PLATFORM_MACOSX) && IGL_PLATFORM_MACOSX
  platform_ = std::make_shared<igl::shell::PlatformMac>(std::move(iglDevice));
#elif defined(IGL_PLATFORM_IOS) && IGL_PLATFORM_IOS
  platform_ = std::make_shared<PlatformIos>(std::move(iglDevice));
#elif defined(IGL_PLATFORM_WINDOWS) && IGL_PLATFORM_WINDOWS
  platform_ = std::make_shared<igl::shell::PlatformWin>(std::move(iglDevice));
#elif defined(IGL_PLATFORM_ANDROID) && IGL_PLATFORM_ANDROID
  platform_ = std::make_shared<igl::shell::PlatformAndroid>(std::move(iglDevice));
#elif defined(IGL_PLATFORM_LINUX) && IGL_PLATFORM_LINUX
  platform_ = std::make_shared<igl::shell::PlatformLinux>(std::move(iglDevice));
#endif

  IGL_DEBUG_ASSERT(platform_);

  if (platform_->getDevice().getBackendType() == igl::BackendType::OpenGL) {
    auto version = platform_->getDevice().getBackendVersion();
    if (version.majorVersion < 2) {
      GTEST_SKIP() << "OpenGL version " << (int)version.majorVersion << "."
                   << (int)version.minorVersion << " is too low";
    }
  }
  // Create an offscreen texture to render to
  Result ret;
  auto hasNativeSwapchainSupport = platform_->getDevice().hasFeature(DeviceFeatures::SRGBSwapchain);
  auto colorFormat = igl::TextureFormat::RGBA_SRGB;
  colorFormat = needsRGBSwapchainSupport && !hasNativeSwapchainSupport ? sRGBToUNorm(colorFormat)
                                                                       : colorFormat;
  TextureDesc texDesc = igl::TextureDesc::new2D(colorFormat,
                                                screenSize.width,
                                                screenSize.height,
                                                igl::TextureDesc::TextureUsageBits::Sampled |
                                                    igl::TextureDesc::TextureUsageBits::Attachment);
  offscreenTexture_ = platform_->getDevice().createTexture(texDesc, &ret);
  ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
  ASSERT_TRUE(offscreenTexture_ != nullptr);
  TextureDesc depthDextureDesc = igl::TextureDesc::new2D(
      igl::TextureFormat::Z_UNorm24,
      screenSize.width,
      screenSize.height,
      igl::TextureDesc::TextureUsageBits::Sampled | igl::TextureDesc::TextureUsageBits::Attachment);
  depthDextureDesc.storage = igl::ResourceStorage::Private;
  offscreenDepthTexture_ = platform_->getDevice().createTexture(depthDextureDesc, &ret);
  ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
  ASSERT_TRUE(offscreenDepthTexture_ != nullptr);
}

void TestShell::run(RenderSession& session, size_t numFrames) {
  ShellParams shellParams;
  session.setShellParams(shellParams);
  session.initialize();
  for (size_t i = 0; i < numFrames; ++i) {
    const igl::DeviceScope scope(platform_->getDevice());
    session.update({offscreenTexture_, offscreenDepthTexture_});
  }
  session.teardown();
}

} // namespace igl::shell
