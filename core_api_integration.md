# Core API 对接说明

## 1. 目的

`core_api` 是对核心分析能力的一层纯 C++ 封装接口，用于给上层程序调用，例如：

- Qt GUI
- 其他 C++ 桌面程序
- 命令行自动化程序
- 后续 DLL/SDK 封装

该接口的目标是：

- 屏蔽内部复杂执行链
- 提供统一、稳定、可调用的分析入口
- 让上层只关注“传什么参数、拿什么结果”

---

## 2. 当前入口

当前统一入口类：

- `core_api/CoreAnalysisFacade.h`
- `core_api/CoreAnalysisFacade.cpp`

核心调用方法：

```cpp
CoreAnalysisResult run(const CoreAnalysisRequest& request);
```

---

## 3. 主要文件

### 3.1 请求结构
- `core_api/CoreAnalysisRequest.h`

### 3.2 返回结构
- `core_api/CoreAnalysisResult.h`

### 3.3 统一门面
- `core_api/CoreAnalysisFacade.h`
- `core_api/CoreAnalysisFacade.cpp`

---

## 4. 调用链路

当前 `core_api` 的内部调用链如下：

```text
CoreAnalysisRequest
 -> CoreAnalysisFacade::run()
 -> RunRequest
 -> MapRunRequest()
 -> TaskBuilder::BuildFromRequest()
 -> FFT11Engine::Run()
 -> CoreAnalysisResult
```

说明：

- 上层不需要直接调用 `TaskBuilder`、`Engine`、`Flow`
- 上层只需要构造 `CoreAnalysisRequest`
- 调用 `CoreAnalysisFacade::run()`
- 读取 `CoreAnalysisResult`

---

## 5. 请求结构说明

`CoreAnalysisRequest` 当前包含以下主要字段：

```cpp
struct CoreAnalysisRequest
{
    std::string filePath;
    std::string channelName;
    std::string analysisMode;
    std::string outputDir;

    size_t fftSize = 8192;
    double overlap = 0.5;
    double freqMin = 0.0;
    double freqMax = 0.0;
    std::string weighting;

    std::string rpmChannelName;
    double rpmBinStep = 50.0;

    size_t maxThreads = 0;
    int maxRetries = 1;
    bool enableCancel = false;
};
```

---

## 6. 请求参数说明

### 6.1 `filePath`
输入文件完整路径。

示例：

```cpp
req.filePath = "C:\\data\\demo.atfx";
```

当前已验证支持的文件类型包括：

- `wav`
- `atfx`
- `hdf`

---

### 6.2 `channelName`
要分析的通道名称。

适用场景：

- ATFX 文件按通道名指定分析对象
- HDF 文件按通道名指定分析对象

说明：

- 对 `wav` 当前可忽略
- 若未填写，则按当前内部默认逻辑处理
- 若填写但文件中不存在该通道，接口会返回失败并给出错误信息

示例：

```cpp
req.channelName = "Channel 1";
```

---

### 6.3 `analysisMode`
分析模式字符串。

当前约定支持：

- `"fft"`
- `"fft_vs_time"`
- `"fft_vs_rpm"`
- `"octave"`
- `"octave_1_1"`
- `"octave_1_3"`

建议上层优先使用以下标准值：

- `fft`
- `fft_vs_time`
- `fft_vs_rpm`
- `octave_1_1`
- `octave_1_3`

示例：

```cpp
req.analysisMode = "fft";
```

---

### 6.4 `outputDir`
输出目录。

示例：

```cpp
req.outputDir = "C:\\output";
```

说明：

- 当前版本会校验该字段不能为空
- 该字段后续建议进一步接入导出路径控制
- 当前部分结果文件路径仍可能由内部默认导出规则决定

---

### 6.5 `fftSize`
FFT 块长。

示例：

```cpp
req.fftSize = 8192;
```

要求：

- 必须大于 0

---

### 6.6 `overlap`
重叠比例。

示例：

```cpp
req.overlap = 0.5;
```

要求：

- 范围必须在 `[0, 1)` 内

---

### 6.7 `weighting`
频率计权类型。

当前可用值建议：

- `"Z"`：无计权
- `"A"`
- `"C"`

示例：

```cpp
req.weighting = "Z";
```

说明：

- 空字符串或 `"Z"` 当前按无计权处理

---

### 6.8 `rpmChannelName`
FFT vs RPM 模式下的转速通道名。

示例：

```cpp
req.rpmChannelName = "RPM";
```

说明：

- 仅在 `fft_vs_rpm` 模式下使用
- 当前更适合 ATFX 数据

---

### 6.9 `rpmBinStep`
RPM 分箱步长。

示例：

```cpp
req.rpmBinStep = 50.0;
```

---

### 6.10 `maxThreads`
最大线程数。

示例：

```cpp
req.maxThreads = 0;
```

说明：

- `0` 表示自动

---

### 6.11 `maxRetries`
失败重试次数。

示例：

