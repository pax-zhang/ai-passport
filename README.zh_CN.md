# FoloToy AI Passport — 农场

[English](README.md) | 简体中文

**当前分支是独立的 QQ 农场风格游戏固件，不是 `main` 上的首页应用。** 开机直接进入农场，不包含通知、对讲机、天气、验证器。语言、Wi-Fi、蓝牙、时钟、屏幕、声音、玩家 ID 都在游戏里设置。设备固定连 `https://farm.netbiu.com`。联网同步、随机/好友偷菜、排行需要 Wi-Fi。配套 Next.js 服务端在 `server/`。

游戏说明：[English](docs/APPS.md) · [简体中文](docs/APPS.zh_CN.md)

FoloToy AI Passport 是一个面向 AI agent 的开放式可穿戴 AI 硬件，本仓库是这款 AI 硬件的开发基线。它不只展示“板子能运行什么”，还把 agent 开发应用所需的**硬件事实、稳定接口、资源边界、参考实现和验收方法**放在同一仓库中。

这个仓库的组织方式是：

- `main` 是最小但完整的可运行基线，也是当前硬件能力的可执行说明；
- `components/bsp` 隔离板级差异，为应用提供稳定 API；
- `demo/*` 分支展示从需求到成品的不同实现路径；
- `AGENTS.md` 约束 agent 的仓库操作，`docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md` 提供完整硬件上下文和故障知识；
- 构建结果与真机结果分开记录，禁止把“编译通过”描述成“硬件验证通过”。

理想的使用方式是：把仓库和一句应用需求交给 agent。agent 先从这里识别能力与限制，再选择相关示例、实现、构建，并给出可在真机上执行的验收清单。

## 给 AI agent 的入口

开始开发前，按以下顺序建立上下文：

1. 阅读 `AGENTS.md`、本 README 和 [`docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md`](docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md)。
2. 执行 `git status --short --branch`，保留用户已有改动。
3. 阅读需求会触及的 `components/bsp/include/*.h` 及其实现，不根据芯片或开发板的常见配置猜测本板行为。
4. 用 `git branch -r --list 'origin/demo/*'` 查找接近需求的示例，只复用相关设计，不默认合并整个示例分支。
5. 将需求拆成输入、输出、状态、并发任务、持久化、内存预算和失败降级，再决定修改 `main` 还是扩展 `components/bsp`。
6. 完成最低构建检查和适用的逻辑测试；所有依赖屏幕、按键、音频、电池或时序的结论均保留真机验收项。

### 事实来源优先级

发生冲突时，使用以下优先级：

```text
原理图 / PCB / 板卡版本 / 实机测量
    > components/bsp/include/bsp_pins.h
    > BSP 公开头文件与实现
    > docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md
    > README 与示例应用
```

当前仓库尚未包含原理图和 PCB 源文件。遇到板卡版本、接线、极性、寄存器或未使用 GPIO 等未知信息时，agent 应明确报告未知项并请求证据，不能用其他 ESP32-C3 开发板的参数补全答案。

## 硬件能力契约

下表描述的是当前 `main` 已提供的应用能力，而不是芯片数据手册中所有可能的能力。

