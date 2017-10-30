#ifndef TASK_ERRORS_H
#define TASK_ERRORS_H

#include "base_error/base_error.h"

namespace taskErr
{
    enum taskError : int
    {
        FORWARDING = 0,     // Code 0 errors always indicate the forwarding type, hence it has to be set manually to zero

        SERVICE_REQ_FAIL   // Service request failed
    };
}

#endif