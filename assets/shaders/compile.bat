%VULKAN_SDK%/Bin/glslc.exe --target-env=vulkan1.2 raygen.rgen -o raygen.rgen.spv
%VULKAN_SDK%/Bin/glslc.exe --target-env=vulkan1.2 closesthit.rchit -o closesthit.rchit.spv
%VULKAN_SDK%/Bin/glslc.exe --target-env=vulkan1.2 miss.rmiss -o miss.rmiss.spv
pause