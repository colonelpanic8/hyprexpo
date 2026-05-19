#include "Internals.hpp"
#include "OverviewPassElement.hpp"

using namespace Hyprutils::String;
using namespace Internals;

void COverview::redrawID(int id, bool forcelowres) {
    if (!pMonitor)
        return;

    if (pMonitor->m_activeWorkspace != startedOn && !closing) {
        // likely user changed.
        onWorkspaceChange();
    }

    blockOverviewRendering = true;

    Render::GL::g_pHyprOpenGL->makeEGLCurrent();

    id = std::clamp(id, 0, SIDE_LENGTH * SIDE_LENGTH - 1);

    Vector2D tileSize       = pMonitor->m_size / SIDE_LENGTH;
    Vector2D tileRenderSize = (pMonitor->m_size - Vector2D{GAP_WIDTH, GAP_WIDTH} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;
    CBox     monbox{0, 0, tileSize.x * 2, tileSize.y * 2};

    if (!forcelowres && (size->value() != pMonitor->m_size || closing))
        monbox = {{0, 0}, pMonitor->m_pixelSize};

    if (!ENABLE_LOWRES)
        monbox = {{0, 0}, pMonitor->m_pixelSize};

    auto& image = images[id];

    ensureFramebuffer(image, monbox, framebufferFormatWithAlpha(pMonitor->m_output->state->state().drmFormat));

    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->beginRender(pMonitor.lock(), fakeDamage, Render::RENDER_MODE_FULL_FAKE, nullptr, image.fb);

    clearWithColor(CHyprColor{0, 0, 0, 1.0});

    const auto PWORKSPACE = image.pWorkspace ? image.pWorkspace : g_pCompositor->getWorkspaceByID(image.workspaceID);
    image.pWorkspace      = PWORKSPACE;

    PHLWORKSPACE openSpecial = pMonitor->m_activeSpecialWorkspace;
    if (openSpecial)
        pMonitor->m_activeSpecialWorkspace.reset();

    startedOn->m_visible = false;

    if (PWORKSPACE) {
        const auto PREVIOUSWS    = activateWorkspaceForPreview(pMonitor.lock(), PWORKSPACE);
        const auto PREVIEWSTATE  = applyWorkspacePreviewState(PWORKSPACE);

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace = openSpecial;

        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

        restoreWorkspacePreviewState(PWORKSPACE, PREVIEWSTATE);
        restoreActiveWorkspaceAfterPreview(pMonitor.lock(), PREVIOUSWS);

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace.reset();
    } else
        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

    g_pHyprRenderer->m_renderData.blockScreenShader = true;
    g_pHyprRenderer->endRender();

    pMonitor->m_activeSpecialWorkspace = openSpecial;
    pMonitor->m_activeWorkspace        = startedOn;
    startedOn->m_visible               = true;
    g_pDesktopAnimationManager->startAnimation(startedOn, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);

    blockOverviewRendering = false;
}

void COverview::redrawAll(bool forcelowres) {
    if (!pMonitor)
        return;
    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        redrawID(i, forcelowres);
    }
}

void COverview::damage() {
    blockDamageReporting = true;
    g_pHyprRenderer->damageMonitor(pMonitor.lock());
    blockDamageReporting = false;
}

