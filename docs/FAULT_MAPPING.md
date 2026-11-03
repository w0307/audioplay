# State 故障映射与录音（3.6）

查表顺序：StateParser 解析 `errors` → 故障类别/类别内顺序排序 → VoicePolicy 与其他场景比较 → VoiceCatalog 查 JSON → 独立引擎播放文件，源文件不可用才 TTS。

优先级：急停1 > 故障2 > 充电3 > 障碍4 > 低电5 > 模式6 > 运动7。故障内部：触碰 > 限位/料车连接 > 设备 > 参数 > 导航通信 > 上装通信 > IPC > 其他；IPC继续按宿主枚举顺序。录音编号与时长均不影响优先级。

## 急停与常规/IPC错误

急停来自 `safetyState.eStop` 有效急停值或 `COMMON_ERROR bit0`，资源键 `emergency_stop`，使用 `043.mp3`。其他映射如下；表中的 TTS 文案也是文件失败后的备用文案。

| State errors 输入 | 类别 / 内部顺序 | JSON资源键 | MP3 | TTS文案 |
| --- | --- | --- | --- | --- |
| COMMON_ERROR bit1 | Touch / 1 | `fault:common/1` | 044.mp3 | 触碰保护已触发，请检查。 |
| COMMON_ERROR bit2 | Ipc / 16 | `fault:common/2` | 050.mp3 | 工控机故障，请检查。 |
| COMMON_ERROR bit3 | LimitOrCoupling / 3 | `fault:common/3` | 041.mp3 | 举升限位异常，请检查。 |
| COMMON_ERROR bit4 | LimitOrCoupling / 4 | `fault:common/4` | 041.mp3 | 料车连接异常，请检查。 |
| COMMON_ERROR bit5 | Parameter / 5 | `fault:common/5` | 100.mp3 | 工控机参数匹配异常，请检查。 |
| COMMON_ERROR bit6 | Other / 106 | `fault:common/6` | 无匹配录音，TTS | 舵轮差速转向超时，请检查。 |
| COMMON_WARNING bit0 | NavigationCommunication / 0 | `fault:warning/0` | 101.mp3 | 导航通信端口掉线，请检查。 |
| COMMON_WARNING bit1 | UpperCommunication / 1 | `fault:warning/1` | 102.mp3 | 上装通信端口掉线，请检查。 |
| COMMON_WARNING / PING_TIMEOUT | Other / 2 | `fault:network/ping_timeout` | 042.mp3 | 网络连接超时，请检查。 |
| ERROR_RCS_TASK | Other / 1 | `fault:task/rcs` | 无匹配录音，TTS | 调度任务异常，请检查任务状态。 |
| ERROR_QR_LOC_NOT_INIT | Ipc / 0 | `fault:ipc/ERROR_QR_LOC_NOT_INIT` | 103.mp3 | 二维码定位尚未初始化，请检查。 |
| ERROR_QR_LOC_INITING | Ipc / 1 | `fault:ipc/ERROR_QR_LOC_INITING` | 104.mp3 | 二维码定位正在初始化，请稍候。 |
| ERROR_QR_LOC_INITED | Ipc / 2 | `fault:ipc/ERROR_QR_LOC_INITED` | 055.mp3 | 定位状态异常，请检查。 |
| ERROR_QR_LOC_ODOM_ERROR | Ipc / 3 | `fault:ipc/ERROR_QR_LOC_ODOM_ERROR` | 106.mp3 | 定位里程计异常，请检查。 |
| ERROR_QR_LOC_QRCODE_LOST | Ipc / 4 | `fault:ipc/ERROR_QR_LOC_QRCODE_LOST` | 无匹配录音，TTS | 二维码丢失，请检查定位。 |
| ERROR_QR_LOC_ODOM_NO_DATA | Ipc / 5 | `fault:ipc/ERROR_QR_LOC_ODOM_NO_DATA` | 108.mp3 | 里程计数据缺失，请检查。 |
| ERROR_NAVI_DERAILMENT | Ipc / 6 | `fault:ipc/ERROR_NAVI_DERAILMENT` | 109.mp3 | 车辆偏离导航路径，请检查。 |
| ERROR_NAVI_OVER_POINT | Ipc / 7 | `fault:ipc/ERROR_NAVI_OVER_POINT` | 无匹配录音，TTS | 车辆超过目标点，请检查。 |
| ERROR_QR_LOC_NO_DATA | Ipc / 8 | `fault:ipc/ERROR_QR_LOC_NO_DATA` | 111.mp3 | 二维码定位数据缺失，请检查。 |
| ERROR_RCS_EXIST_ERROR | Ipc / 9 | `fault:ipc/ERROR_RCS_EXIST_ERROR` | 无匹配录音，TTS | 调度系统异常，请检查。 |
| ERROR_QR_LOC_ODOM_WHEEL_PULSE_NOT_UPDATE | Ipc / 10 | `fault:ipc/ERROR_QR_LOC_ODOM_WHEEL_PULSE_NOT_UPDATE` | 113.mp3 | 编码器脉冲未更新，请检查。 |
| ERROR_QR_LOC_CANNOT_READ_MORE | Ipc / 11 | `fault:ipc/ERROR_QR_LOC_CANNOT_READ_MORE` | 无匹配录音，TTS | 二维码连续读取异常，请检查。 |
| ERROR_QR_LOC_ROT_LOSS_CODE | Ipc / 12 | `fault:ipc/ERROR_QR_LOC_ROT_LOSS_CODE` | 无匹配录音，TTS | 旋转过程中二维码丢失，请检查。 |
| ERROR_NOT_FIND_QR | Ipc / 13 | `fault:ipc/ERROR_NOT_FIND_QR` | 无匹配录音，TTS | 未找到二维码，请检查。 |
| ERROR_NAVI_WHEEL_SLIP | Ipc / 14 | `fault:ipc/ERROR_NAVI_WHEEL_SLIP` | 无匹配录音，TTS | 车辆车轮打滑，请检查。 |
| ERROR_NAVI_ARRIVE_NOT_FINISH | Ipc / 15 | `fault:ipc/ERROR_NAVI_ARRIVE_NOT_FINISH` | 无匹配录音，TTS | 车辆到点任务未完成，请检查。 |

