# 鸿蒙 PC 版（HarmonyOS 2in1）

AiReader 的鸿蒙版和桌面版是**同一份代码**，借助 Qt 6.12 的鸿蒙支持交叉编译成 HAP，
不是用 ArkTS 重写的。下面按顺序说明：怎么搭环境、怎么构建运行、平台差异在哪里、
还有哪些没做完。

## 环境（一次性）

需要一台 macOS（Apple Silicon）机器，装好以下几样：

- **DevEco Studio 6.1+**，自带 HarmonyOS SDK（API 23 及以上）、hvigor、hdc、node 和 JDK。
  `tools/ohos/env.sh` 默认它在 `/Applications/DevEco-Studio.app`。
- 命令行工具：`brew install sevenzip ninja cmake`。另外需要 python3，以及 Xcode
  命令行工具（提供 gperf、bison、flex）。

然后依次运行两个脚本：

```sh
tools/ohos/setup-qt.sh      # Qt 6.12.1 鸿蒙版 + macOS 主机版 → ~/Qt/6.12.1/{harmonyos,macos}
tools/ohos/build-qtpdf.sh   # 从源码编译 QtPdf 并装进 ~/Qt/6.12.1/harmonyos（约 5 GB 源码，10 核约 2 分钟）
```

为什么要自己编 QtPdf：Qt 官方的鸿蒙二进制包里**没有 QtPdf**，但 qtwebengine 6.12
分支的源码已经支持把它编到鸿蒙上。`build-qtpdf.sh` 固定了经过验证的 qtwebengine
提交，并打上 `tools/ohos/patches/` 里的 PDFium 补丁。这个补丁让 PDFium 去
`/system/fonts` 里找系统字体；不打的话，没有嵌入字体的中文 PDF 渲染出来是空白。

## 构建与运行

```sh
tools/ohos/build.sh             # 调试包 build-ohos/AiReader-<版本>-debug-unsigned.hap
tools/ohos/build.sh --release   # 发布包；设置了签名环境变量时会自动签名
tools/ohos/run.sh [--fresh]     # 安装到 hdc 连接的设备或模拟器、启动并跟踪日志
```

### 给鸿蒙 PC 用：`harmonyos-project` 分支

```sh
tools/ohos/build.sh && tools/ohos/publish-project.sh
```

`publish-project.sh` 把生成的 DevEco 工程（含编译好的 `.so`）提交并推送到单独的
`harmonyos-project` 分支。这个分支有自己的历史，和 `main` 无关，所以 `main` 里不会
混进二进制文件。鸿蒙 PC 上的用法：

```sh
git clone -b harmonyos-project --single-branch https://github.com/antonidasyang/ai-reader.git ai-reader-ohos
```

用 DevEco 打开这个目录，勾选自动签名后运行即可。以后更新只要 `git pull`。第一次签名后，
建议执行 `git update-index --skip-worktree build-profile.json5`，这样拉取更新时不会覆盖
本机的签名配置。脚本要求源码已提交，并且已经按当前版本构建过。

模拟器在 DevEco Studio 的 Device Manager 里建，类型选 **2in1**（例如 MateBook Pro）。
它可以直接安装未签名的 HAP。`run.sh --fresh` 会先卸载再装，相当于全新首启。

日志用 `hdc shell hilog -T AIReader` 看。应用自己的 `launch.log` 在沙箱里：
`/data/app/el2/100/base/com.d2ssoft.aireader/files/launch.log`。

## 签名与上架

真机和应用市场只接受签过名的 HAP。签名材料都在 AppGallery Connect（AGC）里申请：

1. 在 AGC 用企业开发者账号创建应用。包名填 `com.d2ssoft.aireader`（定义在 CMake 的
   `AIREADER_OHOS_BUNDLE_NAME`，**上架后不能再改**）。
2. 申请发布证书（.cer）和发布 Profile（.p7b）。密钥库（.p12）用 DevEco Studio 生成。
3. 构建时设置以下环境变量，再执行 `tools/ohos/build.sh --release`：

   ```sh
   export QT_HARMONYOS_SIGNING_STORE_FILE=…/release.p12
   export QT_HARMONYOS_SIGNING_KEY_ALIAS=…
   export QT_HARMONYOS_SIGNING_PLAIN_STORE_PASSWORD=…
   export QT_HARMONYOS_SIGNING_PLAIN_KEY_PASSWORD=…
   export QT_HARMONYOS_SIGNING_CERT_PATH=…/release.cer
   export QT_HARMONYOS_SIGNING_PROFILE=…/release.p7b
   ```

   签名材料和密码不能进仓库。

