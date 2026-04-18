[Qt GUI]
  选择文件 / 通道 / 模式 / 参数 / 点击开始
       |
       v
[QtAnalysisFacade]
  把Qt参数转成内部请求
       |
       v
[Engine]
       |
       v
[TaskPlanner]
       |
       v
[FFTExecutor]
   |         |         |
   v         v         v
FFTFlow  FFTvsTimeFlow  OctaveFlow
   |         |            |
   v         v            v
DataReaderService / SpectrumService / ExportService / Features
       |
       v
[结果文件 + 峰值 + 预览结果]
       |
       v
[Qt GUI显示]