COMMON_ERROR/COMMON_WARNING 的 errorDescription 按十进制 uint32 位图拆分；PING_TIMEOUT 先按具名告警匹配。bit2 是 IPC 汇总，在 IPC 类内排在全部16个具名错误之后。bit6 保持 Other/order=106。

## DEVICE_ERROR

`errorDescription="设备ID -> 故障码"`，设备ID按8位、故障码按16位原值恢复，不拆位。`9 -> -1`、`9 -> 65535`、`9 -> 0xffff` 统一为稳定键 `device/9/65535`。ID=0不生成候选；非零ID的未知码（包括0）继续保留。

| 设备ID | 设备 | 默认JSON资源键 | 录音 |
| ---: | --- | --- | --- |
| 1 | 扩展板 | `fault:device/1/*` | 061.mp3 |
| 2 | 陀螺仪 | `fault:device/2/*` | 062.mp3 |
| 3 | 拉绳 | `fault:device/3/*` | 063.mp3 |
| 4 | 电池 | `fault:device/4/*` | 064.mp3 |
| 5 | 避障器 | `fault:device/5/*` | 065.mp3 |
| 6 | 预留设备 | `fault:device/6/*` | 未提供，使用TTS |
| 7 | 上装动作 | `fault:device/7/*` | 067.mp3 |
| 8 | 视觉避障器 | `fault:device/8/*` | 068.mp3 |
| 9 | 驱动器1 | `fault:device/9/*` | 069.mp3 |
| 10 | 驱动器2 | `fault:device/10/*` | 070.mp3 |
| 11 | 驱动器3 | `fault:device/11/*` | 071.mp3 |
| 12 | 驱动器4 | `fault:device/12/*` | 072.mp3 |
| 13 | 驱动器5 | `fault:device/13/*` | 073.mp3 |
| 14 | 驱动器6 | `fault:device/14/*` | 074.mp3 |
| 15 | 驱动器7 | `fault:device/15/*` | 未提供，使用TTS |
| 16 | 驱动器8 | `fault:device/16/*` | 未提供，使用TTS |

设备录音通常只说“异常或掉线”，不包含具体故障码。故障身份仍保留设备ID和原始码：同一故障重复输入不重启，码值变化仍属于新的故障。未配置 text 的设备项使用代码映射文案作 TTS：驱动器1～8的65535播“驱动器N掉线，复位无法清除，需断电重启。”，其他已知设备播“设备名故障，故障码X。”。

资源命中次序：`fault:device/9/65535` → `fault:device/9/*` → `fault` → 内置文案。精确条目存在就使用该条目；它的 file 为空或不可用时直接 TTS，不转播设备通用录音。`*` 仅支持该设备资源键位置，不是通用通配表达式。

未知但可解析的设备ID使用含ID和码值的TTS；描述无法解析使用 `fault:device/unknown` 对应051。6、15、16没有匹配素材，默认TTS。设备7并未提供举升/牵引/辊道等机构ID，不能将046～049自动绑定到它。

## 未映射项与编号差异

未定义 COMMON_ERROR 位n使用 `fault:common/n`；未定义 COMMON_WARNING 位n使用 `fault:warning/n`；未知类型使用 `fault:other/TYPE`，WARNING级别为通用告警，其余为通用故障。非法位图使用 `fault:input/TYPE`。不会直接朗读任意 errorDescription。

- 105是“初始化完成”，当前 ERROR_QR_LOC_INITED 在 errors 中被宿主标记FATAL，本包沿用错误候选并使用055，不自动生成成功事件。
- 107是“二维码间距过大”，当前丢码条件未明确该原因，保留丢码TTS。
- 110是“RCS任务点越界”，当前 ERROR_NAVI_OVER_POINT 是运动越过路径点，不能对应。
- 114包含“初始化失败”，现有无法继续读取的错误没有足够信息确认这一限定，保留TTS。
- 115是“配置参数异常”，当前相同枚举顺序对应旋转丢码，不能按103+序号自动绑定。
- 080～085来自MCU独立touch_err位图；现有State未携带，不能当DEVICE_ERROR码或COMMON_ERROR位。

这些录音全部保留，详见 AUDIO_INVENTORY.md。后续确认实际 errorType/description 或字段后，补 AnnouncementTable/StateParser 条件以及 voices 条目即可；引擎无需修改。

依据：用户上传主工程 task_manager/src/task_manager.cpp 的 PublishState、task_manager/include/task_base.h 的 IPC_ERROR_CODE_LIST、task_manager/src/route_task.cpp 的错误设置，以及此前确认的MCU/设备协议映射。素材标签取自本次ZIP文件名。
