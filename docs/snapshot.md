# Vulkan Snapshots

## Overview

Snapshots are used to save and load Vulkan state at a specific point in time.
Snapshots are useful for Virtual Devices that want to power off and then power
back on with the same device state.

Snapshotting is handled by tracking Vulkan API calls and their resulting objects
in a `DependencyGraph`. During snapshot saving, the nodes for API calls that
are present in the `DependencyGraph` are saved in chronological order. During
snapshot loading, the saved API calls are replayed which reconstructs the
original device state.

`DependencyGraph` has 2 types of nodes:

* `DependencyGraph::ApiNode` where each node represents a specific innovcation
of a Vulkan API function.

* `DependencyGraph::DepNode` where each node represents either:

  * A Vulkan object.

  * A marker for handling dependencies of specific commands.

## Object Creation:

```
// Time1
VkInstance instance1;
vkCreateInstace(..., &instance1);

// Time2
VkPhysicalDevice physicaldevice1;
vkEnumeratePhysicalDevices(..., &physicaldevice1);

// Time3
VkDevice device1;
vkCreateDevice(..., &device1);

...

// Time4
VkImage image1;
vkCreateImage(..., &image1);

// Time5
VkDeviceMemory memory1;
vkAllocateMemory(..., &memory1);

// Time6
vkBindImageMemory(image1, memory1);

...

// Time7
vkDestroyImage(image1);

// Time8
vkFreeMemory(memory1);
```

The `DependencyGraph` after `Time6` would look like:

```dot
digraph  {
    "instance1" [shape=circle];
    "physicaldevice1" [shape=circle];
    "device1"   [shape=circle];
    "image1"    [shape=circle];
    "memory1"   [shape=circle];
    "Helper vkBindImageMemory (Time6)" [shape=circle];

    "vkCreateInstance (Time1)" [shape=box];
    "vkEnumeratePhysicalDevices (Time2)" [shape=box];
    "vkCreateDevice (Time3)" [shape=box];
    "vkCreateImage (Time4)" [shape=box];
    "vkAllocateMemory (Time5)" [shape=box];
    "vkBindImageMemory (Time6)" [shape=box];

    "vkCreateInstance (Time1)" -> "instance1";

    "vkEnumeratePhysicalDevices (Time2)" -> "physicaldevice1";
    "instance1" -> "physicaldevice1";

    "vkCreateDevice (Time3)" -> "device1";
    "physicaldevice1" -> "device1";

    "vkCreateImage (Time4)" -> "image1";
    "device1" -> "image1";

    "vkAllocateMemory (Time5)" -> "memory1";
    "device1" -> "memory1";

    "vkBindImageMemory (Time6)"  -> "Helper vkBindImageMemory (Time6)";
    "image1" -> "Helper vkBindImageMemory (Time6)";
    "memory1" -> "Helper vkBindImageMemory (Time6)";
}
```

where API calls nodes (boxes) are linked with the objects created and dependent
objects are linked with their parents.

When a `vkDestory*()` happens, nodes and all of their descendants are removed.
The `DependencyGraph` after `Time7` would look like:

```dot
digraph  {
    "instance1" [shape=circle];
    "physicaldevice1" [shape=circle];
    "device1"   [shape=circle];
    "memory1"   [shape=circle];

    "vkCreateInstance (Time1)" [shape=box];
    "vkEnumeratePhysicalDevices (Time2)" [shape=box];
    "vkCreateDevice (Time3)" [shape=box];
    "vkCreateImage (Time4)" [shape=box];
    "vkAllocateMemory (Time5)" [shape=box];
    "vkBindImageMemory (Time6)" [shape=box];

    "vkCreateInstance (Time1)" -> "instance1";

    "vkEnumeratePhysicalDevices (Time2)" -> "physicaldevice1";
    "instance1" -> "physicaldevice1";

    "vkCreateDevice (Time3)" -> "device1";
    "physicaldevice1" -> "device1";

    "vkAllocateMemory (Time5)" -> "memory1";
    "device1" -> "memory1";

    "vkBindImageMemory (Time6)";
}
```

TODO(bohu@ + natsu@): cleanup the `"vkCreateInstance (Time1)"` and
`"vkBindImageMemory (Time6)"` nodes.


## Command Buffers:

Ignoring instances, physical devices, and devices, suppose we have

```

// Time1
VkImage image1;
vkCreateImage(..., &image1);

// Time2
VkDeviceMemory memory1;
vkAllocateMemory(..., &memory1);

// Time3
vkBindImageMemory(image1, memory1);

...

// Time4
VkCommandPool commandpool1;
vkCreateCommandPool(..., &commandpool1);

// Time5
VkCommandBuffer commandbuffer1;
vkAllocateCommandBuffers(..., &commandpool1);

...

//// Rendering for Frame 1 ////

// Time6
vkBeginCommandBuffer(commandbuffer1);

// Time7
vkCmdCopyBufferToImage(commandbuffer1, ..., image1);

// Time8
vkEndCommandBuffer(commandbuffer1);

//// Rendering for Frame 2 ////

// Time9
vkResetCommandBuffer(commandbuffer1);

// Time10
vkBeginCommandBuffer(commandbuffer1);

// Time11
vkCmdCopyBufferToImage(commandbuffer1, ..., image1);

// Time12
vkEndCommandBuffer(commandbuffer1);
```

The `DependencyGraph` after `Time8` would look like:

```dot
digraph  {
    "image1" [shape=circle];
    "memory1" [shape=circle];
    "Helper vkBindImageMemory (Time3)" [shape=circle];
    "commandpool1" [shape=circle];
    "commandbuffer1" [shape=circle];

    "vkCreateImage (Time1)" [shape=box];
    "vkAllocateMemory (Time2)" [shape=box];
    "vkBindImageMemory (Time3)" [shape=box];
    "vkCreateCommandPool (Time4)" [shape=box];
    "vkAllocateCommandBuffers (Time5)" [shape=box];
    "vkBeginCommandBuffer (Time6)" [shape=box];
    "vkCmdCopyBufferToImage (Time7)" [shape=box];
    "vkEndCommandBuffer (Time8)" [shape=box];

    "vkCreateImage (Time1)" -> "image1";

    "vkAllocateMemory (Time2)" -> "memory1";

    "vkBindImageMemory (Time3)"  -> "Helper vkBindImageMemory (Time3)";
    "image1" -> "Helper vkBindImageMemory (Time3)";
    "memory1" -> "Helper vkBindImageMemory (Time3)";

    "vkCreateCommandPool (Time4)" -> "commandpool1";

    "vkAllocateCommandBuffers (Time5)" -> "commandbuffer1";
    "commandpool1" -> "commandbuffer1";

    "vkCmdCopyBufferToImage (Time7)" -> "image1";
}
```

Then, when the `vkResetCommandBuffer()` at `Time9` happens:
old command for commandbuffer1 is cleared.



