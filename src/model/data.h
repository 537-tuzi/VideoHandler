#pragma once

// 应用全局常量：跨模块复用
inline constexpr auto kAppTitle = "视频音频处理工具";// 主窗口标题
inline constexpr auto kSuffixAudio = "_audio";// 音轨输出文件后缀
inline constexpr auto kSuffixMute = "_mute";// 无声视频输出文件后缀
inline constexpr int kCanonicalRate = 44100;// 统一采样率：处理引擎工作点
inline constexpr int kCanonicalChannels = 2;// 统一声道数：交错 float32
inline constexpr auto kSuffixDenoised = "_denoised";// 降噪输出文件后缀
inline constexpr auto kSuffixAi = "_ai";// AI 降噪输出文件后缀
inline constexpr auto kAiModelRelPath = "models/denoiser/gtcrn_simple.onnx";// 模型相对 exe 路径
inline constexpr auto kSuffixPitch = "_pitch";// 变调输出文件后缀
inline constexpr auto kSuffixVocals = "_vocals";// 人声分离输出文件后缀
inline constexpr auto kSuffixInstrumental = "_instrumental";// 伴奏提取输出文件后缀
inline constexpr auto kSepModelRelPath = "models/separator/UVR-MDX-NET-Voc_FT.onnx";// 模型相对 exe 路径
