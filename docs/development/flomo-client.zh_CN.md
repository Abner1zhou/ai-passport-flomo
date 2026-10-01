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

## 首次配置

1. 开机后在主页用下键翻页到 `设置`。
2. 选 `开始配网`:设备以 `BLUFI_FoloPassport` 广播,用「蓝牙配网-FoloToy AI
   PASSPORT」微信小程序下发 2.4 GHz Wi-Fi 凭证(见
   [Wi-Fi 配网指南](engineering/wifi-provisioning.zh_CN.md))。
3. 联网后,同一局域网的浏览器打开 `http://<设备IP>/`,填写 flomo webhook、
   ASR 端点、key、模型与语言。ASR key 只写 NVS:永不回显、永不写日志。
   局域网内用明文 HTTP 是明确取舍:设备 IP 动态变化,无法校验服务器证书。
4. `清除配置` 会清空 flomo/ASR 配置与已保存的 Wi-Fi 凭证。

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
仓库测试强制显示面的每个中文字面量都在字库子集覆盖内。录音质量、小程序配网、
ASR 效果、flomo 写入、中文渲染等设备侧行为在完成硬件测试前均列为未验证。
