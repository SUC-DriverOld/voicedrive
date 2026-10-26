# VoiceDrive

对停更的 JUCE 5.4.7 VST2 插件 `VOICEDRIVE_64.dll` 的逆向分析与 VST3 重写，基于 IDA Pro 逆向。

## 构建

```bash
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

## 说明

- 按原版精确复刻（162x200 原比例、2x 放大渲染）：三个纵向面板 VIBRATION / DRIVE ELEMENTS / MASTER。预设下拉带 ROUGH VOICE / POP GROWL / EXTREME 分组标题，9 个预设与原版一致。
- 参数（27 个，顺序与原版一致）：effectLevel, presetNo, oscWaveform, oscSpeed, oscNoiseRatio, jitter, shimmer, shimmerFreq1, shimmerFreq2, f0EstType, f0EstRgeMin, f0EstRgeMax, bstLevel, bstAlgo, bstRgeTopMin, bstRgeTopMax, bstRgeBtmMin, bstRgeBtmMax, shiftLevel, shiftRgeMin, shiftRgeMax, shiftStyle, shiftRate, shiftMixLevel, addGain, lagOrder, phaseShift。前 8 个为可自动化参数（对应 UI 控件），其余为引擎参数（非自动化，与原版一致）。
- 结果与差异：全部 9 个预设稳定渲染、输出量级与原版同阶；逐位一致未达成（原版依赖 Intel IPP FFT，此处使用 JUCE FFT 替代）。
