#pragma once

// 应用全局常量：跨模块复用
inline constexpr auto kAppTitle = "视频音频处理工具";// 主窗口标题
inline constexpr auto kSuffixAudio = "_audio";// 音轨输出文件后缀
inline constexpr auto kSuffixMute = "_mute";// 无声视频输出文件后缀
inline constexpr int kCanonicalRate = 44100;// 兜底采样率：AI/分离路径输出与源信息缺失时的回落；transcode/denoise/pitch 输出跟随源
inline constexpr int kCanonicalChannels = 2;// 兜底声道数：交错 float32，输出跟随源时折到 min(源,2)
inline constexpr auto kSuffixDenoised = "_denoised";// 降噪输出文件后缀
inline constexpr auto kSuffixTranscoded = "_transcoded";// 直通转码输出文件后缀（命令层缺省命名；GUI 无对应按钮）
inline constexpr auto kSuffixAi = "_ai";// AI 降噪输出文件后缀
inline constexpr auto kAiModelRelPath = "models/denoiser/gtcrn_simple.onnx";// 模型相对 exe 路径
inline constexpr auto kSuffixPitch = "_pitch";// 变调输出文件后缀
inline constexpr auto kSuffixVocals = "_vocals";// 人声分离输出文件后缀
inline constexpr auto kSuffixInstrumental = "_instrumental";// 伴奏提取输出文件后缀
inline constexpr auto kSepModelRelPath = "models/separator/UVR-MDX-NET-Voc_FT.onnx";// 模型相对 exe 路径
inline constexpr auto kWsRootCacheFile = "index.json";// 工作区根级路径缓存文件名（可丢弃可重建，损坏按空处理）
inline constexpr auto kWsIndexFile = "workspace.json";// 工作区权威索引文件名（源登记 + 版本清单 + head）
inline constexpr auto kWsDefaultRootName = "VideoHandler-Workspace";// 缺省工作区根目录名（用户主目录下，FR-A4 OQ-1）
inline constexpr auto kSettingsWorkspaceRoot = "workspaceRoot";// QSettings 键：工作区根目录（仅 GUI 壳读写，D7 命令层/服务层不碰配置）
inline constexpr auto kSettingsModelRoot = "modelRoot";// QSettings 键：模型库根目录（仅 GUI 壳读写，FR-8 D1/OQ-3，承载边界同 workspaceRoot）
inline constexpr auto kSettingsModelEndpoint = "modelEndpoint";// QSettings 键：缺省下载源 endpoint（仅 GUI 壳读写，FR-8 D4）
inline constexpr auto kModelsIndexFile = "models.json";// 模型库清单文件名（缓存性登记非真相源：损坏按空处理、磁盘实况为准，FR-8 D1/D2）
inline constexpr auto kModelsDefaultRootName = "VideoHandler-Models";// 缺省模型库根目录名（用户主目录下，FR-8 OQ-3）
inline constexpr auto kDefaultModelEndpoint = "https://huggingface.co";// 缺省 HF endpoint（镜像经请求字段 endpoint 换前缀，FR-8 D4：参数归参数）
