// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MAGMA_CLIENT_INCLUDE_LIB_MAGMA_MAGMA_SYSMEM_H_
#define LIB_MAGMA_CLIENT_INCLUDE_LIB_MAGMA_MAGMA_SYSMEM_H_

#include <lib/magma/magma_common_defs.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// This header is C, so clang-tidy shouldn't recommend using C++ features.
// NOLINTBEGIN(modernize-use-using)

// LINT.IfChange

// An opaque handle that corresponds to a fuchsia.sysmem.BufferCollection.
typedef uint64_t magma_buffer_collection_t;

// An opaque handle that corresponds to a set of constraints on a sysmem buffer collection (similar
// to a fuchsia.sysmem.BufferCollectionConstraints). Each set of constraints has multiple "format
// slots", and sysmem can use any one of those slots to determine the image format. If no format
// slots are set, the buffer collection may only be used as an opaque buffer.
typedef uint64_t magma_sysmem_buffer_constraints_t;

// An opaque handle that corresponds to a description of the format of an allocated buffer
// collection. Various attributes of the description can be queried and used to determine the pixel
// layout of the image. This corresponds most closely to a fuchsia.sysmem.BufferCollectionInfo_2.
typedef uint64_t magma_collection_info_t;

typedef struct magma_image_plane {
  uint32_t bytes_per_row;
  uint32_t byte_offset;
} magma_image_plane_t;

// A basic set of constraints on an image format. Corresponds to
// `fuchsia.sysmem.ImageFormatConstraints`.
typedef struct magma_image_format_constraints {
  uint32_t image_format;
  magma_bool_t has_format_modifier;
  uint64_t format_modifier;
  uint32_t width;
  uint32_t height;
  uint32_t layers;
  uint32_t bytes_per_row_divisor;
  uint32_t min_bytes_per_row;
} magma_image_format_constraints_t;

// Signals what struct members are valid on `magma_buffer_format_constraints_t`.
typedef uint32_t magma_buffer_format_constraint_options_t;

#define MAGMA_BUFFER_FORMAT_CONSTRAINT_OPTIONS_EXTRA_COUNTS \
  ((magma_buffer_format_constraint_options_t)(1 << 0))

// A set of constraints on a buffer collection; corresponds to some properties of
// `fuchsia.sysmem.BufferCollectionConstraints`.
typedef struct magma_buffer_format_constraints {
  // min_buffer_count
  // Always enabled.
  struct {
    uint32_t count;
    uint32_t usage;
    magma_bool_t secure_permitted;
    magma_bool_t secure_required;
    magma_bool_t ram_domain_supported;
    magma_bool_t cpu_domain_supported;
    uint32_t min_size_bytes;
  };
  magma_buffer_format_constraint_options_t options;
  // Enabled with MAGMA_BUFFER_FORMAT_CONSTRAINT_OPTIONS_EXTRA_COUNTS set.
  struct {
    uint32_t max_buffer_count;
    uint32_t min_buffer_count_for_camping;
    uint32_t min_buffer_count_for_dedicated_slack;
    uint32_t min_buffer_count_for_shared_slack;
  };
} magma_buffer_format_constraints_t;

typedef struct magma_buffer_format_additional_constraints {
  uint32_t max_buffer_count;
  uint32_t min_buffer_count_for_camping;
  uint32_t min_buffer_count_for_dedicated_slack;
  uint32_t min_buffer_count_for_shared_slack;
} magma_buffer_format_additional_constraints_t;

///
/// \brief Import and take ownership of a sysmem connection
/// \param handle A channel connected to `fuchsia.sysmem.Allocator`.
/// \param connection_out The returned sysmem connection
///
MAGMA_EXPORT magma_status_t
magma_sysmem_connection_import(magma_handle_t handle, magma_sysmem_connection_t* connection_out);

///
/// \brief Release a connection to the sysmem service. Allocated buffers are allowed to outlive the
///        connection.
/// \param connection The connection to release.
///
MAGMA_EXPORT void magma_sysmem_connection_release(magma_sysmem_connection_t connection);

///
/// \brief Allocate a sysmem buffer with a specific size. This buffer doesn't have a sysmem token
///        and should only be used internally by the driver.
/// \param connection The connection to allocate it on.
/// \param flags A set of `MAGMA_SYSMEM_FLAG_` flags to use when allocating the buffer.
/// \param size The size of the buffer in bytes.
/// \param buffer_handle_out The returned buffer handle
///
MAGMA_EXPORT magma_status_t
magma_sysmem_connection_allocate_buffer(magma_sysmem_connection_t connection, uint32_t flags,
                                        uint64_t size, magma_handle_t* buffer_handle_out);

