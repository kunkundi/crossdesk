/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CUDA_GRAPHICS_H_
#define _CUDA_GRAPHICS_H_

namespace crossdesk {

// Call before SDL or Slint initializes EGL/GLX. Select NVIDIA on hybrid
// systems with a PRIME offload GPU screen so GL can share NVDEC buffers.
// Keep the default driver when NVIDIA already owns the display or the
// display server does not advertise offload support.
void ConfigureLinuxCudaGraphics();

}  // namespace crossdesk

#endif