`versionCode` 由版本号自动换算（1.3.33 → 1003033），每次发布都会递增。

## 与桌面版的差异（都在代码里，以 `Q_OS_HARMONY` / CMake `OHOS` 区分）

| 方面 | 桌面版 | 鸿蒙版 |
|---|---|---|
| API Key / 登录令牌 | qtkeychain（macOS 钥匙串 / Windows 凭据管理器 / libsecret） | `src/platform/ohos/keychain.h`：接口与 qtkeychain 一致的替身实现，底层用系统的关键资产存储（Asset Store Kit） |
| MicroTeX 公式资源 | 放在可执行文件旁边 | 编进 qrc，首次启动时解压到沙箱 `files/microtex_res-<版本>` |
| Qt 自带的中文翻译 | 从 Qt 安装目录读取 | 只把 `qtbase_zh_CN.qm` 编进 `:/qt_translations` |
| SVG 图标插件 | 运行时在 Qt 安装目录里找到 | 显式链接 Qt6::Svg，并通过 `QT_HARMONYOS_EXTRA_PLUGINS` 打进 HAP |
| 更新 | 应用内下载安装（Windows） | 平台标识为 `harmonyos-arm64`。应用不能自行安装包，清单里对应的条目应指向应用市场页面 |
| 剪贴板读取 | 直接读 | 需要用户授予 `READ_PASTEBOARD`，Qt 插件只检查不申请。`src/platform/ohos/pasteboardaccess.*` 会在第一次粘贴时弹出系统授权框，授权后重放这次 Ctrl+V。右键文本框、打开「从链接下载」也会申请；用户拒绝过的话，再次粘贴会改走设置页授权 |
| 窗口位置 | 按上次保存的位置恢复 | 系统决定窗口落在哪里（位置请求和移动都会被忽略，只接受改尺寸）。窗口显示出来之后，如果超出屏幕，就原地缩小到能放下 |

文件访问不需要做特殊处理：「打开…」和「打开文件夹…」会调起系统的文件选择器，
用户选过的文件和文件夹，重启后依然可以访问。登录流程（浏览器回调到本机
127.0.0.1:9876）在鸿蒙上同样可用。

## 已验证（MateBook Pro 模拟器，HarmonyOS 6.1.1 / API 24）

- 启动和首启向导，中文界面，图标正常；
- 用系统选择器打开 PDF，渲染、翻页、多标签都正常；打开文件夹后能浏览目录、打开其中的论文；
- 重启后自动恢复上次打开的论文；
- 本地分段：GROBID 请求失败时会回退到本地分段；
- HTTPS（HTTP/2）请求 LLM 接口正常；API Key 存进关键资产存储、重启后能读回，配置文件里没有明文；
- 登录：能拉起系统浏览器，浏览器能访问本机回调地址。

## 待办

- **真机验收**：鼠标拖选文字、拖放 PDF 进窗口、中文输入法、公式渲染、完整登录流程
  （模拟器所在的这台 Mac 访问不了 aireader.d2ssoft.com）。
- **「用 AiReader 打开」PDF**：Qt 模板默认注册的是 text/png/jpeg，需要换成 PDF，
  应用也要处理传进来的文件 Want。
- **HAP 体积**（约 127 MB）：可以去掉用不到的 Quick Controls 样式（应用只用 Fusion），
  也可以考虑 `compressNativeLibs`。
- **权限**：`READ_PASTEBOARD` 属于受限权限（ACL），上架时要在 AGC 申请，并说明使用场景：
  用户在输入框里粘贴 API Key、论文链接等。调试阶段的自动签名已经包含它。
- **授权**：需要确认 Qt for HarmonyOS 在开源（LGPL）和商业授权下各自的条款。
- **上架资质**：企业开发者账号、软著、APP 备案、隐私政策；国内上架生成式 AI 应用，
  默认模型可能要换成已备案的国产模型。
- 构建脚本目前只在 macOS 上验证过。