///
/// \brief Release a magma_collection_info_t object.
/// \param description The object to release
///
MAGMA_EXPORT void magma_collection_info_release(magma_collection_info_t collection_info);

///
/// \brief Get information about the memory layout of all image planes of an image, given a set of
///        image dimensions.
/// \param description The description to retrieve information about.
/// \param width width of the image to retrieve layout information about
/// \param height height of the image to retrieve layout information about
/// \param image_planes_out An array with MAGMA_MAX_IMAGE_PLANES elements that will receive
///        information on all planes.
///
MAGMA_EXPORT magma_status_t magma_collection_info_get_plane_info_with_size(
    magma_collection_info_t collection_info, uint32_t width, uint32_t height,
    magma_image_plane_t* image_planes_out);

///
/// \brief  Get the MAGMA_FORMAT_* value for a buffer description.
/// \param description The description to retrieve information about.
/// \param format_out Receives a `MAGMA_FORMAT_*` value describing the image. May receive
///        MAGMA_FORMAT_INVALID if the buffer isn't an image.
///
MAGMA_EXPORT magma_status_t
magma_collection_info_get_format(magma_collection_info_t collection_info, uint32_t* format_out);

///
/// \brief Get a sysmem buffer format modifier for an image description.
/// \param description The description to retrieve information about.
/// \param has_format_modifier_out Receives whether the description has a format modifier.
/// \param format_modifier_out Receives the sysmem format modifier value.
///
MAGMA_EXPORT magma_status_t magma_collection_info_get_format_modifier(
    magma_collection_info_t collection_info, magma_bool_t* has_format_modifier_out,
    uint64_t* format_modifier_out);

///
/// \brief Get the first allowable sysmem color space for a buffer.
/// \param description The description to retrieve information about.
/// \param color_space_out Receives the color space
///
MAGMA_EXPORT magma_status_t magma_collection_info_get_color_space(
    magma_collection_info_t collection_info, uint32_t* color_space_out);

///
/// \brief Gets the buffer coherency domain for a description.
/// \param description The description to retrieve information about.
/// \param coherency_domain_out receives a `MAGMA_COHERENCY_DOMAIN_*` value.
///
MAGMA_EXPORT magma_status_t magma_collection_info_get_coherency_domain(
    magma_collection_info_t collection_info, uint32_t* coherency_domain_out);

///
/// \brief Get the number of buffers allocated in a buffer collection.
/// \param description The description to retrieve information about.
/// \param count_out receives the buffer count. This corresponds to
///        `fuchsia.sysmem.BufferCollectionInfo_2.buffer_count`.
///
MAGMA_EXPORT magma_status_t magma_collection_info_get_buffer_count(
    magma_collection_info_t collection_info, uint32_t* count_out);

///
/// \brief Gets the value of fuchsia.sysmem.BufferMemorySettings.is_secure is for the buffers in the
///        collection.
/// \param description The description to retrieve information about.
/// \param is_secure_out Receives the value.
///
MAGMA_EXPORT magma_status_t magma_collection_info_get_is_secure(
    magma_collection_info_t collection_info, magma_bool_t* is_secure_out);

///
/// \brief Import a magma buffer collection.
/// \param connection The connection to import into.
/// \param handle a `fuchsia.symsmem.BufferCollectionToken` handle to import from. If
///        ZX_HANDLE_INVALID (0), then a new buffer collection is created.
/// \param collection_out Receives the collection
///
MAGMA_EXPORT magma_status_t magma_sysmem_connection_import_buffer_collection(
    magma_sysmem_connection_t connection, magma_handle_t handle,
    magma_buffer_collection_t* collection_out);

///
/// \brief Release a magma buffer collection.
/// \param collection The Collection to release.
///
MAGMA_EXPORT void magma_buffer_collection_release2(magma_buffer_collection_t collection);

///
/// \brief Create a set of buffer constraints. These constraints can be modified with additional
///        requirements, then set on a buffer collection. The buffer constraints must be freed using
///        `magma_buffer_constraints_release`.
/// \param connection The connection to create constraints on.
/// \param buffer_constraints A struct describing overall constraints of every image format.
/// \param constraints_out receives the allocated buffer constraints.
///
MAGMA_EXPORT magma_status_t magma_sysmem_connection_create_buffer_constraints(
    magma_sysmem_connection_t connection,
    const magma_buffer_format_constraints_t* buffer_constraints,
    magma_sysmem_buffer_constraints_t* constraints_out);

