# Drawing graphics via `/dev/dri/`

```
strace -e ioctl -- glxdemo
ltrace -l libGL.so.1 -l libGLX.so.0 -l libGLdispatch.so.0 -l libdl.so.2 -l libXau.so.6 -l libXdmcp.so.6 -l libbsd.so.0 -l librt.so.1 -- glxdemo

LD_PRELOAD=./bin/ioctl-logger IOCTL_LOGGER=* glxdemo
```

## Configuration of client-side drivers
- https://www.mesa3d.org/envvars.html
- https://dri.freedesktop.org/wiki/ConfigurationInfrastructure/

## Proprietary drivers

Linux doesn't have neither stable kernel API nor ABI. https://www.kernel.org/doc/html/latest/process/stable-api-nonsense.html

Because of that proprietary drivers' authors ship not only binary blob with the driver itself, but also source of a small kernel module which will create ABI for binary blob. Of course, because of lack of stable API, there can be problems with that kernel module, but most of the time it's ok.

To see more, download any nvidia driver and extract it. There's `kernel` folder and a very good `README.txt`.

## Links
- `man 7 drm-memory`
- http://betteros.org/tut/graphics1.php#dumb
- https://www.x.org/releases/X11R7.5/doc/libxcb/tutorial/#pixmapscreate
- https://keithp.com/blogs/dri3_extension/
- [DRI3 protocol](https://cgit.freedesktop.org/~keithp/dri3proto)
- [GEM - the Graphics Execution Manager](https://lwn.net/Articles/283798/)
- https://01.org/linuxgraphics/gfx-docs/drm/gpu/drm-mm.html
- [Intel HD Kaby Lake documentation](https://01.org/linuxgraphics/hardware-specification-prms/2016-intelr-processors-based-kaby-lake-platform)
- [i915/GEM Crashcourse](https://blog.ffwll.ch/2013/01/i915gem-crashcourse-overview.html)
- https://habr.com/ru/post/336630/
- https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/dr13-implementation-media-sdk-atom-e3900-white-paper.pdf

- https://hg.libsdl.org/SDL
- https://hg.libsdl.org/SDL/file/25998acc4810/src/video/kmsdrm

- https://github.com/tiagovignatti/intel-gpu-tools / http://cgit.freedesktop.org/xorg/app/intel-gpu-tools/
- https://fgiesen.wordpress.com/2011/07/01/a-trip-through-the-graphics-pipeline-2011-part-1/

- ["A very ugly Wayland EGL OpenGL example"](https://gist.github.com/Miouyouyou/ca15af1c7f2696f66b0e013058f110b4)

- [kmscube - bare metal graphics](https://gitlab.freedesktop.org/mesa/kmscube/)
- [libglvnd - The GL Vendor-Neutral Dispatch library](https://github.com/NVIDIA/libglvnd)

## Wayland
```
openat(AT_FDCWD, "/dev/dri/card0", O_RDWR|O_CLOEXEC) = 4
ioctl(4, DRM_IOCTL_GET_MAGIC, 0x7fff2b275974) = 0
```