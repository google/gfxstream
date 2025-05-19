# Gfxstream Design Notes

## Guest Vulkan

gfxstream vulkan is the most actively developed component. Some key commponents
of the current design include:

*   1:1 threading model - each guest Vulkan encoder thread gets host side
    decoding thread
*   Support for both virtio-gpu, goldish and testing transports.
*   Support for Android, Fuchsia, and Linux guests.
*   Ring Buffer to stream commands, in the style of io_uring.
*   Mesa embedded to provide
    [dispatch](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/vulkan/dispatch.rst)
    and
    [objects](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/vulkan/base-objs.rst).
*   Currently, there are a set of Mesa objects and gfxstream objects. For
    example, `struct gfxstream_vk_device` and the gfxstream object
    `goldfish_device` both are internal representations of Vulkan opaque handle
    `VkDevice`. The Mesa object is used first, since Mesa provides dispatch. The
    Mesa object contains a key to the hash table to get a gfxstream internal
    object (for example, `gfxstream_vk_device::internal_object`). Eventually,
    gfxstream objects will be phased out and Mesa objects used exclusively.