| 能力 | 已确认实现 | 应用接口 | 必须遵守的边界 |
| --- | --- | --- | --- |
| 显示 | ST7789P3，240 × 320，竖屏 RGB565，SPI2 40 MHz；LEDC 背光 | `bsp_display_*`、`bsp_lvgl_*` | ESP32-C3 无 PSRAM；当前为小型单 DMA 缓冲；没有 LCD MISO、触摸或已知 TE 接口 |
| 输入 | `UP` / `DOWN` / `OK` 三键，共用 GPIO0 的 ADC 电阻分压 | `bsp_button_init()`、`bsp_button_read_mv()` | 回调运行在 button 组件任务中，不能阻塞；不能再创建第二个 ADC1 unit |
| 音频 | ES8311，I2S0 全双工 PCM，可播放和麦克风录音 | `bsp_audio_*` | PCM 读写为阻塞调用，应放工作任务；格式切换必须保留 BSP 内的 close/open 流程 |
| 电池 | CW2017 的 SOC 与电压读取 | `bsp_battery_*` | 是可缺省能力；读数精度取决于电芯与 profile，不能等同于已标定结果 |
| Wi-Fi | 2.4 GHz STA：扫描、加入、NVS 记忆并自动重连 | `bsp_wifi_*` | 仅 STA（SoftAP 已关闭）；扫描和连接必须放工作任务；凭据在拿到 IP 后写入 NVS；天线、射频和共存表现必须实机测量 |
| BLE / ANCS | NimBLE 外设 + Apple 通知中心客户端 | `bsp_ble_*` | 最多同时 2 路连接、6 个绑定；ANCS 仅 iPhone；需配对并打开“分享通知”；关键字在板上匹配；界面用 12px 回退字体显示中文；射频距离和与 Wi-Fi 共存必须实机测量 |
| 共享总线 | ES8311 与 CW2017 共用 I2C0 | `bsp_i2c_*` | 所有设备复用 BSP 持有的总线；不能为扫描或新设备再创建同端口总线 |
| 日志与烧录 | ESP32-C3 原生 USB Serial/JTAG | ESP-IDF console | GPIO18/19 保留给 USB；UART0 默认 TX GPIO21 与背光冲突 |

所有引脚、地址、面板参数和按键电压窗口只在 [`components/bsp/include/bsp_pins.h`](components/bsp/include/bsp_pins.h) 定义。应用代码不得复制这些常量。完整引脚表、面板初始化、ADC 阈值、I2C 地址规则、音频时钟和内存说明见 [AI 硬件开发指南](docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md)。

应用也可以使用 ESP-IDF 提供的定时器、FreeRTOS 任务和内部 Flash/NVS；番茄钟分支提供了额外的 NVS 示例。BSP 已通过 `bsp_wifi_*` 封装 2.4 GHz Wi-Fi STA，通过 `bsp_ble_*` 封装 BLE ANCS。ESP32-C3 没有经典蓝牙。`demo/claude-buddy-port` 仍可作为 BLE 应用架构参考，不能替代对当前板卡天线、射频表现、功耗和与 Wi-Fi 共存的实测。所有 FoloToy AI Passport 均配备 8 MB Flash，默认固件配置也以 8 MB 为准。

### 不属于当前能力契约的事项

仓库目前没有足够证据保证以下能力：触摸、屏幕读回、IMU、外部存储、充电控制、USB 插拔检测、可控功放使能、深度睡眠唤醒、任意“空闲 GPIO”、电池精确容量或量产级电源指标。ESP32-C3 芯片具备某项功能，不代表这块板已经接出、供电正确或经过验证。

需要这些能力时，先补充原理图、板卡修订号、器件资料或实测结果，再扩展 BSP 和验收项。

## 用一句需求开始开发

简单需求可以直接这样交给 agent：

```text
请基于 main 分支为 FoloToy AI Passport 开发一个离线习惯打卡应用。
使用三个实体按键和 240×320 屏幕，记录保存在掉电不丢失的存储中。
遵守 AGENTS.md 和 AI_HARDWARE_DEVELOPMENT_GUIDE.md；先查找相关 demo 分支，
保持硬件逻辑在 components/bsp、应用逻辑在 main，完成可运行实现与测试，
最后分别报告构建结果、未执行的真机项目和逐项验收方法。
```

需求越具体，agent 越容易一次实现正确。建议说明：

- 用户流程：每个页面显示什么，三个按键的短按、双击、长按分别做什么；
- 状态与数据：是否计时、断电保存、联网、录音或与电脑通信；
- 体验目标：字体、颜色、动画、声音、响应时间和异常状态；
- 限制条件：是否允许替换主菜单、增加依赖、使用 Flash 或改变默认交互；
- 验收标准：哪些行为必须自动测试，哪些必须在真实硬件观察。

