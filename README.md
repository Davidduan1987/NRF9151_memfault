# nRF9151 DK：Serial Modem、Memfault 与 OTA

基于 Nordic Semiconductor Serial Modem 的 nRF9151 DK 测试工程。PC 通过 AT 命令控制 nRF9151；设备通过蜂窝网络将 Memfault 数据发送到 nRF Cloud。本工程不包含 nRF54L 端适配代码。

## 功能与验证状态

- nRF Cloud 设备认领及 provisioning：通过 AT 命令操作。
- Memfault 日志、事件、指标、心跳和应用 coredump：通过 nRF Cloud CoAP 上传。
- Button 1 采集 modem trace：冻结 trace，恢复联网后以 Memfault CDR 上传，同时保存 `Hello world from nRF9151 Button 1` 日志。
- Modem 故障时尝试保存 trace，重新联网后上传；此故障路径尚未进行硬件验证。
- nRF Cloud FOTA：支持应用固件及 modem 差分固件更新的 AT 流程；**尚未完成实际 OTA 验证**。

已在 nRF9151 DK 验证串口 AT、NB-IoT 注册、nRF Cloud 连接，以及 Button 1 的 CDR 上传。应用 CPU 崩溃后的 modem trace 自动上传尚未实现；不要将应用 coredump 和 modem trace CDR 混为一谈。

## 硬件与版本

| 项目 | 测试配置 |
| --- | --- |
| 开发板 | nRF9151 DK，PCA10171 |
| 板级目标 | `nrf9151dk/nrf9151/ns` |
| NCS 源码 | v3.4.1 |
| Serial Modem 上游基础版本 | `948b44df0647b50ecad0946c15c9d0fb9fef3985` |
| Memfault Firmware SDK | 1.45.0 |
| 已使用的工具链 | nRF Connect SDK v3.4.0 工具链 |
| 工程应用版本 | `2.0.1+0`，由 `app/VERSION` 生成 |

上游依赖由 [west.yml](west.yml) 固定。源码保留了 Nordic Semiconductor 的版权声明，使用及再分发请遵守 [LICENSE](LICENSE) 和各文件许可证。

## 准备和编译

### 方式一：使用 VS Code 图形界面

1. 在 VS Code 安装 **nRF Connect for VS Code Extension Pack**。先准备好下文所述的 west workspace 和依赖；仅下载本仓库而没有 NCS 和 west 模块无法编译。
2. 打开 nRF Connect 扩展，在 **Applications** 视图选择 **Add an existing application**，选择本仓库的 `app` 目录，**不是**仓库根目录。当前电脑的示例路径为：

   ```text
   C:\ncs\ncs-serial-modem\modem_nrf91\serial_modem_91\NRF9151_memfault\app
   ```

3. 点击应用下的 **Add build configuration**，按以下内容配置：
   - **SDK**：选择该 west workspace 使用的 NCS v3.4.1；当前已验证的 workspace 为 `C:\ncs\ncs-serial-modem`。不要误选 `C:\ncs\v3.3.0`。
   - **Toolchain**：选择已安装的 v3.4.0 工具链；本机已验证路径为 `C:\ncs\toolchains\dcbdc366a1`。若扩展自动识别 workspace，确认其实际 SDK 路径和工具链即可。
   - **Board target**：选择 `nrf9151dk/nrf9151/ns`。
   - **Sysbuild**：选择启用；不同版本界面可能显示为 **Use sysbuild**。
   - **Build directory name**：例如 `build-nrf9151-memfault`。Windows 下建议使用较短的路径，避免路径过长。
   - 保留默认基础配置和基础 devicetree overlay；不要把下面的附加文件设置为基础文件。
4. 在 **Extra CMake arguments** 中使用 **Add Argument** 分别添加两项，直接输入以下内容，**无需加引号或 `--`**：

   ```text
   -Dapp_EXTRA_CONF_FILE=nrf-device-provisioning.conf;pc-trace.conf
   -Dapp_EXTRA_DTC_OVERLAY_FILE=trace-backend-nrf9151.overlay;pc-trace-nrf9151.overlay
   ```

   两项中的分号分别连接两个文件，`app_` 是 sysbuild 应用域前缀。不要再在 Extra Kconfig fragments 或 Extra Devicetree overlays 中重复选择同一批文件。若扩展界面提供应用域专属配置字段，也必须确认最终生成的是上述 `app_` 参数。
