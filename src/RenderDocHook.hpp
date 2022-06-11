//
// Created by fab on 14/05/2022.
//

#ifndef GRAPHICSPLAYGROUND_RENDERDOCHOOK_HPP
#define GRAPHICSPLAYGROUND_RENDERDOCHOOK_HPP

#include "renderdoc_app.h"

class RenderDocHook
{
public:
    RenderDocHook(RENDERDOC_DevicePointer devicePointer, RENDERDOC_WindowHandle windowHandle);

    void startCapture();
    void endCapture();
private:
    bool m_startCapture = false;
    RENDERDOC_API_1_5_0* m_api = nullptr;
};


#endif //GRAPHICSPLAYGROUND_RENDERDOCHOOK_HPP
