//
// Created by fab on 04/09/2022.
//

#include "FrameGraph.hpp"

void FrameGraph::startSetup()
{
    for(auto pass : m_renderpasses)
    {
        pass->setup();
    }
}

void FrameGraph::startCompiling()
{

}
