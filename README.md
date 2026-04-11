This Repository is an implementation exercise of spine(cpp) for a CUHKSZ C++ class. I did it only for fun, don't see it that as a university homework. This final product(an MacOS app), is a desktop-ish programme, or whatever. I adopted it into a [online game](https://blog.nero-lithos.com/pet/).

- Runnables:

The MacOS app is located at `./Mac-dockpet/build/RosMonDockPet`, which is NOT compatible with any device running Win system. The unmodified-raw-cpp file is located at `./runros_sfml`.

You can run by `./spine/mac-dockpet/build/RosMonDockPet.app/Contents/MacOS/RosMonDockPet` or just double-clicking the app. If it failed, it's maybe because of a non-universal codesign, and you may need to sign it with `cmake --build . -- -j4` it yourself (you'll need sfml and cmake in terminal at least).

- Other Components:

`./background.png` is a picture took on 25th September 2025 at the lower campus of CUHKSZ.

Copyrights of the spine models, namely `./rosmon.atlas`, `./build_char_391_rosmon.png`, and `./rosmon.skel` belongs to the company, [Hypergryph](hypergryph.com) based in Shanghai, P.R C..

Again, **Copyright © Hypergryph. All rights reserved. For non-commercial use only.**



---



这个仓库是一个关于 spine(cpp) 的实例化练习，作为香港中文大学（深圳）C++ 课程的延伸探索。我只是出于兴趣完成的，请不要把它当作大学作业来看待。最终结果是一个类似于桌宠之类的东西。

- **可运行文件：**

MacOS 应用位于 ./Mac-dockpet/build/RosMonDockPet，**不兼容任何运行 Windows 系统的设备**。未经修饰的原始 C++ 文件位于 ./runros_sfml。

你可以通过以下方式运行：

./spine/mac-dockpet/build/RosMonDockPet.app/Contents/MacOS/RosMonDockPet

或者直接双击该应用。如果运行失败，可能是因为缺少代码签名，你可能需要自己用 cmake --build . -- -j4 进行签名（至少需要在终端中安装 sfml 和 cmake）。

- **其他组件：**

./background.png 是 2025 年 9 月 25 日在香港中文大学（深圳）下校区拍摄的一张照片。

Spine 模型的版权，包括 ./rosmon.atlas、./build_char_391_rosmon.png 和 ./rosmon.skel，均归位于中华人民共和国上海的公司 [鹰角网络（Hypergryph）](https://hypergryph.com) 所有。

再次声明：**版权所有 © Hypergryph。保留所有权利。仅限非商业用途。**