#include "../wsi_presenter.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <wsi/native_wsi.h>

#include "wsi_native_surface.h"

#ifdef __APPLE__
namespace {
  typedef int (*PlatformMainCallFn)(int (*)(void*), void*);
  struct PlatformMainDispatcher { PlatformMainCallFn call; };

  struct SdlSurfaceArgs {
    SDL_Window* window;
    VkInstance instance;
    VkSurfaceKHR* surface;
  };

  int createSdlSurfaceOnMainThread(void* opaque) {
    auto* args = static_cast<SdlSurfaceArgs*>(opaque);
    return SDL_Vulkan_CreateSurface(args->window, args->instance, args->surface) ? 1 : 0;
  }
}
#endif

#ifdef __ANDROID__
// VK_KHR_android_surface by hand: the NDK's vulkan_android.h is newer than
// dxvk's Vulkan headers.
namespace {
  struct AndroidSurfaceCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    void*           window;
  };
  typedef VkResult (VKAPI_PTR *PFN_CreateAndroidSurface)(
    VkInstance, const AndroidSurfaceCreateInfo*, const VkAllocationCallbacks*, VkSurfaceKHR*);
}
#endif

namespace dxvk::wsi {

  VkResult createSurface(
          HWND                hWindow,
    const Rc<vk::InstanceFn>& vki,
          VkSurfaceKHR*       pSurface) {
#ifdef __ANDROID__
    NativeSurface ns;
    if (nativeSurface(hWindow, &ns)) {
      auto create = reinterpret_cast<PFN_CreateAndroidSurface>(
        vki->sym("vkCreateAndroidSurfaceKHR"));
      if (!create)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
      AndroidSurfaceCreateInfo info = { VkStructureType(1000008000), nullptr, 0, ns.window };
      return create(vki->instance(), &info, nullptr, pSurface);
    }
#endif
    SDL_Window* window = fromHwnd(hWindow);

#ifdef __APPLE__
    SdlSurfaceArgs args = { window, vki->instance(), pSurface };
    auto* dispatcher = static_cast<PlatformMainDispatcher*>(
      SDL_GetWindowData(window, "lb.main-thread-dispatcher"));
    return dispatcher && dispatcher->call(createSdlSurfaceOnMainThread, &args)
         ? VK_SUCCESS
         : VK_ERROR_OUT_OF_HOST_MEMORY;
#else
    return SDL_Vulkan_CreateSurface(window, vki->instance(), pSurface)
         ? VK_SUCCESS
         : VK_ERROR_OUT_OF_HOST_MEMORY;
#endif
  }

}
