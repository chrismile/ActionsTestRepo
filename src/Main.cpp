/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2020, Christoph Neuhauser
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <Utils/File/FileUtils.hpp>
#include <Utils/AppSettings.hpp>
#include <Utils/AppLogic.hpp>
#include <Graphics/Window.hpp>
#include <Graphics/Vulkan/Utils/Device.hpp>
#include <Graphics/Vulkan/Utils/Swapchain.hpp>

#include "MainApp.hpp"

int main(int argc, char *argv[]) {
    // Initialize the filesystem utilities.
    sgl::FileUtils::get()->initialize("Cloud Rendering", argc, argv);

#ifdef DATA_PATH
    if (!sgl::FileUtils::get()->directoryExists("Data") && !sgl::FileUtils::get()->directoryExists("../Data")) {
        sgl::AppSettings::get()->setDataDirectory(DATA_PATH);
    }
#endif
    sgl::AppSettings::get()->initializeDataDirectory();

    // Load the file containing the app settings
    std::string settingsFile = sgl::FileUtils::get()->getConfigDirectory() + "settings.txt";
    sgl::AppSettings::get()->loadSettings(settingsFile.c_str());
    sgl::AppSettings::get()->getSettings().addKeyValue("window-multisamples", 0);
    sgl::AppSettings::get()->getSettings().addKeyValue("window-vSync", true);
    sgl::AppSettings::get()->getSettings().addKeyValue("window-resizable", true);
    sgl::AppSettings::get()->getSettings().addKeyValue("window-savePosition", true);
#ifdef NDEBUG
    sgl::AppSettings::get()->getSettings().addKeyValue("window-debugContext", false);
#else
    sgl::AppSettings::get()->getSettings().addKeyValue("window-debugContext", true);
#endif
    //sgl::AppSettings::get()->setVulkanDebugPrintfEnabled();

    ImVector<ImWchar> fontRanges;
    ImFontGlyphRangesBuilder builder;
    builder.AddChar(L'\u03BB'); // lambda
    builder.BuildRanges(&fontRanges);
    sgl::AppSettings::get()->setLoadGUI(fontRanges.Data, true, false);

    sgl::AppSettings::get()->setRenderSystem(sgl::RenderSystem::VULKAN);
    sgl::Window* window = sgl::AppSettings::get()->createWindow();

    std::vector<const char*> optionalDeviceExtensions;
#ifdef SUPPORT_CUDA_INTEROP
    optionalDeviceExtensions = sgl::vk::Device::getCudaInteropDeviceExtensions();
#endif
    optionalDeviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

    sgl::vk::Instance* instance = sgl::AppSettings::get()->getVulkanInstance();
    sgl::vk::Device* device = new sgl::vk::Device;
    device->createDeviceSwapchain(
            instance, window,
            {
                    //VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                    //VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                    //VK_KHR_MAINTENANCE3_EXTENSION_NAME,
                    //VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
                    //VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                    //VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                    //VK_KHR_RAY_QUERY_EXTENSION_NAME,
                    VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME
            },
            optionalDeviceExtensions);
    sgl::vk::Swapchain* swapchain = new sgl::vk::Swapchain(device);
    swapchain->create(window);
    sgl::AppSettings::get()->setPrimaryDevice(device);
    sgl::AppSettings::get()->setSwapchain(swapchain);
    sgl::AppSettings::get()->initializeSubsystems();

    auto app = new MainApp();
    app->run();
    delete app;

    sgl::AppSettings::get()->release();

    return 0;
}
