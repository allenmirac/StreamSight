#pragma once
#include "control/StreamTask.h"

namespace control {

class Classifier {
public:
    void Apply(StreamTask& task) const;
};

} // namespace control
