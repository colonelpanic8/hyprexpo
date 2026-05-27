#include "Internals.hpp"

bool COverview::usesLivePreview() const {
    return previewMode == EPreviewMode::LIVE;
}

bool COverview::usesCachedPreview() const {
    return previewMode == EPreviewMode::CACHED;
}

std::vector<CBox> COverview::currentTileBoxes() const {
    if (!pMonitor)
        return {};

    static const CConfigValue<Config::INTEGER> PGAPOUT("plugin:hyprexpo:gap_size_outer");

    const auto                                 GAPSIZE        = (closing ? (1.0 - size->getPercent()) : size->getPercent()) * GAP_WIDTH;
    const double                               outer          = *PGAPOUT * (closing ? (1.0 - size->getPercent()) : size->getPercent());
    Vector2D                                   SIZE           = size->value();
    Vector2D                                   tileRenderSize = (SIZE - Vector2D{GAPSIZE, GAPSIZE} * (SIDE_LENGTH - 1) - Vector2D{outer * 2, outer * 2}) / SIDE_LENGTH;

    std::vector<CBox>                          tileBoxes(images.size());
    for (size_t y = 0; y < (size_t)SIDE_LENGTH; ++y) {
        for (size_t x = 0; x < (size_t)SIDE_LENGTH; ++x) {
            CBox box = {outer + x * tileRenderSize.x + x * GAPSIZE, outer + y * tileRenderSize.y + y * GAPSIZE, tileRenderSize.x, tileRenderSize.y};
            box.scale(pMonitor->m_scale).translate(pos->value());
            box.round();
            tileBoxes[x + y * SIDE_LENGTH] = box;
        }
    }

    return tileBoxes;
}
