#pragma once
#include "hook_curl.h"

void RiftAuth_PrePerform(InterceptorData& c);
void RiftAuth_PostPerform(InterceptorData& c);