void COverview::onDamageReported() {
    damageDirty = true;

    Vector2D                                   SIZE = size->value();

    static const CConfigValue<Config::INTEGER> PGAPOUT("plugin:hyprexpo:gap_size_outer");

    const auto                                 GAPSIZE        = (closing ? (1.0 - size->getPercent()) : size->getPercent()) * GAP_WIDTH;
    const auto                                 OUTER          = *PGAPOUT * (closing ? (1.0 - size->getPercent()) : size->getPercent());
    Vector2D                                   tileRenderSize = (SIZE - Vector2D{GAPSIZE, GAPSIZE} * (SIDE_LENGTH - 1) - Vector2D{OUTER * 2, OUTER * 2}) / SIDE_LENGTH;
    CBox                                       texbox         = CBox{OUTER + (openedID % SIDE_LENGTH) * tileRenderSize.x + (openedID % SIDE_LENGTH) * GAPSIZE,
                       OUTER + (openedID / SIDE_LENGTH) * tileRenderSize.y + (openedID / SIDE_LENGTH) * GAPSIZE, tileRenderSize.x, tileRenderSize.y}
                      .translate(pMonitor->m_position);

    damage();

    blockDamageReporting = true;
    g_pHyprRenderer->damageBox(texbox);
    blockDamageReporting = false;
    g_pCompositor->scheduleFrameForMonitor(pMonitor.lock());
}

int64_t COverview::selectedWorkspaceID() const {
    const int ID = closeOnID == -1 ? openedID : closeOnID;
    if (ID < 0 || ID >= (int)images.size())
        return WORKSPACE_INVALID;

    return images[ID].workspaceID;
}

void COverview::close(bool switchToSelection) {
    if (closing)
        return;

    const int   ID     = closeOnID == -1 ? openedID : closeOnID;
    const int   SAFEID = std::clamp(ID, 0, SIDE_LENGTH * SIDE_LENGTH - 1);

    const auto& TILE = images[SAFEID];

    Vector2D    tileSize = (pMonitor->m_size / SIDE_LENGTH);

    size->warp();
    pos->warp();

    *size = pMonitor->m_size * pMonitor->m_size / tileSize;
    *pos  = (-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{SAFEID % SIDE_LENGTH, SAFEID / SIDE_LENGTH}) * pMonitor->m_scale) * (pMonitor->m_size / tileSize);

    closing = true;

    redrawAll();

    if (switchToSelection && TILE.workspaceID != pMonitor->activeWorkspaceID()) {
        pMonitor->setSpecialWorkspace(0);

        // If this tile's workspace was WORKSPACE_INVALID, move to the next
        // empty workspace. This should only happen if skip_empty is on, in
        // which case some tiles will be left with this ID intentionally.
        const int  NEWID = TILE.workspaceID == WORKSPACE_INVALID ? getWorkspaceIDNameFromString("emptynm").id : TILE.workspaceID;

        const auto NEWIDWS = g_pCompositor->getWorkspaceByID(NEWID);

        const auto OLDWS = pMonitor->m_activeWorkspace;

        if (!NEWIDWS)
            Config::Actions::changeWorkspace(std::to_string(NEWID));
        else
            Config::Actions::changeWorkspace(NEWIDWS->getConfigName());

        g_pDesktopAnimationManager->startAnimation(pMonitor->m_activeWorkspace, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);
        g_pDesktopAnimationManager->startAnimation(OLDWS, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true);

        startedOn = pMonitor->m_activeWorkspace;
    }

    size->setCallbackOnEnd([](auto) { g_pEventLoopManager->doLater([] { g_pOverview.reset(); }); });
}

void COverview::onPreRender() {
    if (damageDirty) {
        damageDirty = false;
        redrawID(closing ? (closeOnID == -1 ? openedID : closeOnID) : openedID);
    }
}

void COverview::onWorkspaceChange() {
    if (valid(startedOn))
        g_pDesktopAnimationManager->startAnimation(startedOn, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true);
    else
        startedOn = pMonitor->m_activeWorkspace;

    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        if (images[i].workspaceID != pMonitor->activeWorkspaceID())
            continue;

        openedID = i;
        break;
    }

    closeOnID = openedID;
    if (!closing)
        close();
}

void COverview::render() {
    g_pHyprRenderer->m_renderPass.add(makeUnique<COverviewPassElement>());
}

