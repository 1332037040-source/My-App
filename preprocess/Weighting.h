#pragma once
#include "core/common.h"
#include <string>
#include <vector>

namespace Weighting {

    enum class WeightType {
        None,
        A,
        B,
        C,
        D
    };

    // 对 FFT 结果应用计权（原地修改）
    void apply_weighting(CVector& fft_result, uint32_t sample_rate, WeightType wtype);

    // 对时域信号应用频率计权（原地修改）
    // 说明：
    // - 当前实现采用“频域加权 + IFFT 回时域”的离线方式，
    //   适合 Level vs Time 这种离线分析。
    // - 若后续需要实时处理，再替换成双二阶 IIR 实现。
    void apply_weighting_time_domain(DVector& signal, uint32_t sample_rate, WeightType wtype);

    // 获取计权类型名称（用于菜单显示）
    std::string weight_type_to_string(WeightType wtype);
}