5. 不勾选 **Generate only**，点击 **Generate and Build**；之后可在 **Actions** 中点击 **Build** 重新编译。更换板子、SDK 或配置片段时，新建构建配置或使用 **Pristine Build** 清理后重新生成；清理前确认所选构建目录正确。
6. 编译成功后，打开所选构建目录的 `app\zephyr\.config`，核对至少包含：

   ```text
   CONFIG_NRF_PROVISIONING=y
   CONFIG_SM_PC_MODEM_TRACE_UPLOAD=y
   CONFIG_NRF_MODEM_LIB_TRACE_BACKEND_FLASH=y
   CONFIG_SM_NRF_CLOUD_FOTA=y
   ```

   OTA 镜像和符号文件的位置见下文。若扩展未识别自定义 workspace，先在 **Manage SDKs > Manage West Workspace** 中检查 workspace；切勿改用另一个版本的 SDK 后直接假定功能一致。

上面图形界面的字段和参数依据 Nordic 扩展说明及当前安装的扩展格式整理；本工程已用等效 west 命令编译通过，尚未逐项通过图形界面验证。

### 方式二：在现有 workspace 中使用命令行

先准备包含 NCS v3.4.1 和匹配 Serial Modem 上游模块的 west workspace；本工程应使用与 `west.yml` 一致的依赖，不要直接使用其他版本的 NCS workspace。以下是当前 Windows 环境的示例路径，按实际位置修改：

```powershell
cd C:\ncs\ncs-serial-modem
nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- west build --sysbuild -b nrf9151dk/nrf9151/ns -d C:\ncs\ncs-serial-modem\build-nrf9151-memfault C:\ncs\ncs-serial-modem\modem_nrf91\serial_modem_91\NRF9151_memfault\app -- '-Dapp_EXTRA_CONF_FILE=nrf-device-provisioning.conf;pc-trace.conf' '-Dapp_EXTRA_DTC_OVERLAY_FILE=trace-backend-nrf9151.overlay;pc-trace-nrf9151.overlay'
```

使用 sysbuild 时必须保留 `app_` 前缀及上述两个配置片段、两个 devicetree overlay，否则不是本说明对应的功能组合。

### 方式三：将本仓库作为 west manifest

仓库发布后，可以用其 `west.yml` 创建新的独立 workspace，并按安装环境选择工具链：

```powershell
nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- west init -m https://github.com/Davidduan1987/NRF9151_memfault --mr main C:\ncs\nrf9151-memfault-workspace
cd C:\ncs\nrf9151-memfault-workspace
nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- west update
```

然后将前面编译命令中的应用路径改为该 workspace 下 manifest 仓库的 `app` 目录、构建目录改为目标目录。新 workspace 初始化流程依赖网络和本机工具安装，尚未在全新 workspace 中验证。

编译后的主要文件位于构建目录：

- `app\zephyr\zephyr.signed.bin`：应用 OTA 上传文件。
- `app\zephyr\zephyr.elf`：上传至云端 Symbol Files 的符号文件。
- `app\zephyr\.config`：实际生成的 Kconfig，可核对功能和版本。

**签名与烧录：** 当前配置使用开发签名密钥，仅供测试，生产环境必须管理自己的签名密钥。升级镜像必须由设备当前信任的密钥签名。不要仅把 `zephyr.signed.bin` 当作完整的首次烧录镜像；首次烧录应使用 sysbuild 的全部镜像及 provisioning 数据。擦除设备前确认数据和证书备份；整片擦除会清除应用非易失数据，可能需要重新配置或 provisioning。

## 串口设置与基础 AT 命令

PC 连接 nRF9151 DK 的 **VCOM0**：115200 baud、8N1、开启 RTS/CTS 硬件流控，命令以 CR 结束。Windows 上以设备管理器显示的 COM 端口为准。VCOM1 用于日志，波特率为 1000000。

| 命令 | 作用 |
| --- | --- |
| `AT` | 测试串口通信 |
| `AT#XSMVER` | 查询 Serial Modem 和 NCS 版本 |
| `AT+CGMR` | 查询 modem 固件版本 |
| `AT+CFUN?` | 查询 modem 功能状态 |
| `AT+CFUN=1` | 启动 modem 并注册网络 |
| `AT+CFUN=4` | 飞行模式；**不等同于关闭 trace** |
| `AT+CEREG?` | 查询 LTE 注册状态；状态 `1` 或 `5` 表示已注册 |
| `AT#XLOG=1` | 打开 VCOM1 应用日志 |
| `AT#XLOG=0` | 关闭 VCOM1 应用日志 |

`AT#XNRFCLOUDOBSDEVINFO` 需要 `CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG=y`；当前构建没有启用该选项，返回 `ERROR` 是预期现象。该选项同时启用主动触发崩溃等调试命令，勿在生产固件中随意启用。

## 首次认领、provisioning 与云连接

1. 在云端创建或选择项目，将设备认领到对应项目。需要设备证明令牌时，在可信环境执行 `AT%ATTESTTOKEN`，按照云端设备认领页面操作；不要公开令牌或证书。
2. 发送 `AT+CFUN=1`，使用 `AT+CEREG?` 等待网络注册。
3. 需要 provisioning 时发送：

   ```text
   AT#XNRFPROV
   ```

