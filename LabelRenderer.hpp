#pragma once

#include "Overview.hpp"

SP<Render::ITexture> ensureLabelTexture(COverview::SWorkspaceImage::SLabelTexture& cached, const std::string& text, uint64_t color, int fontSizePx, const std::string& fontFamily,
                                        bool bold, bool italic);
