#include "Preprocessing.h"

namespace Preprocessing {
    void remove_dc(DVector& data) {
        if (data.empty()) return;
        double sum = 0;
        for (double v : data) sum += v;
        double mean = sum / data.size();
        for (double& v : data) v -= mean;
    }
}