若需求没有给出所有细节，agent 可以在不改变产品方向的范围内采用保守默认值，但应在交付中列出这些假设。涉及新接线、电源安全、硬件版本或不可恢复数据格式的决定必须先确认。

## 示例分支是设计案例，不是功能堆叠

每个 `demo/*` 分支都从基线演化出一个独立应用。它们的价值是展示具体问题的实现方式；新应用通常应从 `main` 建分支，按需参考，而不是把多个 demo 整体合并。

| 分支 | 展示的应用 | 值得复用的模式 |
| --- | --- | --- |
| `demo/stopwatch` | 秒表 | 最小计时应用、纯逻辑与 LVGL 分离、主机逻辑测试 |
| `demo/cat-themed-pomodoro-timer` | 猫咪养成番茄钟 | 单调时钟、暂停/恢复、NVS 持久化、较完整的 PRD 与状态模型 |
| `demo/rock-paper-scissors` | 石头剪刀布 | RGB565 图片资产、素材生成脚本、Flash 资源权衡 |
| `demo/tetris-game` | 三键俄罗斯方块 | 实时游戏循环、低延迟 `PRESS` 输入、局部刷新、纯游戏模型、音效与麦克风交互 |
| `demo/claude-buddy-port` | 桌面 AI 硬件伴侣 | 用完整应用替换 demo 菜单、加密 BLE、协议解析、状态归约、任务通信和较完整的主机测试 |

查看示例而不切换当前工作区：

```bash
git branch -r --list 'origin/demo/*'
git diff main...origin/demo/tetris-game -- main components tests
git show origin/demo/tetris-game:main/demo_tetris.c
```

开始新应用：

```bash
git switch main
git switch -c feature/my-passport-app
```

示例分支之间可能改变了同一菜单、配置或驱动。agent 应先理解差异，再提取状态模型、资源流水线或并发模式；不能因为代码曾出现在示例分支，就把它当成当前 `main` 的 BSP 保证。

## 应用与 BSP 的边界

```text
自然语言需求
  └─ main/                         页面、状态机、动画、业务任务、应用资源
      └─ components/bsp/include/  稳定的板级 API
          └─ components/bsp/src/  GPIO、总线、器件和驱动细节
              └─ bsp_pins.h       引脚与硬件参数的单一事实来源
```

新增普通页面时，创建 `main/demo_<feature>.c` 并实现 `enter`、`exit`、`key` 接口，然后同步修改：

- `main/demo.h` 中的声明；
- `main/CMakeLists.txt` 中的源文件；
- `main/main.c` 的 `DEMOS[]` 注册；
- 若有新的可选外设，菜单的初始化状态与失败降级。

只有多个应用都会使用的硬件能力才进入 `components/bsp`。BSP API 需要说明阻塞性、线程上下文、内存所有权、失败值和初始化顺序；引脚或 I2C 地址只能加入 `bsp_pins.h`。

### 运行时不可破坏的规则

- LVGL 不是线程安全的；非 LVGL 上下文操作 `lv_*` 对象必须持有 `bsp_lvgl_lock()`。
- 按键回调只派发轻量事件；录音、播放、存储和其他慢操作放到工作任务。
- 页面退出时先停止可能访问 UI 的任务或定时器，再删除 screen 并清空对象指针。
- 全局交互默认是菜单中 `UP`/`DOWN` 导航、`OK` 单击进入、页面中 `OK` 长按返回；改动时要明确说明。
- 新图片、字体、网络栈、音频缓存、LVGL buffer 或任务栈都要评估内部 RAM；总空闲堆足够不代表存在足够大的连续内存块。
- 可测试的状态机、协议、计时和布局计算应与 ESP-IDF/LVGL 分离，优先加入主机逻辑测试。

## 构建与运行基线

项目使用 ESP-IDF 5.5.x，已知环境为 5.5.3：

```bash
get_idf553                    # 维护者本机快捷命令
# 或 source "$HOME/esp/esp-idf-v5.5.3/export.sh"（示例安装路径）
idf.py set-target esp32c3     # 新 checkout 或曾配置其他目标时执行
idf.py build
idf.py flash monitor
```