4. 按设备通知操作：收到 `#XNRFPROV: 1` 后发送 `AT+CFUN=4`；收到 `#XNRFPROV: 2` 后发送 `AT+CFUN=1`；等待 `#XNRFPROV: 0`。若收到第二个 `#XNRFPROV: 0`，也应等待它完成。
5. 确认注册后连接云端：

   ```text
   AT#XNRFCLOUD=1
   ```

   等待 `#XNRFCLOUD: 1,0`。可用 `AT#XNRFCLOUD?` 查询；`AT#XNRFCLOUD=0` 断开。

### Memfault 项目路由与密钥

设备必须认领到正确项目，并能通过项目路由把数据发送到目标 Memfault 项目。如果没有配置服务器端自动路由，可在**可信串口环境**设置 Button 1 上传使用的 Project Key：

```text
AT#XNRFCLOUDOBSAUTO=0,,"<你的Project Key>"
```

此命令只保存项目路由密钥，不启用周期上传。密钥保存在设备设置中，且 `AT#XNRFCLOUDOBSAUTO?` 会读回它；不要公开串口记录、截图、配置文件或设备设置。FOTA 命令的可选 Project Key 是单独的参数，**不会自动使用这里保存的密钥**。

## Button 1 上传 modem trace 和演示日志

1. 完成认领、联网、云连接及项目路由配置。
2. 按 nRF9151 DK 的 **Button 1**。设备断开云连接、进入飞行模式、停止 trace 并重新初始化 modem 库，但不重启应用 CPU。
3. 等待：

   ```text
   #XPCMODEMTRACE: READY,<trace字节数>
   ```

   `READY,0` 表示没有可发送的 trace，不会生成 trace CDR；不要将其当作 CDR 已上传。
4. 手动恢复网络和云连接：

   ```text
   AT+CFUN=1
   AT+CEREG?
   AT#XNRFCLOUD=1
   ```

   先等待 `AT+CEREG?` 显示已注册，再连接云端。云连接就绪后，后台自动开始上传，不需要额外发送上传命令。
5. 观察 `#XPCMODEMTRACE: UPLOADING`、`#XPCMODEMTRACE: DONE,<字节数>` 或 `#XPCMODEMTRACE: ERROR,<错误码>`。`LOG_ERROR` 表示演示日志写入时发生丢弃。连接仍就绪时失败会在约 60 秒后重试；断线时先恢复 LTE 和 nRF Cloud。

`DONE` 表示设备端发送完成，不代表云端一定完成重组、接收或展示。trace 等待上传时不要启动 FOTA、DFU 或重置；固件对此类操作有保护。

### 云端如何查看 CDR 和日志

- 先在设备详情页打开 **Developer Mode / Development Mode**，便于测试时绕过服务端速率限制；生产环境按项目策略设置。正常模式的默认 CDR 限制为每设备 24 小时一次，具体以云端项目策略为准。
- 在 **CDR Payloads** 或设备 **Timeline** 中选择对应设备及时间，查找原因 `modem traces`；下载二进制文件后使用 Nordic Cellular Monitor 分析。
- 自定义文字日志在设备 **Log Files / Timeline** 中查看；它不是心跳包。
- **Chunk Decode** 显示原始分片及分片处理错误；**Processing Log** 显示云端处理记录，不是设备日志列表。两者没有错误并不单独证明 CDR 最终可用。检查筛选的项目、设备、时间、Errors 和设备 Developer Mode。
- 如果 CDR 为空，先检查 `READY` 字节数、`DONE`/`ERROR`、Chunk Decode 的 Errors 和云端限流。服务端异步处理也可能失败。设备在成功发送后会清除本次 trace；如需再次测试，须重新采集。

外部 Flash trace 为循环缓冲区，满时覆盖最旧数据；上传期间停止采集。缓冲区约 1 MB，NB-IoT 上传可能耗时较长并产生流量。状态部分保存在 RAM，断电后不保证保留；不要在待上传时断电或重启。

## Memfault 常用 AT 命令

这些命令与 Button 1 流程不同；`AT#XNRFCLOUDOBSUPLOAD` 不会触发 Button 1 的 trace 冻结。

