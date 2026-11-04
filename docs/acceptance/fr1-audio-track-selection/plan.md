# FR-1 多音轨选择验收 plan

- 需求：`docs/REQUIREMENTS.md:86` FR-1（含多音轨视频的轨道选择，现仅取 best stream）。
- 方案：`docs/spec/fr1-audio-track-selection.md`（D1–D8 设计 + §7.1 主线程拍板——实现以 §7.1 为准）。
- 改动面：spec §4 文件级清单（fftranscoder / ffextractor / commandlayer / mediaservice /
  mainwidget / fr24 契约文档 / CLAUDE.md；data.h 零改）。
- 验证：TRK-001..013 + TRK-GUI-001/002（spec §6）+ 零漂移回归网全量复跑 + ci-smoke 接线串行全绿；
  素材配方「先造 wav 再 mux」（-ac 位序陷阱）、各轨规格错开（44.1k/2、48k/1、32k/2）、default
  旗标设计在后轨、非连续 index mkv；红绿纪律：缺省零漂移门先行（不带 track 输出 vs 基线逐字节），
  再上选轨正例。
- 挂账：Windows round-4 真机；probeInputSpec 首轨规格既有瑕疵不动（spec §7.1 拍板 C 记档）。