首次构建会通过 ESP-IDF Component Manager 获取 LVGL、`esp_lvgl_port`、`button` 和 `esp_codec_dev` 等依赖。不要直接修改生成的 `managed_components/`。配置陈旧时可以使用 `idf.py fullclean` 后重新配置，但不要用它清理用户源码改动。

当前基线的纯逻辑测试可独立运行：

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math

cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ble_filter.c main/ble_filter.c \
  -o /tmp/test_ble_filter
/tmp/test_ble_filter

cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_app_i18n.c main/app_i18n.c \
  -o /tmp/test_app_i18n
/tmp/test_app_i18n

cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_app_farm.c main/app_farm_logic.c \
  -o /tmp/test_app_farm
/tmp/test_app_farm

cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_app_ota.c main/app_ota_logic.c \
  -o /tmp/test_app_ota
/tmp/test_app_ota
```

不同示例分支可能提供自己的 host test 命令，应以该分支 README 为准。

## 验收与交付格式

`idf.py build` 是最低自动检查，不是硬件验收。涉及实体外设的改动，至少在 FoloToy AI Passport 上记录：

- USB Serial/JTAG 有稳定启动日志，无重启循环、assert 或 watchdog；
- 显示方向、颜色、边缘、刷新与背光正确；
- `UP` / `DOWN` / `OK` 的目标事件和长按返回正确；
- 音频采样速度、播放、非零录音和页面退出正确；
- 电池读数合理，CW2017 缺失时应用能安全降级；
- Wi-Fi 页能扫描、加入网络，复位后用已存凭据自动重连，并且可以忘记网络；
- BLE 页能广播、与 iPhone 以及另一台主机（如 Mac 或另一台 Passport，最多 2 路同时在线）配对、接收 ANCS 通知，并显示关键字匹配的短信（或抽出的验证码）；
- 对讲机页能以 WebRTC/Wi-Fi 或蓝牙启动，按住上键说话、确定停止；同一频道的两台设备能互相听到；同一局域网的手机可打开扫码页上的 `http://<ip>:8080/`（普通 HTTP，不要用 80 端口）；麦克风页 `/w`、`/rtc` 会跳到 `https://<ip>:8443/`（需接受自签证书）；不要用微信内置浏览器，用 Safari 或 Chrome 打开；Passport 只做信令；
- 重复进出页面和并发操作后没有任务、对象或堆持续泄漏。

agent 的最终交付应明确区分：

```text
Build: PASS / FAIL / NOT RUN
Host tests: PASS / FAIL / NOT RUN
Device tests: PASS / FAIL / NOT RUN
Unverified: 仍需板卡、仪器或用户确认的事项
```

按引脚、LCD、ADC、codec、I2C、DMA 等修改类型展开的验收矩阵和故障速查表见 [AI 硬件开发指南](docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md)。

## 项目结构

```text
components/bsp/include/  BSP 公开 API 与 bsp_pins.h 硬件事实
components/bsp/src/      显示、按键、音频、电池、Wi-Fi、BLE、共享 I2C 实现
main/                    农场游戏界面、逻辑与 HTTP 同步
assets/farm/             Ardot 透明 PNG 贴纸（不要背景）
scripts/png_to_lvgl.py   把这些 PNG 转成 LVGL RGB565A8
server/                  Next.js + MySQL：注册、农场同步、偷菜、好友、排行
tests/                   可脱离硬件运行的轻量逻辑测试源
docs/                    agent 硬件指南、机上应用/设置说明与扩展文档
docs/APPS.zh_CN.md       农场分页、设置项与 NVS 键
sdkconfig.defaults       ESP32-C3、USB console、Flash、LVGL、Wi-Fi STA、NimBLE 默认配置
partitions.csv           8 MB Flash 分区，双 OTA 槽各约 3.9 MB
AGENTS.md                agent 在本仓库的编码、验证和提交规则
```