```cpp
req.maxRetries = 1;
```

---

### 6.12 `enableCancel`
是否允许取消。

示例：

```cpp
req.enableCancel = false;
```

---

## 7. 返回结构说明

当前返回结构：

```cpp
struct CoreAnalysisResult
{
    bool success = false;
    std::string message;

    std::string timeSignalCsv;
    std::string fftCsv;
    std::string spectrogramCsv;
    std::string octaveCsv;
    std::string reportFile;

    double peakFrequency = 0.0;
    double peakValue = 0.0;

    std::vector<std::string> generatedFiles;
};
```

---

## 8. 返回字段说明

### 8.1 `success`
本次调用是否成功。

- `true`：执行成功
- `false`：执行失败或校验失败

---

### 8.2 `message`
结果说明信息。

常见场景：

- `"OK"`
- 参数校验失败提示
- 通道未找到提示
- TaskBuilder / Engine 执行失败提示

---

### 8.3 `timeSignalCsv`
输出的时域 CSV 路径。

---

### 8.4 `fftCsv`
输出的 FFT 结果 CSV 路径。

---

### 8.5 `spectrogramCsv`
输出的时频谱 CSV 路径。

说明：

- 当前在 `fft_vs_time` 模式下由内部结果映射得到

---

### 8.6 `octaveCsv`
输出的倍频程 CSV 路径。

说明：

- 当前在倍频程模式下由内部结果映射得到

---

### 8.7 `reportFile`
报告文件路径。

说明：

- 当前可能尚未完整接入

---

### 8.8 `peakFrequency`
峰值频率。

---

### 8.9 `peakValue`
峰值幅值。

---

### 8.10 `generatedFiles`
本次分析生成的文件列表。

适合上层统一显示“已生成文件”。

---

## 9. 最小调用示例

```cpp
#include "core_api/CoreAnalysisFacade.h"

int main()
{
    CoreAnalysisFacade facade;

    CoreAnalysisRequest req;
    req.filePath = "C:\\Users\\fang\\Desktop\\Pink\\pink.atfx";
    req.channelName = "Channel 1";
    req.outputDir = "C:\\Users\\fang\\Desktop\\Pink";
    req.analysisMode = "fft";
    req.fftSize = 8192;
    req.overlap = 0.5;
    req.weighting = "Z";
    req.maxThreads = 0;
    req.maxRetries = 1;
    req.enableCancel = false;

    CoreAnalysisResult res = facade.run(req);

    if (!res.success) {
        std::cout << "failed: " << res.message << std::endl;
        return 1;
    }

    std::cout << "success" << std::endl;
    std::cout << "fftCsv = " << res.fftCsv << std::endl;
    std::cout << "peakFrequency = " << res.peakFrequency << std::endl;
    std::cout << "peakValue = " << res.peakValue << std::endl;
    return 0;
}
```

---

## 10. Qt 对接建议

Qt 侧不建议直接修改核心执行链，而是通过一层轻量适配调用 `core_api`。

建议关系如下：

```text
Qt UI
 -> QtAnalysisRequest / QtAnalysisResult（Qt类型）
 -> 转换为 CoreAnalysisRequest / CoreAnalysisResult（纯C++）
 -> CoreAnalysisFacade::run()
```

Qt 侧典型转换：

- `QString -> std::string`
- `QStringList -> std::vector<std::string>`
- `std::string -> QString`

建议 Qt 侧只做：

1. 参数收集
2. 类型转换
3. 调用 `CoreAnalysisFacade`
4. 展示结果

不建议 Qt 侧直接调用：

- `TaskBuilder`
- `TaskPlanner`
- `Engine`
- `FFTExecutor`
- `Flow`

---

## 11. 当前已验证能力

当前版本已完成以下验证：

- `CoreAnalysisFacade` 可独立调用
- FFT 模式执行成功
- 可生成时域 CSV 与 FFT CSV
- 可返回峰值频率与峰值幅值
- `channelName` 已支持按名称解析通道
- 错误通道名可返回明确失败信息

---

## 12. 当前已知说明

### 12.1 `outputDir`
当前版本已校验非空，但导出路径控制仍建议继续完善，使其完全受该字段控制。

### 12.2 `freqMin / freqMax`
字段已预留，但当前内部参数链路未完全接入。

### 12.3 多文件/多通道批量结果
当前返回结构更偏单次调用结果汇总，若后续需要复杂批量结果展示，可扩展更细粒度结果列表结构。

---

## 13. 推荐上层调用原则

建议上层始终：

1. 明确传入 `filePath`
2. 对 ATFX/HDF 明确传入 `channelName`
3. 明确传入 `analysisMode`
4. 明确传入 `outputDir`
5. 对 FFT 参数给出确定值，而不要完全依赖默认值

---

## 14. 版本建议

建议将当前阶段标记为：

- `Core API v1`

其特点为：

- 纯 C++ 可调用
- 主执行链��打通
- FFT 模式已验证
- 单通道名称选择已验证

---