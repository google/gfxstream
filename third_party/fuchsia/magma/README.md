# `regen.py`

Regenerates magma.in and magma.attrib using
[magma.json](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/src/graphics/lib/magma/include/magma/magma.json)
using some basic heuristics. This will overwrite any existing modifications to
the output files. Subsequent verification is needed to ensure the interface and
type definitions accurately match the API.

# Regenerate encoder/decoder

To regen the encoder/decoder, build ../generic-apigen and run the following commands:

```
mkdir -p enc_new
mkdir -p dec_new
vulkan-cereal/build/gfxstream-generic-apigen -i . -E enc_new/ -D dec_new/ magma
```

Note that both the `-E` and `-D` flags should be used together, as some
definition errors are only visible during one of the two codegen phases. If
codegen succeeded, the files should be moved to replace their existing
counterparts:

Encoder path: `$GOLDFISH_OPENGL/system/magma_enc`

Decoder path: `$VULKAN_CEREAL/stream-servers/magma`
