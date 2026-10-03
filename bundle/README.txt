M1-19 line-rasterization spike -- for the first machine (RTX A2000 + Intel UHD)
================================================================================
Nothing here is for commit. It measures whether 1-pixel lines land on the same
pixels on different GPUs, with the default rule and with Bresenham lines.

On this machine (AMD Radeon RX 7900 XTX, 2026-10-03): default and Bresenham
give bit-identical frames, 889 lit pixels each.

Steps, from Git Bash in the repository, on a clean master at c988a2f:

  git apply <bundle>/m1-19-line-spike.patch
  cmake --build build/relwithdebinfo --target orbsim      (CLion's cmake)

  # 1. NVIDIA, default rule, then Bresenham
  ./build/relwithdebinfo/orbsim.exe --validate --probe lines --probe-out spike/nvidia-default
  ORBSIM_SPIKE_BRESENHAM=1 ./build/relwithdebinfo/orbsim.exe --validate --probe lines --probe-out spike/nvidia-bresenham

  # 2. Intel UHD: hide the NVIDIA driver, as decision 238 did
  #    (VK_LOADER_DRIVERS_DISABLE with the NVIDIA driver's manifest name), then the same two runs
  #    into spike/intel-default and spike/intel-bresenham

  # 3. Each run must end with exit code 0 (echo $?). Exit 1 on a Bresenham run
  #    means the GPU lacks VK_KHR_line_rasterization's bresenhamLines -- that is a result too.

  # 4. The line settings of each GPU:
  vulkaninfoSDK > spike/vulkaninfo.txt
  grep -n -E "deviceName|lineSubPixelPrecisionBits|strictLines|nonStrict|bresenhamLines|rectangularLines|smoothLines" spike/vulkaninfo.txt

  # 5. Compare everything against the AMD frames (standard-library Python only):
  python <bundle>/compare_lines.py <bundle>/amd-rx7900xtx/lines-default.png   spike/nvidia-default/lines.png
  python <bundle>/compare_lines.py <bundle>/amd-rx7900xtx/lines-bresenham.png spike/nvidia-bresenham/lines.png
  python <bundle>/compare_lines.py <bundle>/amd-rx7900xtx/lines-default.png   spike/intel-default/lines.png
  python <bundle>/compare_lines.py <bundle>/amd-rx7900xtx/lines-bresenham.png spike/intel-bresenham/lines.png

  # 6. Undo:  git checkout -- src/render   and rebuild.

Bring back: the printed output of steps 3-5, the four lines.png files, and
the four lines.txt sidecars -- their gpu.vendor and gpu.device lines name each
card's golden folder for M1-110 (decision 287).
