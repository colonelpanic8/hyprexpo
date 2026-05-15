#pragma once

#define WLR_USE_UNSTABLE

#include "Globals.hpp"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// saves on resources, but is a bit broken rn with blur.
// hyprland's fault, but cba to fix.
constexpr bool ENABLE_LOWRES = false;

class CMonitor;

class COverview {
  public:
    COverview(PHLWORKSPACE startedOn_, bool swipe = false);
    ~COverview();

    void render();
    void damage();
    void onDamageReported();
    void onPreRender();

    void setClosing(bool closing);

    void resetSwipe();
    void onSwipeUpdate(double delta);
    void onSwipeEnd();

    // close without a selection
    void          close(bool switchToSelection = true);
    void          selectHoveredWorkspace();
    bool          selectWorkspaceByID(int64_t workspaceID);
    bool          selectVisibleIndex(size_t index);
    bool          selectVisibleToken(const std::string& token);
    int64_t       selectedWorkspaceID() const;
    void          moveKeyboardFocus(int dx, int dy);
    void          confirmKeyboardFocus();

    bool          blockOverviewRendering = false;
    bool          blockDamageReporting   = false;

    PHLMONITORREF pMonitor;
    bool          m_isSwiping = false;

    struct SWorkspaceImage {
        SP<Render::IFramebuffer> fb;
        int64_t                  workspaceID = -1;
        PHLWORKSPACE             pWorkspace;
        CBox                     box;

        struct SLabelTexture {
            SP<Render::ITexture> tex;
            Vector2D             sizePx;
            std::string          text;
            uint64_t             color      = 0;
            int                  fontSizePx = 0;
            std::string          fontFamily;
            bool                 bold   = false;
            bool                 italic = false;
        };

        std::array<SLabelTexture, 4> labels;
        SLabelTexture                selectionLabel;
    };

  private:
    void                         redrawID(int id, bool forcelowres = false);
    void                         redrawAll(bool forcelowres = false);
    void                         onWorkspaceChange();
    void                         fullRender();
    void                         updateHoveredFromMouse();
    bool                         isTileValid(int id) const;
    void                         ensureKeyboardFocus();

    int                          SIDE_LENGTH = 3;
    int                          GAP_WIDTH   = 5;
    CHyprColor                   BG_COLOR    = CHyprColor{0.1, 0.1, 0.1, 1.0};

    bool                         damageDirty = false;

    Vector2D                     lastMousePosLocal = Vector2D{};

    int                          openedID  = -1;
    int                          closeOnID = -1;
    int                          hoveredID = -1;
    int                          kbFocusID = -1;

    std::vector<SWorkspaceImage> images;

    PHLWORKSPACE                 startedOn;

    PHLANIMVAR<Vector2D>         size;
    PHLANIMVAR<Vector2D>         pos;

    bool                         closing = false;

    CHyprSignalListener          mouseMoveHook;
    CHyprSignalListener          mouseButtonHook;
    CHyprSignalListener          touchMoveHook;
    CHyprSignalListener          touchDownHook;

    bool                         swipe                = false;
    bool                         swipeWasCommenced    = false;
    bool                         showWorkspaceNumbers = false;

    friend class COverviewPassElement;
};

inline std::unique_ptr<COverview> g_pOverview;
