`libuvc` is a cross-platform library for USB video devices, built atop `libusb`.
It enables fine-grained control over USB video devices exporting the standard USB Video Class
(UVC) interface, enabling developers to write drivers for previously unsupported devices,
or just access UVC devices in a generic fashion.

## Getting and Building libuvc

Prerequisites: You will need `libusb` and [CMake](http://www.cmake.org/) installed.

To build, you can just run these shell commands:

    git clone https://github.com/libuvc/libuvc
    cd libuvc
    mkdir build
    cd build
    cmake ..
    make && sudo make install

and you're set! If you want to change the build configuration, you can edit `CMakeCache.txt`
in the build directory, or use a CMake GUI to make the desired changes.

There is also `BUILD_EXAMPLE` and `BUILD_TEST` options to enable the compilation of `example` and `uvc_test` programs. To use them, replace the `cmake ..` command above with `cmake .. -DBUILD_TEST=ON -DBUILD_EXAMPLE=ON`.
Then you can start them with `./example` and `./uvc_test` respectively. Note that you need OpenCV to build the later (for displaying image).

## Developing with libuvc

The documentation for `libuvc` can currently be found at https://libuvc.github.io/.

Happy hacking!


## Debuging with libuvc

```
option(BUILD_EXAMPLE "Build example program" ON)
option(BUILD_TEST "Build test program" ON)
option(ENABLE_UVC_DEBUGGING "Enable UVC debugging" ON)
```

`Clion -> Edit Configurations -> Run with root privileges`

## macOS USB preview

The `example` program captures through libuvc/libusb and displays frames with
OpenCV. The example requests full-HD MJPEG at 30 fps, preferring 1920×1080.
If that mode is unavailable, it tries 1080×1920 and rotates the preview to
1920×1080.
MJPEG is decoded to RGB and converted to BGR for OpenCV. OpenCV's `core`, `highgui`, and `imgproc` components are
required; use `OpenCV_DIR` if OpenCV is not installed in a standard location.

```sh
cmake -S . -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_EXAMPLE=ON -DBUILD_TEST=OFF -DENABLE_UVC_DEBUGGING=OFF \
  -DOpenCV_DIR="$PWD/build/deps/opencv-build"
cmake --build build -j 4
sudo ./build/example 2e7e 0a08
```

The OpenCV directory above is the local OpenCV 4.8 build used on this machine.
For a system installation, omit `-DOpenCV_DIR` or set it to that installation.
USB vendor/product IDs are optional hexadecimal arguments. macOS requires root
privileges to detach its USB camera driver. Close other camera apps first.
Press **R** to rotate the preview by 90 degrees, and **Q** or **Esc** to stop.
The separate `tools/preview-camera.sh` uses AVFoundation, not libuvc. Run it on
macOS with the Swift compiler installed to build and open a native preview:

```sh
./tools/preview-camera.sh
```

Allow camera access when macOS prompts. This preview selects `HD CAMERA` or
another external camera and remembers the rotation set with its toolbar buttons.