void COverview::fullRender() {
    const auto GAPSIZE = (closing ? (1.0 - size->getPercent()) : size->getPercent()) * GAP_WIDTH;

    if (pMonitor->m_activeWorkspace != startedOn && !closing) {
        // likely user changed.
        onWorkspaceChange();
    }

    Vector2D                                   SIZE = size->value();

    static const CConfigValue<Config::INTEGER> PGAPOUT("plugin:hyprexpo:gap_size_outer");
    static const CConfigValue<Config::INTEGER> PTILEROUND("plugin:hyprexpo:tile_rounding");
    static const CConfigValue<Config::FLOAT>   PROUNDINGPOWER("plugin:hyprexpo:tile_rounding_power");
    static const CConfigValue<Config::INTEGER> PTILEROUNDHOVER("plugin:hyprexpo:tile_rounding_hover");
    static const CConfigValue<Config::INTEGER> PTILEROUNDFOCUS("plugin:hyprexpo:tile_rounding_focus");
    static const CConfigValue<Config::INTEGER> PTILEROUNDCURRENT("plugin:hyprexpo:tile_rounding_current");
    static const CConfigValue<Config::INTEGER> PLABELENABLE("plugin:hyprexpo:label_enable");
    static const CConfigValue<Config::STRING>  PLABELMODE("plugin:hyprexpo:label_text_mode");
    static const CConfigValue<Config::STRING>  PLABELTOKENMAP("plugin:hyprexpo:label_token_map");
    static const CConfigValue<Config::STRING>  PLABELPOS("plugin:hyprexpo:label_position");
    static const CConfigValue<Config::INTEGER> PLABELOFFX("plugin:hyprexpo:label_offset_x");
    static const CConfigValue<Config::INTEGER> PLABELOFFY("plugin:hyprexpo:label_offset_y");
    static const CConfigValue<Config::INTEGER> PSELECTLABELENABLE("plugin:hyprexpo:selection_label_enable");
    static const CConfigValue<Config::STRING>  PSELECTLABELTOKENS("plugin:hyprexpo:selection_label_token_map");
    static const CConfigValue<Config::STRING>  PSELECTLABELPOS("plugin:hyprexpo:selection_label_position");
    static const CConfigValue<Config::INTEGER> PSELECTLABELOFFX("plugin:hyprexpo:selection_label_offset_x");
    static const CConfigValue<Config::INTEGER> PSELECTLABELOFFY("plugin:hyprexpo:selection_label_offset_y");
    static const CConfigValue<Config::INTEGER> PSELECTLABELCOL("plugin:hyprexpo:selection_label_color");
    static const CConfigValue<Config::STRING>  PLABELSHOW("plugin:hyprexpo:label_show");
    static const CConfigValue<Config::INTEGER> PLABELSIZE("plugin:hyprexpo:label_font_size");
    static const CConfigValue<Config::STRING>  PLABELFONT("plugin:hyprexpo:label_font_family");
    static const CConfigValue<Config::INTEGER> PLABELBOLD("plugin:hyprexpo:label_font_bold");
    static const CConfigValue<Config::INTEGER> PLABELITALIC("plugin:hyprexpo:label_font_italic");
    static const CConfigValue<Config::INTEGER> PLABELCOLDEFAULT("plugin:hyprexpo:label_color_default");
    static const CConfigValue<Config::INTEGER> PLABELCOLHOVER("plugin:hyprexpo:label_color_hover");
    static const CConfigValue<Config::INTEGER> PLABELCOLFOCUS("plugin:hyprexpo:label_color_focus");
    static const CConfigValue<Config::INTEGER> PLABELCOLCURRENT("plugin:hyprexpo:label_color_current");
    static const CConfigValue<Config::INTEGER> PWORKSPACENUMCOL("plugin:hyprexpo:workspace_number_color");
    static const CConfigValue<Config::INTEGER> PLABELBGENABLE("plugin:hyprexpo:label_bg_enable");
    static const CConfigValue<Config::INTEGER> PLABELBGCOLOR("plugin:hyprexpo:label_bg_color");
    static const CConfigValue<Config::INTEGER> PLABELBGROUND("plugin:hyprexpo:label_bg_rounding");
    static const CConfigValue<Config::INTEGER> PLABELBGPAD("plugin:hyprexpo:label_padding");
    static const CConfigValue<Config::INTEGER> PLABELPIXELSNAP("plugin:hyprexpo:label_pixel_snap");
    static const CConfigValue<Config::INTEGER> PBORDERWIDTH("plugin:hyprexpo:border_width");
    static const CConfigValue<Config::INTEGER> PBORDERCURRENT("plugin:hyprexpo:border_color_current");
    static const CConfigValue<Config::INTEGER> PBORDERHOVER("plugin:hyprexpo:border_color_hover");
    static const CConfigValue<Config::INTEGER> PBORDERFOCUS("plugin:hyprexpo:border_color_focus");

    const double                               outer          = *PGAPOUT * (closing ? (1.0 - size->getPercent()) : size->getPercent());
    Vector2D                                   tileRenderSize = (SIZE - Vector2D{GAPSIZE, GAPSIZE} * (SIDE_LENGTH - 1) - Vector2D{outer * 2, outer * 2}) / SIDE_LENGTH;

    clearWithColor(BG_COLOR.stripA());

    const int   baseRoundPx    = std::max(0, (int)std::lround((double)*PTILEROUND * pMonitor->m_scale));
    const int   hoverRoundPx   = *PTILEROUNDHOVER >= 0 ? std::max(0, (int)std::lround((double)*PTILEROUNDHOVER * pMonitor->m_scale)) : baseRoundPx;
    const int   focusRoundPx   = *PTILEROUNDFOCUS >= 0 ? std::max(0, (int)std::lround((double)*PTILEROUNDFOCUS * pMonitor->m_scale)) : baseRoundPx;
    const int   currentRoundPx = *PTILEROUNDCURRENT >= 0 ? std::max(0, (int)std::lround((double)*PTILEROUNDCURRENT * pMonitor->m_scale)) : baseRoundPx;
    const float roundingPower  = *PROUNDINGPOWER;

    auto        tileBoxFor = [&](size_t x, size_t y) {
        CBox box = {outer + x * tileRenderSize.x + x * GAPSIZE, outer + y * tileRenderSize.y + y * GAPSIZE, tileRenderSize.x, tileRenderSize.y};
        box.scale(pMonitor->m_scale).translate(pos->value());
        box.round();
        return box;
    };

    auto tileRoundFor = [&](int id, const CBox& box) {
        int round = baseRoundPx;
        if (id == kbFocusID)
            round = focusRoundPx;
        else if (id == openedID)
            round = currentRoundPx;
        else if (id == hoveredID)
            round = hoverRoundPx;

        return std::min(round, std::max(0, (int)std::floor(std::min(box.w, box.h) / 2.0)));
    };

    std::vector<CBox> tileBoxes(images.size());

    for (size_t y = 0; y < (size_t)SIDE_LENGTH; ++y) {
        for (size_t x = 0; x < (size_t)SIDE_LENGTH; ++x) {
            const int id     = x + y * SIDE_LENGTH;
            CBox      texbox = tileBoxFor(x, y);
            tileBoxes[id]    = texbox;

            CRegion damage{0, 0, INT16_MAX, INT16_MAX};
            auto&   image = images[id];
            Render::GL::g_pHyprOpenGL->renderTextureInternal(image.fb->getTexture(), texbox,
                                                             {.damage = &damage, .a = 1.0, .round = tileRoundFor(id, texbox), .roundingPower = roundingPower});
        }
    }

    const bool labelsEnabled          = *PLABELENABLE || showWorkspaceNumbers;
    const bool selectionLabelsEnabled = *PSELECTLABELENABLE;
    if (labelsEnabled || selectionLabelsEnabled) {
        const std::string tokenMapConfig  = *PLABELTOKENMAP;
        const auto        tokenMap        = tokenMapConfig.empty() ? std::vector<std::string>{} : splitCommaList(tokenMapConfig);
        const std::string selectMapConfig = *PSELECTLABELTOKENS;
        const auto        selectMap       = selectMapConfig.empty() ? std::vector<std::string>{} : splitCommaList(selectMapConfig);
        size_t            visible         = 0;

        auto drawLabel = [&](COverview::SWorkspaceImage::SLabelTexture& cache, const std::string& label, uint64_t color, const std::string& position, int offsetX, int offsetY,
                             const CBox& tileBox) {
            if (label.empty())
                return;

            const int fontSize = std::max(8, (int)*PLABELSIZE);
            auto      tex      = ensureLabelTexture(cache, label, color, fontSize, *PLABELFONT, *PLABELBOLD, *PLABELITALIC);
            if (!tex || tex->m_texID == 0 || cache.sizePx.x <= 0 || cache.sizePx.y <= 0)
                return;

            const Vector2D labelSize = cache.sizePx;
            CBox           labelBox  = {0, 0, labelSize.x, labelSize.y};
            const double   offX      = offsetX * pMonitor->m_scale;
            const double   offY      = offsetY * pMonitor->m_scale;

            if (position == "top-left") {
                labelBox.x = tileBox.x + offX;
                labelBox.y = tileBox.y + offY;
            } else if (position == "top-right") {
                labelBox.x = tileBox.x + tileBox.w - labelBox.w - offX;
                labelBox.y = tileBox.y + offY;
            } else if (position == "bottom-left") {
                labelBox.x = tileBox.x + offX;
                labelBox.y = tileBox.y + tileBox.h - labelBox.h - offY;
            } else if (position == "bottom-right") {
                labelBox.x = tileBox.x + tileBox.w - labelBox.w - offX;
                labelBox.y = tileBox.y + tileBox.h - labelBox.h - offY;
            } else {
                labelBox.x = tileBox.x + (tileBox.w - labelBox.w) / 2.0 + offX;
                labelBox.y = tileBox.y + (tileBox.h - labelBox.h) / 2.0 + offY;
            }

            if (*PLABELPIXELSNAP)
                labelBox.round();

            if (*PLABELBGENABLE) {
                const double pad = *PLABELBGPAD * pMonitor->m_scale;
                CBox         bg  = {labelBox.x - pad, labelBox.y - pad, labelBox.w + pad * 2, labelBox.h + pad * 2};
                if (*PLABELPIXELSNAP)
                    bg.round();
                Render::GL::g_pHyprOpenGL->renderRect(bg, CHyprColor(*PLABELBGCOLOR), {.round = std::max(0, (int)std::lround((double)*PLABELBGROUND * pMonitor->m_scale))});
            }

            Render::GL::g_pHyprOpenGL->renderTexture(tex, labelBox, {.a = 1.0});
        };

        for (size_t id = 0; id < images.size(); ++id) {
            auto& image = images[id];
            if (image.workspaceID == WORKSPACE_INVALID)
                continue;

            const bool        isHover   = (int)id == hoveredID;
            const bool        isFocus   = (int)id == kbFocusID;
            const bool        isCurrent = (int)id == openedID;

            const std::string showMode   = *PLABELSHOW;
            const bool        shouldShow = showWorkspaceNumbers || showMode == "always" || (showMode == "hover" && isHover) || (showMode == "focus" && isFocus) ||
                (showMode == "hover+focus" && (isHover || isFocus)) || (showMode == "current+focus" && (isCurrent || isFocus));
            const CBox& tileBox = tileBoxes[id];

            if (labelsEnabled && shouldShow && (showWorkspaceNumbers || showMode != "never")) {
                std::string label;
                const auto  labelMode = showWorkspaceNumbers ? std::string{"id"} : std::string{*PLABELMODE};
                if (labelMode == "index")
                    label = std::to_string(visible + 1);
                else if (labelMode == "token")
                    label = visible < tokenMap.size() ? tokenMap[visible] : fallbackTokenForVisibleIndex(visible);
                else
                    label = std::to_string(image.workspaceID);

                int      state = 0;
                uint64_t color = showWorkspaceNumbers ? *PWORKSPACENUMCOL : *PLABELCOLDEFAULT;
                if (!showWorkspaceNumbers && isFocus) {
                    state = 2;
                    color = *PLABELCOLFOCUS;
                } else if (!showWorkspaceNumbers && isCurrent) {
                    state = 3;
                    color = *PLABELCOLCURRENT;
                } else if (!showWorkspaceNumbers && isHover) {
                    state = 1;
                    color = *PLABELCOLHOVER;
                }

                drawLabel(image.labels[state], label, color, *PLABELPOS, *PLABELOFFX, *PLABELOFFY, tileBox);
            }

            if (selectionLabelsEnabled) {
                const std::string token = visible < selectMap.size() ? selectMap[visible] : "";
                drawLabel(image.selectionLabel, token, *PSELECTLABELCOL, *PSELECTLABELPOS, *PSELECTLABELOFFX, *PSELECTLABELOFFY, tileBox);
            }

            ++visible;
        }
    }

    auto drawBorder = [&](int id, uint64_t color) {
        if (id < 0 || id >= (int)images.size() || *PBORDERWIDTH <= 0 || CHyprColor(color).a <= 0.0)
            return;

        const CBox& box = tileBoxes[id];
        Render::GL::g_pHyprOpenGL->renderBorder(box, gradientFromColor(color),
                                                {.round = tileRoundFor(id, box), .roundingPower = roundingPower, .borderSize = std::max(1, (int)*PBORDERWIDTH)});
    };

    if (hoveredID != -1 && hoveredID != openedID && hoveredID != kbFocusID)
        drawBorder(hoveredID, *PBORDERHOVER);
    if (openedID != -1)
        drawBorder(openedID, *PBORDERCURRENT);
    if (kbFocusID != -1)
        drawBorder(kbFocusID, *PBORDERFOCUS);
    if (dragMoved && dragSourceID != -1)
        drawBorder(dragSourceID, *PBORDERFOCUS);

    if (dragWindow && isTileValid(dragSourceID)) {
        const auto WINDOWBOX = dragWindow->getWindowMainSurfaceBox();
        if (WINDOWBOX.w > 0 && WINDOWBOX.h > 0) {
            const CBox&  sourceBox = tileBoxes[dragSourceID];
            const double scaleX    = sourceBox.w / pMonitor->m_size.x;
            const double scaleY    = sourceBox.h / pMonitor->m_size.y;
            const int    round     = tileRoundFor(dragSourceID, sourceBox);
            const double minW      = std::min(sourceBox.w, 24.0 * pMonitor->m_scale);
            const double minH      = std::min(sourceBox.h, 24.0 * pMonitor->m_scale);

            CBox         proxy = {
                lastMousePosLocal.x * pMonitor->m_scale - dragGrabOffset.x * scaleX,
                lastMousePosLocal.y * pMonitor->m_scale - dragGrabOffset.y * scaleY,
                std::clamp(WINDOWBOX.w * scaleX, minW, sourceBox.w),
                std::clamp(WINDOWBOX.h * scaleY, minH, sourceBox.h),
            };
            proxy.round();

            Render::GL::g_pHyprOpenGL->renderRect(proxy, CHyprColor{0.93F, 0.70F, 0.26F, dragMoved ? 0.24F : 0.14F}, {.round = round, .roundingPower = roundingPower});
            Render::GL::g_pHyprOpenGL->renderBorder(proxy, gradientFromColor(*PBORDERFOCUS),
                                                    {.round = round, .roundingPower = roundingPower, .borderSize = std::max(2, (int)*PBORDERWIDTH + 1)});
        }
    }
}