| 命令 | 作用 |
| --- | --- |
| `AT#XNRFCLOUDOBSUPLOAD` | 上传当前缓存的 observability 数据，等待 `#XNRFCLOUDOBSUPLOAD` 结果 |
| `AT#XNRFCLOUDOBSUPLOAD="<项目Key>"` | 指定项目上传 |
| `AT#XNRFCLOUDOBSHEARTBEAT` | 生成指标心跳；之后需上传才会发送 |
| `AT#XNRFCLOUDOBSAUTO=1,600` | 启用每 600 秒的周期上传 |
| `AT#XNRFCLOUDOBSAUTO=0` | 关闭周期上传 |
| `AT#XNRFCLOUDOBSAUTO?` | 查询设置，**可能显示密钥** |
| `AT#XNRFCLOUDOBSCOREDUMP=1` | 允许上传时包含已缓存的应用 coredump |
| `AT#XNRFCLOUDOBSCOREDUMP=0` | 暂不上传已缓存的应用 coredump |
| `AT#XNRFCLOUDOBSCOREDUMP?` | 查询应用 coredump 上传设置 |
| `AT#XNRFCLOUDOBSFORWARD="<Base64分片>"` | 转发主机提供的 Memfault chunk；主机适配尚未在本工程验证 |

周期上传间隔范围为 60–86400 秒。使用命令上传前应先连接 nRF Cloud。更完整的 Serial Modem AT 命令及响应，请参考 [Nordic 官方 AT 文档](https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest/main/app/at_nrfcloud.html)。

## 应用 OTA：云端及 AT 操作

OTA 会重启设备；确保供电和网络可靠、没有待上传 trace。当前工程版本是 `2.0.1+0`；下面以设备当前 `2.0.0`、目标 `2.0.1+0` 为例：

1. 在 `app/VERSION` 设置更高版本并重新编译，**不要先将新固件烧录到待测试设备**。
2. 上传新构建的 `app\zephyr\zephyr.elf` 至 **Symbol Files**。符号文件和 OTA 镜像必须来自同一次构建。
3. 在 **OTA Releases** 创建 **Full Release**：
   - Version：填**新固件的 Memfault 版本**，本例为 `2.0.1+0`，不是设备目前的 `2.0.0`。可核对新构建 `.config` 中的 `CONFIG_MEMFAULT_NCS_FW_VERSION`。
   - Revision 和 Notes：可按需填写或留空。
   - Must Pass Through：普通测试不勾选。
4. 添加 OTA Payload：Hardware version 填 `nrf9151dk`，Software type 为 `app`，选择新构建的 `app\zephyr\zephyr.signed.bin`。硬件版本和软件类型应与设备实际配置一致；如果云端没有该硬件版本，先在 Project Settings 的 Hardware 中创建。
5. 将 release 激活到设备所属的 cohort。仅创建 release 或上传文件不会自动开始设备更新。
6. 确保已注册网络并连接云端，然后发送：

   ```text
   AT#XSMVER
   AT#XNRFCLOUDFOTA=4
   ```

   `#XNRFCLOUDFOTA: 0,0,1` 表示有更新，`0,0,0` 表示未发现更新。若需要指定应用项目，可使用 `AT#XNRFCLOUDFOTA=4,"<应用项目Key>"`。
7. 开始下载：

   ```text
   AT#XNRFCLOUDFOTA=1
   ```

   如需指定项目，使用 `AT#XNRFCLOUDFOTA=1,"<应用项目Key>"`。`OK` 仅表示命令已接受；等待进度通知和 `#XNRFCLOUDFOTA: 4,0`，确认镜像已暂存。
8. 发送 `AT#XRESET` 激活并重启，重启后用 `AT#XSMVER` 检查是否为 `v2.0.1`。不要仅凭下载进度 100% 判定已升级。

## Modem 差分 OTA

Modem 项目和应用项目通常不同。先用 `AT+CGMR` 查询当前 modem 版本，在 modem 项目创建 **Delta Release**，选择与当前版本和目标版本严格匹配的官方差分 `.bin` 文件并激活到相应 cohort。Modem Project Key 与应用 Project Key 不应混用。

```text
AT#XNRFCLOUDFOTA=6,"<modem项目Key>"
AT#XNRFCLOUDFOTA=2,"<modem项目Key>"
```

第一条仅检查，第二条检查并下载；等待 `#XNRFCLOUDFOTA: 4,0` 后发送 `AT#XMODEMRESET` 激活，再用 `AT+CGMR` 核对版本。若不传 Key，请确认服务器端默认路由指向正确项目。下载过程中可使用 `AT#XNRFCLOUDFOTA=0` 取消；只有一个 FOTA 会话可同时运行。

**Modem 差分 OTA 和应用 OTA 均尚未在本工程完成端到端验证。** 请先在测试设备验证签名、版本、网络、cohort 和回滚策略，避免在生产设备直接尝试。

## 官方资料

- [Nordic Serial Modem 与 nRF Cloud 快速入门](https://docs.nrfcloud.com/docs/mcu/quickstart-serial-modem)
- [Serial Modem nRF Cloud AT 命令](https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest/main/app/at_nrfcloud.html)
- [CDR 与设备 Timeline](https://docs.memfault.com/docs/platform/inspecting-a-device#custom-data-recordings)
- [Developer Mode 与速率限制](https://docs.nrfcloud.com/docs/platform/rate-limiting#server-side-developer-mode)
