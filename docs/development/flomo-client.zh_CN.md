<p align="right">
  <a href="flomo-client.md">English</a> · <strong>简体中文</strong>
</p>

# flomo 语音备忘客户端

衍生应用(分支 `feature/flomo-client`):按住确定键录音,云端 ASR 转文字,经
flomo Incoming Webhook 写入。离线备忘录存入 littlefs 分区排队,联网后自动补传。

## 前置条件

- **flomo PRO**:Incoming Webhook(`https://flomoapp.com/iwh/<token>`)是 PRO 功能。
  flomo 有限额但未公开;客户端对重试做了节流(HTTP 429 基础退避 60 秒,上限
  600 秒)。
- 一个 OpenAI 兼容的语音转写端点与 API key。默认指向
  `https://api.groq.com/openai/v1/audio/transcriptions`(`whisper-large-v3`),
  任何兼容端点均可。

## 首次配置(SoftAP 网页配网,xiaozhi 风格)

1. 开机后在主页用下键翻页到 `设置`,选 `开始配网`:设备开启开放热点
   `FoloPassport-XXXX`(末四位取自 MAC),配网页显示热点名与
   `192.168.4.1`。
2. 手机连上热点,浏览器打开 `http://192.168.4.1/`:下拉选择扫描到的网络
   (或手动输入)并填密码,再填 flomo webhook / ASR 配置,点"保存并连接"。
3. 设备保存配置(`flomo_config` 部分更新语义)→ 应用 Wi-Fi 凭证连接 →
   页面轮询 `/status.json` 显示结果;连接成功后热点保留约 5 秒再关闭,
   设备回主页。
4. 联网后 `http://<设备IP>/` 是同一页面的维护入口(无 Wi-Fi 区块)。
   ASR key 只写 NVS:永不回显、永不写日志;页面留空提交表示保持已存值。
5. `清除配置` 会清空 flomo/ASR 配置与已保存的 Wi-Fi 凭证。

架构要点:Wi-Fi 协议栈由 `flomo_wifi` 唯一持有,STA 与 AP netif 在
`esp_wifi_start()` 前一并创建;配网期间切 `WIFI_MODE_APSTA` 并挂起 STA
自动重连(旧凭证不能在后台连上打断换网),退出配网回 STA-only,未连接且
有已存凭证则恢复连接。`esp_http_server`(flomo_websrv)同时服务配网
(192.168.4.1)与日常维护(设备 IP)两个入口,配置应用统一走
`apply_config` 钩子。配网不走蓝牙:BluFi/NimBLE 相关代码与 sdkconfig
配置已移除,腾出 RAM/flash,并恢复 mbedtls 硬件 MPI 加速 TLS。

## 日常使用

- **录音**:主页长按 OK 开始;单击 OK 结束,上/下键丢弃。单条最长 120 秒
  (16 kHz/16bit 单声道 WAV,约 1.9 MB/分钟)。
- **同步**:录完自动转文字并发送;失败则留在队列。`待传队列` 页显示条数、
  最老一条的时长与重试次数;OK 立即重传,长按 OK 删除最老一条。
- 一条备忘录最多尝试 5 次,超限放弃(HTTP 4xx 如 webhook token 失效则立即放弃)。
- 电量百分比显示在右上角;15 秒无操作熄屏。`关机` 进入 deep sleep——板卡没有
  已验证的按键唤醒电路,唤醒需按复位键或重新上电。

## 分区布局

本分支 `partitions.csv` 划出约 4.8 MB `storage`(littlefs)作离线队列——约合
2.5 分钟 16 kHz/16bit 单声道音频——factory 应用分区缩小到约 3.06 MB。烧录合并
镜像会重排原单应用布局。

## 验证状态

主机测试覆盖 WAV 头、JSON 解析/转义、配置解析、队列重试策略与三键映射;
仓库测试强制显示面的每个中文字面量都在字库子集覆盖内。录音质量、SoftAP
配网与页面交互、ASR 效果、flomo 写入、中文渲染等设备侧行为在完成硬件测试前
均列为未验证。
