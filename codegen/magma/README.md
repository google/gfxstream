# Magma Emugen Interface Definition

The files in this directory were generated using
third-party/fuchsia/magma/regen.py and reflect the standard magma protocol.
However, as emugen does not have the ability to generate binding code for nested
pointers, some method signatures must be modified to use a packed format. For
clarity, these are defined alongside the original (disabled) signatures and
labeled "fudge" methods.

## Fudge Methods

The following describes how fudge methods differ from their original methods.

### `magma_device_query_fudge`

If `magma_device_query` for the given query `id` returns a value in `result_out`, then the method populates the value and other fudge parameters are ignored. Otherwise, if `host_allocate` is 1, `result_buffer_mapping_id_inout` is populated with a host-allocated buffer mapping ID, and `result_buffer_size_inout` is populated with this buffer's size. If `host_allocate` is 0, the host looks at the value passed to `result_buffer_size_inout`. If the size is too small for the query data (including value `0`), `result_buffer_mapping_id_inout` is ignored and `result_buffer_size_inout` is set to the minimum buffer size required. Otherwise, `result_buffer_size_inout` is unchanged and the host uses `result_buffer_mapping_id_inout` to map and populate the result buffer contents. Note that as certain queries may return a variable amount of data, clients should query in a loop to ensure all data is successfully retrieved.

#### Examples

##### Basic Query

```cpp
uint64_t buffer_mapping_id = 0;
uint64_t buffer_size = 0;
uint64_t result = 0;
magma_device_query_fudge(
    device,
    MAGMA_QUERY_VENDOR_ID,
    /* host_allocate = */ 0,
    &buffer_mapping_id, // Ignored and unchanged
    &buffer_size, // Ignored and unchanged
    &result); // Populated with VENDOR_ID
```

##### Host-Allocated Query

```cpp
uint64_t buffer_mapping_id = 0;
uint64_t buffer_size = 0;
uint64_t result = 0;
magma_device_query_fudge(
    device,
    MAGMA_QUERY_TOTAL_TIME,
    /* host_allocate = */ 1,
    &buffer_mapping_id, // Populated with mapping ID
    &buffer_size, // Populated with buffer size
    &result); // Ignored and unchanged
```

##### Guest-Allocated Query

Note that in the following example, the query always returns a fixed-size `magma_total_time_query_result_t`. Although the client could have allocated a buffer sufficiently large to hold this data up front, a loop is used to illustrate handling of variable-size queries, as may be encountered in vendor-specific queries.

```cpp
uint64_t buffer_mapping_id = 0;
uint64_t buffer_size = 0;
uint64_t result = 0;

uint64_t allocated_size = 0;
GuestBuffer buffer;
do {
    if (buffer_size > 0) {
        CreateBufferWithMappingId(buffer_size, &buffer, &buffer_mapping_id);
        allocated_size = buffer_size;
    }
    // On the first pass, buffer_mapping_id is ignored,
    // and buffer_size populated with the required size.
    // On subsequent passes, an allocated buffer and its
    // size are passed. If the buffer is sufficiently large,
    // the buffer contents are populated and the loop exits.
    // Otherwise, a larger buffer is allocated and the query
    // is attempted again.
    magma_device_query_fudge(
        device,
        MAGMA_QUERY_TOTAL_TIME,
        /* host_allocate = */ 0,
        &buffer_mapping_id,
        &buffer_size,
        &result); // Ignored and unchanged
} while (allocated_size < buffer_size);
```

### `magma_connection_execute_command_fudge`

This method has the same semantics as `magma_connection_execute_command`, however `descriptor` points to `descriptor_size` bytes that contain a packed `magma_command_descriptor_t` using the following tightly-packed format:

```cpp
uint32_t resource_count;
uint32_t command_buffer_count;
uint32_t wait_semaphore_count;
uint32_t signal_semaphore_count;
uint64_t flags;
magma_exec_resource_t resources[resource_count];
magma_exec_command_buffer_t command_buffers[command_buffer_count];
uint64_t semaphore_ids[signal_semaphore_count];
```

### `magma_connection_execute_immediate_commands_fudge`

This method has the same semantics as `magma_connection_execute_immediate_commands`, however `command_buffers` points to `command_buffers_size` bytes containing a list of packed `magma_inline_command_buffer_t` structs. The offset to each packed `magma_inline_command_buffer_t` is defined by the elements of `command_buffer_offsets`. Each `magma_inline_command_buffer_t` is encoded using the following tightly-packed format:

```cpp
uint64_t size;
uint32_t semaphore_count;
uint32_t padding;
uint64_t semaphore_ids[semaphore_count];
uint8_t data[size];
```

### `magma_buffer_set_name_fudge`

This method has the same semantics as `magma_buffer_set_name`, however `name_size` should be set to the number of bytes that should be used to define `name`. This should not include the terminating null character.

#### Example

```cpp
magma_buffer_set_name_fudge(buffer, "MyBuf", 5);
```
