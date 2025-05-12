set -x

# The encoders use the prefix GL while the decoders use the prefix GLES
cp -f codegen/gles1/gles1.attrib  codegen/gles1/gl.attrib
cp -f codegen/gles1/gles1.in      codegen/gles1/gl.in
cp -f codegen/gles1/gles1.types   codegen/gles1/gl.types
./build/gfxstream-generic-apigen -i ./codegen/gles1 -D ./host/gl/gles1_dec gles1
./build/gfxstream-generic-apigen -i ./codegen/gles1 -E ./guest/GLESv1_enc gl
rm codegen/gles1/gl.attrib
rm codegen/gles1/gl.in
rm codegen/gles1/gl.types

cp -f codegen/gles2/gles2.attrib  codegen/gles2/gl2.attrib
cp -f codegen/gles2/gles2.in      codegen/gles2/gl2.in
cp -f codegen/gles2/gles2.types   codegen/gles2/gl2.types
./build/gfxstream-generic-apigen -i ./codegen/gles2 -D ./host/gl/gles2_dec gles2
./build/gfxstream-generic-apigen -i ./codegen/gles2 -E ./guest/GLESv2_enc gl2
rm codegen/gles2/gl2.attrib
rm codegen/gles2/gl2.in
rm codegen/gles2/gl2.types

./build/gfxstream-generic-apigen -i ./codegen/renderControl -D ./host/renderControl_dec renderControl
./build/gfxstream-generic-apigen -i ./codegen/renderControl -E ./guest/renderControl_enc renderControl