///
/// \brief Set information about a format slot on the constraints. The sysmem driver will choose one
///        format slot to use when creating the image.  May not be called after
///        `magma_buffer_collection_set_constraints` using these constraints.
/// \param connection The connection for the constraints.
/// \param constraints Constraints to modify.
/// \param index The format slot index to set. A format slot index may only be set once.
/// \param format_constraints constraints on the image format.
///
MAGMA_EXPORT magma_status_t
magma_buffer_constraints_set_format2(magma_sysmem_buffer_constraints_t constraints, uint32_t index,
                                     const magma_image_format_constraints_t* format_constraints);

///
/// \brief Sets the list of allowable color spaces for an image format slot.
///        May not be called after `magma_buffer_collection_set_constraints` using these
///        constraints.
/// \param constraints Constraints to modify.
/// \param index the format slot index to set colorspace constraints on.
///        `magma_buffer_constraints_set_format` must have been set on this index.
/// \param color_space_count Number of elements in the color_spaces array.
/// \param color_spaces array of color spaces. Must be elements of `fuchsia.sysmem.ColorSpaceType`
///
MAGMA_EXPORT magma_status_t magma_buffer_constraints_set_colorspaces2(
    magma_sysmem_buffer_constraints_t constraints, uint32_t index, uint32_t color_space_count,
    const uint32_t* color_spaces);

///
/// \brief Release a `magma_sysmem_buffer_constraints_t`.
/// \param constraints The constraints to release.
MAGMA_EXPORT void magma_buffer_constraints_release2(magma_sysmem_buffer_constraints_t constraints);

///
/// \brief Set format constraints for a buffer collection. This call may trigger sysmem to allocate
///        the collection if all constraints are set.
/// \param collection The collection to use
/// \param constraints Constraints to use
///
MAGMA_EXPORT magma_status_t magma_buffer_collection_set_constraints2(
    magma_buffer_collection_t collection, magma_sysmem_buffer_constraints_t constraints);

///
/// \brief Creates a buffer format description to describe a collection of allocated buffers. This
///        call will wait until the initial buffers in the collection are allocated.
/// \param collection The collection
/// \param collection_info_out receives the buffer format description.  On success must
///        later be released using magma_buffer_format_description_release.
///
MAGMA_EXPORT magma_status_t magma_buffer_collection_get_collection_info(
    magma_buffer_collection_t collection, magma_collection_info_t* collection_info_out);

///
/// \brief Retrieves a handle to a VMO to retrieve from a buffer collection. This call will wait
///        until the initial buffers in the collection are allocated.
/// \param collection A collection to retrieve the handle from
/// \param index the index of the handle to retrieve. Must be < the value of
///        `magma_get_buffer_count`
/// \param buffer_handle_out receives a VMO handle for the collection. May be used with
///        `magma_import`. On success, the caller takes ownership.
/// \param vmo_offset Receives the byte offset into the VMO where the buffer starts.
///
MAGMA_EXPORT magma_status_t magma_buffer_collection_get_buffer_handle(
    magma_buffer_collection_t collection, uint32_t index, magma_handle_t* buffer_handle_out,
    uint32_t* vmo_offset_out);

///
/// \brief Determines which constraint format slot indices match the buffer description.
/// \param description The description to retrieve information about.
/// \param constraints The constraints originally used to allocate the collection that `description`
///        was retrieved from.
/// \param format_valid_out An array of elements; `format_valid_out[i]` is set to true if the format
///        slot index i in the constraints match the format description.  Multiple format slots may
///        map to a single format in sysmem, which is why this call may set multiple formats as
///        valid.
/// \param format_valid_count The number of entires in `format_valid_out`. Must be > the maximum
///        format slot index used when creating the constraints.
///
MAGMA_EXPORT magma_status_t magma_collection_info_get_format_index(
    magma_collection_info_t collection_info, magma_sysmem_buffer_constraints_t constraints,
    magma_bool_t* format_valid_out, uint32_t format_valid_count);

// LINT.ThenChange(magma_common_defs.h:version)

// NOLINTEND(modernize-use-using)

#if defined(__cplusplus)
}
#endif

#endif  // LIB_MAGMA_CLIENT_INCLUDE_LIB_MAGMA_MAGMA_SYSMEM_H_
