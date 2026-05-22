#include "Internals.hpp"
#include "OverviewPassElement.hpp"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>
#include <hyprgraphics/image/Image.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using namespace Hyprutils::String;
using namespace Internals;

namespace {

    struct SWindowIconTexture {
        SP<Render::ITexture> tex;
        Vector2D             sizePx;
        bool                 missing = false;
    };

    struct GObjectDeleter {
        void operator()(gpointer ptr) const {
            if (ptr)
                g_object_unref(ptr);
        }
    };

    struct GKeyFileDeleter {
        void operator()(GKeyFile* ptr) const {
            if (ptr)
                g_key_file_unref(ptr);
        }
    };

    using GDesktopAppInfoPtr = std::unique_ptr<GDesktopAppInfo, GObjectDeleter>;
    using GKeyFilePtr        = std::unique_ptr<GKeyFile, GKeyFileDeleter>;

    std::string trimCopy(std::string value) {
        const auto FIRST = value.find_first_not_of(" \t\r\n");
        if (FIRST == std::string::npos)
            return "";

        const auto LAST = value.find_last_not_of(" \t\r\n");
        return value.substr(FIRST, LAST - FIRST + 1);
    }

    std::vector<std::string> splitColonList(const char* value) {
        std::vector<std::string> result;
        if (!value || !*value)
            return result;

        std::string input = value;
        size_t      start = 0;
        while (start <= input.size()) {
            const size_t end  = input.find(':', start);
            auto         item = trimCopy(input.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (!item.empty())
                result.push_back(item);
            if (end == std::string::npos)
                break;
            start = end + 1;
        }

        return result;
    }

    std::vector<std::filesystem::path> xdgDataDirs() {
        std::vector<std::filesystem::path> dirs;

        if (const auto HOME = std::getenv("HOME")) {
            if (const auto DATAHOME = std::getenv("XDG_DATA_HOME"); DATAHOME && *DATAHOME)
                dirs.emplace_back(DATAHOME);
            else
                dirs.emplace_back(std::filesystem::path(HOME) / ".local/share");
        }

        auto systemDirs = splitColonList(std::getenv("XDG_DATA_DIRS"));
        if (systemDirs.empty())
            systemDirs = {"/usr/local/share", "/usr/share"};

        for (const auto& dir : systemDirs)
            dirs.emplace_back(dir);

        if (const auto HOME = std::getenv("HOME"))
            dirs.emplace_back(std::filesystem::path(HOME) / ".nix-profile/share");

        if (const auto USER = std::getenv("USER"); USER && *USER)
            dirs.emplace_back(std::filesystem::path("/etc/profiles/per-user") / USER / "share");

        dirs.emplace_back("/nix/profile/share");
        dirs.emplace_back("/nix/var/nix/profiles/default/share");
        dirs.emplace_back("/run/current-system/sw/share");

        std::vector<std::filesystem::path> unique;
        std::unordered_set<std::string>    seen;
        for (const auto& dir : dirs) {
            const auto key = dir.lexically_normal().string();
            if (!seen.insert(key).second)
                continue;
            unique.push_back(dir);
        }

        return unique;
    }

    std::optional<std::string> iniField(const std::filesystem::path& path, const std::string& group, const std::string& field) {
        GKeyFilePtr keyFile(g_key_file_new());
        if (!g_key_file_load_from_file(keyFile.get(), path.c_str(), G_KEY_FILE_NONE, nullptr))
            return std::nullopt;

        const auto VALUE = g_key_file_get_string(keyFile.get(), group.c_str(), field.c_str(), nullptr);
        if (!VALUE)
            return std::nullopt;

        std::string result = trimCopy(VALUE);
        g_free(VALUE);
        return result;
    }

    std::optional<std::string> desktopField(const std::filesystem::path& path, const std::string& field) {
        return iniField(path, "Desktop Entry", field);
    }

    std::optional<std::string> iconNameFromDesktopFile(const std::filesystem::path& path) {
        GDesktopAppInfoPtr appInfo(g_desktop_app_info_new_from_filename(path.c_str()));
        if (appInfo) {
            GIcon* icon = g_app_info_get_icon(G_APP_INFO(appInfo.get()));
            if (icon && G_IS_FILE_ICON(icon)) {
                const auto FILE     = g_file_icon_get_file(G_FILE_ICON(icon));
                const auto FILEPATH = FILE ? g_file_get_path(FILE) : nullptr;
                if (FILEPATH) {
                    std::string result = FILEPATH;
                    g_free(FILEPATH);
                    return result;
                }
            }

            if (icon && G_IS_THEMED_ICON(icon)) {
                const auto NAMES = g_themed_icon_get_names(G_THEMED_ICON(icon));
                if (NAMES && NAMES[0] && *NAMES[0])
                    return std::string(NAMES[0]);
            }

            const auto ICONSTRING = icon ? g_icon_to_string(icon) : nullptr;
            if (ICONSTRING) {
                std::string result = ICONSTRING;
                g_free(ICONSTRING);
                if (!result.empty())
                    return result;
            }
        }

        if (const auto ICON = desktopField(path, "Icon"); ICON && !ICON->empty())
            return ICON;

        return std::nullopt;
    }

    std::optional<std::string> startupWMClassFromDesktopFile(const std::filesystem::path& path) {
        GDesktopAppInfoPtr appInfo(g_desktop_app_info_new_from_filename(path.c_str()));
        if (appInfo) {
            const auto STARTUPWMCLASS = g_desktop_app_info_get_startup_wm_class(appInfo.get());
            if (STARTUPWMCLASS && *STARTUPWMCLASS)
                return std::string(STARTUPWMCLASS);
        }

        return desktopField(path, "StartupWMClass");
    }

    std::optional<std::string> steamAppIDForClass(const std::string& classLower) {
        const std::string PREFIX = "steam_app_";
        if (!classLower.starts_with(PREFIX) || classLower.size() == PREFIX.size())
            return std::nullopt;

        return classLower.substr(PREFIX.size());
    }

    std::optional<std::string> iconNameForClass(const std::string& windowClass) {
        static std::unordered_map<std::string, std::optional<std::string>> cache;

        const auto                                                         CLASSLOWER = lowerCopy(windowClass);
        if (CLASSLOWER.empty())
            return std::nullopt;

        if (const auto IT = cache.find(CLASSLOWER); IT != cache.end())
            return IT->second;

        const std::vector<std::string> candidateNames = {
            windowClass + ".desktop",
            CLASSLOWER + ".desktop",
        };

        for (const auto& dataDir : xdgDataDirs()) {
            const auto appDir = dataDir / "applications";
            for (const auto& name : candidateNames) {
                const auto path = appDir / name;
                if (std::filesystem::exists(path)) {
                    cache[CLASSLOWER] = iconNameFromDesktopFile(path);
                    if (cache[CLASSLOWER])
                        return cache[CLASSLOWER];
                }
            }
        }

        for (const auto& dataDir : xdgDataDirs()) {
            const auto appDir = dataDir / "applications";
            if (!std::filesystem::exists(appDir))
                continue;

            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(appDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".desktop")
                    continue;

                const auto basename = lowerCopy(entry.path().stem().string());
                if (basename == CLASSLOWER || basename.ends_with("." + CLASSLOWER)) {
                    cache[CLASSLOWER] = iconNameFromDesktopFile(entry.path());
                    if (cache[CLASSLOWER])
                        return cache[CLASSLOWER];
                }

                const auto STARTUPWMCLASS = startupWMClassFromDesktopFile(entry.path());
                if (STARTUPWMCLASS && lowerCopy(*STARTUPWMCLASS) == CLASSLOWER) {
                    cache[CLASSLOWER] = iconNameFromDesktopFile(entry.path());
                    if (cache[CLASSLOWER])
                        return cache[CLASSLOWER];
                }

                const auto STEAMAPPID = steamAppIDForClass(CLASSLOWER);
                const auto EXEC       = STEAMAPPID ? desktopField(entry.path(), "Exec") : std::nullopt;
                if (EXEC && EXEC->find("steam://rungameid/" + *STEAMAPPID) != std::string::npos) {
                    cache[CLASSLOWER] = iconNameFromDesktopFile(entry.path());
                    if (cache[CLASSLOWER])
                        return cache[CLASSLOWER];
                }
            }
        }

        cache[CLASSLOWER] = std::nullopt;
        return std::nullopt;
    }

    bool supportedIconExtension(const std::filesystem::path& path) {
        const auto EXT = lowerCopy(path.extension().string());
        return EXT == ".png" || EXT == ".svg" || EXT == ".webp" || EXT == ".jpg" || EXT == ".jpeg" || EXT == ".bmp" || EXT == ".jxl" || EXT == ".avif";
    }

    bool iconFileExists(const std::filesystem::path& path) {
        if (!supportedIconExtension(path))
            return false;

        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec))
            return true;

        ec.clear();
        return std::filesystem::is_symlink(path, ec) && std::filesystem::exists(path, ec);
    }

    std::optional<std::string> gtkIconThemeNameFromFile(const std::filesystem::path& path) {
        if (const auto THEME = iniField(path, "Settings", "gtk-icon-theme-name"); THEME && !THEME->empty())
            return THEME;

        std::ifstream file(path);
        if (!file)
            return std::nullopt;

        std::string line;
        while (std::getline(file, line)) {
            line = trimCopy(line);
            if (!line.starts_with("gtk-icon-theme-name"))
                continue;

            const auto EQ = line.find('=');
            if (EQ == std::string::npos)
                continue;

            auto value = trimCopy(line.substr(EQ + 1));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);

            if (!value.empty())
                return value;
        }

        return std::nullopt;
    }

    std::string configuredIconThemeName() {
        if (const auto HOME = std::getenv("HOME")) {
            for (const auto& path : {
                     std::filesystem::path(HOME) / ".config/gtk-3.0/settings.ini",
                     std::filesystem::path(HOME) / ".config/gtk-4.0/settings.ini",
                     std::filesystem::path(HOME) / ".gtkrc-2.0",
                 }) {
                if (const auto THEME = gtkIconThemeNameFromFile(path))
                    return *THEME;
            }
        }

        return "hicolor";
    }

    std::vector<std::string> splitCommaValues(const std::string& value) {
        std::vector<std::string> result;
        size_t                   start = 0;
        while (start <= value.size()) {
            const size_t end  = value.find(',', start);
            auto         item = trimCopy(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (!item.empty())
                result.push_back(item);
            if (end == std::string::npos)
                break;
            start = end + 1;
        }

        return result;
    }

    std::vector<std::filesystem::path> iconThemeDirs(const std::string& theme) {
        std::vector<std::filesystem::path> dirs;

        if (const auto HOME = std::getenv("HOME")) {
            dirs.emplace_back(std::filesystem::path(HOME) / ".icons" / theme);
            dirs.emplace_back(std::filesystem::path(HOME) / ".local/share/icons" / theme);
        }

        for (const auto& dataDir : xdgDataDirs())
            dirs.emplace_back(dataDir / "icons" / theme);

        std::vector<std::filesystem::path> unique;
        std::unordered_set<std::string>    seen;
        for (const auto& dir : dirs) {
            const auto key = dir.lexically_normal().string();
            if (!seen.insert(key).second || !std::filesystem::exists(dir))
                continue;
            unique.push_back(dir);
        }

        return unique;
    }

    void appendIconThemeSearchRoots(std::vector<std::filesystem::path>& roots, const std::string& theme, std::unordered_set<std::string>& seenThemes) {
        if (theme.empty() || !seenThemes.insert(lowerCopy(theme)).second)
            return;

        const auto DIRS = iconThemeDirs(theme);
        for (const auto& dir : DIRS)
            roots.push_back(dir);

        for (const auto& dir : DIRS) {
            const auto index    = dir / "index.theme";
            const auto inherits = iniField(index, "Icon Theme", "Inherits");
            if (!inherits)
                continue;

            for (const auto& inherited : splitCommaValues(*inherits))
                appendIconThemeSearchRoots(roots, inherited, seenThemes);
        }
    }

    int iconPathScore(const std::filesystem::path& path, int targetSize) {
        int        score = 0;

        const auto EXT = lowerCopy(path.extension().string());
        if (EXT == ".svg")
            score += 10000;
        else if (EXT == ".png")
            score += 8000;
        else
            score += 6000;

        for (const auto& part : path) {
            const auto name = lowerCopy(part.string());
            if (name == "apps")
                score += 1000;
            if (name == "scalable" || name == "symbolic")
                score += 700;

            const auto X = name.find('x');
            if (X != std::string::npos) {
                try {
                    const int side = std::stoi(name.substr(0, X));
                    score += std::max(0, 512 - std::abs(side - targetSize));
                } catch (...) { ; }
            }
        }

        return score;
    }

    std::optional<std::filesystem::path> resolveIconPathInRoots(const std::string& iconName, int targetSize, const std::vector<std::filesystem::path>& roots) {
        if (iconName.empty())
            return std::nullopt;

        std::filesystem::path direct(iconName);
        if (direct.is_absolute() && iconFileExists(direct))
            return direct;

        const auto                           ICONPATH   = std::filesystem::path(iconName);
        const bool                           HASICONEXT = supportedIconExtension(ICONPATH);
        const auto                           BASE       = HASICONEXT ? lowerCopy(ICONPATH.stem().string()) : lowerCopy(iconName);
        const auto                           WANT       = HASICONEXT ? lowerCopy(ICONPATH.filename().string()) : "";

        std::optional<std::filesystem::path> best;
        int                                  bestScore = -1;

        std::vector<std::filesystem::path>   searchRoots = roots;
        std::unordered_set<std::string>      seenRoots;
        for (const auto& root : searchRoots)
            seenRoots.insert(root.lexically_normal().string());

        for (const auto& dataDir : roots) {
            const auto pixmaps = dataDir / "pixmaps";
            const auto icons   = dataDir / "icons";
            if (seenRoots.insert(pixmaps.lexically_normal().string()).second)
                searchRoots.emplace_back(pixmaps);
            if (seenRoots.insert(icons.lexically_normal().string()).second)
                searchRoots.emplace_back(icons);
        }

        seenRoots.clear();
        for (const auto& root : searchRoots) {
            const auto rootKey = root.lexically_normal().string();
            if (!seenRoots.insert(rootKey).second || !std::filesystem::exists(root))
                continue;

            std::error_code ec;
            const auto      options = std::filesystem::directory_options::skip_permission_denied | std::filesystem::directory_options::follow_directory_symlink;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root, options, ec)) {
                if (ec) {
                    ec.clear();
                    continue;
                }
                if (!iconFileExists(entry.path()))
                    continue;

                const auto FILENAME = lowerCopy(entry.path().filename().string());
                const auto STEM     = lowerCopy(entry.path().stem().string());
                if ((!WANT.empty() && FILENAME != WANT) || (WANT.empty() && STEM != BASE))
                    continue;

                const int score = iconPathScore(entry.path(), targetSize);
                if (score > bestScore) {
                    best      = entry.path();
                    bestScore = score;
                }
            }
        }

        return best;
    }

    std::optional<std::filesystem::path> resolveIconPath(const std::string& iconName, int targetSize) {
        static std::unordered_map<std::string, std::optional<std::filesystem::path>> cache;

        const auto                                                                   CACHEKEY = lowerCopy(iconName) + "\n" + std::to_string(targetSize);
        if (const auto IT = cache.find(CACHEKEY); IT != cache.end())
            return IT->second;

        std::vector<std::filesystem::path> searchRoots;
        std::unordered_set<std::string>    seenThemes;
        appendIconThemeSearchRoots(searchRoots, configuredIconThemeName(), seenThemes);
        appendIconThemeSearchRoots(searchRoots, "hicolor", seenThemes);

        if (const auto HOME = std::getenv("HOME"))
            searchRoots.emplace_back(std::filesystem::path(HOME) / ".icons");

        for (const auto& dataDir : xdgDataDirs())
            searchRoots.emplace_back(dataDir / "pixmaps");

        for (const auto& dataDir : xdgDataDirs())
            searchRoots.emplace_back(dataDir / "icons");

        cache[CACHEKEY] = resolveIconPathInRoots(iconName, targetSize, searchRoots);
        return cache[CACHEKEY];
    }

    std::vector<std::string> iconNamesForClass(const std::string& windowClass, int sizePx) {
        std::vector<std::string>        names;
        std::unordered_set<std::string> seen;

        auto                            addName = [&](const std::string& name) {
            if (name.empty())
                return;

            const auto key = lowerCopy(name);
            if (!seen.insert(key).second)
                return;

            names.push_back(name);
        };

        if (const auto ICON = iconNameForClass(windowClass))
            addName(*ICON);

        const auto CLASSLOWER = lowerCopy(windowClass);
        if (const auto STEAMAPPID = steamAppIDForClass(CLASSLOWER))
            addName("steam_icon_" + *STEAMAPPID);

        addName(windowClass);
        addName(CLASSLOWER);

        return names;
    }

    SWindowIconTexture& windowIconTextureForClass(const std::string& windowClass, int sizePx) {
        static std::unordered_map<std::string, SWindowIconTexture> cache;

        const auto                                                 CACHEKEY = lowerCopy(windowClass) + "\n" + std::to_string(sizePx);
        auto&                                                      cached   = cache[CACHEKEY];
        if (cached.tex || cached.missing)
            return cached;

        for (const auto& iconName : iconNamesForClass(windowClass, sizePx)) {
            const auto ICONPATH = resolveIconPath(iconName, sizePx);
            if (!ICONPATH)
                continue;

            Hyprgraphics::CImage image(ICONPATH->string(), Vector2D{sizePx, sizePx});
            if (!image.success() || !image.cairoSurface() || !image.cairoSurface()->cairo())
                continue;

            cached.tex    = g_pHyprRenderer->createTexture(image.cairoSurface()->cairo());
            cached.sizePx = cached.tex ? cached.tex->m_size : Vector2D{};
            if (cached.tex && cached.tex->m_texID != 0)
                return cached;

            cached.tex.reset();
            cached.sizePx = {};
        }

        cached.missing = true;
        return cached;
    }

    std::optional<CBox> intersectBoxes(const CBox& a, const CBox& b) {
        const double x1 = std::max(a.x, b.x);
        const double y1 = std::max(a.y, b.y);
        const double x2 = std::min(a.x + a.w, b.x + b.w);
        const double y2 = std::min(a.y + a.h, b.y + b.h);
        if (x2 <= x1 || y2 <= y1)
            return std::nullopt;

        return CBox{x1, y1, x2 - x1, y2 - y1};
    }

    CBox anchoredBox(const CBox& anchor, double width, double height, const std::string& position, double offX, double offY) {
        CBox box = {0, 0, width, height};

        if (position == "top-left") {
            box.x = anchor.x + offX;
            box.y = anchor.y + offY;
        } else if (position == "top-right") {
            box.x = anchor.x + anchor.w - box.w - offX;
            box.y = anchor.y + offY;
        } else if (position == "bottom-left") {
            box.x = anchor.x + offX;
            box.y = anchor.y + anchor.h - box.h - offY;
        } else if (position == "bottom-right") {
            box.x = anchor.x + anchor.w - box.w - offX;
            box.y = anchor.y + anchor.h - box.h - offY;
        } else {
            box.x = anchor.x + (anchor.w - box.w) / 2.0 + offX;
            box.y = anchor.y + (anchor.h - box.h) / 2.0 + offY;
        }

        box.round();
        return box;
    }

}

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
        const auto PREVIOUSWS   = activateWorkspaceForPreview(pMonitor.lock(), PWORKSPACE);
        const auto PREVIEWSTATE = applyWorkspacePreviewState(PWORKSPACE);
        const auto WINDOWSTATE  = PWORKSPACE == startedOn ? std::vector<SWindowPreviewState>{} : applyWorkspaceWindowGoalState(PWORKSPACE);

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace = openSpecial;

        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

        restoreWorkspaceWindowGoalState(WINDOWSTATE);
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
    static const CConfigValue<Config::INTEGER> PWINDOWICONENABLE("plugin:hyprexpo:window_icon_enable");
    static const CConfigValue<Config::STRING>  PWINDOWICONPOS("plugin:hyprexpo:window_icon_position");
    static const CConfigValue<Config::INTEGER> PWINDOWICONSIZE("plugin:hyprexpo:window_icon_size");
    static const CConfigValue<Config::INTEGER> PWINDOWICONOFFX("plugin:hyprexpo:window_icon_offset_x");
    static const CConfigValue<Config::INTEGER> PWINDOWICONOFFY("plugin:hyprexpo:window_icon_offset_y");
    static const CConfigValue<Config::FLOAT>   PWINDOWICONALPHA("plugin:hyprexpo:window_icon_alpha");
    static const CConfigValue<Config::INTEGER> PWINDOWICONBGENABLE("plugin:hyprexpo:window_icon_bg_enable");
    static const CConfigValue<Config::INTEGER> PWINDOWICONBGCOLOR("plugin:hyprexpo:window_icon_bg_color");
    static const CConfigValue<Config::INTEGER> PWINDOWICONBGROUND("plugin:hyprexpo:window_icon_bg_rounding");
    static const CConfigValue<Config::INTEGER> PWINDOWICONBGPAD("plugin:hyprexpo:window_icon_padding");

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

    if (*PWINDOWICONENABLE) {
        const int    iconSizePx = std::max(1, (int)std::lround((double)*PWINDOWICONSIZE * pMonitor->m_scale));
        const double offX       = *PWINDOWICONOFFX * pMonitor->m_scale;
        const double offY       = *PWINDOWICONOFFY * pMonitor->m_scale;
        const double bgPad      = std::max(0, (int)*PWINDOWICONBGPAD) * pMonitor->m_scale;
        const double alpha      = std::clamp((double)*PWINDOWICONALPHA, 0.0, 1.0);

        for (size_t id = 0; id < images.size(); ++id) {
            auto& image = images[id];
            if (image.workspaceID == WORKSPACE_INVALID)
                continue;

            const auto WORKSPACE = image.pWorkspace ? image.pWorkspace : g_pCompositor->getWorkspaceByID(image.workspaceID);
            if (!WORKSPACE)
                continue;

            const CBox&  tileBox = tileBoxes[id];
            const double scaleX  = tileBox.w / pMonitor->m_size.x;
            const double scaleY  = tileBox.h / pMonitor->m_size.y;

            for (const auto& window : g_pCompositor->m_windows) {
                if (!windowVisibleOnWorkspace(window, WORKSPACE))
                    continue;

                auto* icon = &windowIconTextureForClass(window->m_class.empty() ? window->m_initialClass : window->m_class, iconSizePx);
                if ((!icon->tex || icon->tex->m_texID == 0 || icon->missing) && !window->m_initialClass.empty() && window->m_initialClass != window->m_class)
                    icon = &windowIconTextureForClass(window->m_initialClass, iconSizePx);
                if (!icon->tex || icon->tex->m_texID == 0 || icon->missing)
                    continue;

                const auto WINDOWBOX = window->getWindowMainSurfaceBox();
                if (WINDOWBOX.w <= 0 || WINDOWBOX.h <= 0)
                    continue;

                const CBox previewBox = {
                    tileBox.x + (WINDOWBOX.x - pMonitor->m_position.x) * scaleX,
                    tileBox.y + (WINDOWBOX.y - pMonitor->m_position.y) * scaleY,
                    WINDOWBOX.w * scaleX,
                    WINDOWBOX.h * scaleY,
                };

                const auto visibleBox = intersectBoxes(previewBox, tileBox);
                if (!visibleBox)
                    continue;

                const double side    = std::min<double>(iconSizePx, std::max(1.0, std::min(visibleBox->w, visibleBox->h)));
                CBox         iconBox = anchoredBox(*visibleBox, side, side, *PWINDOWICONPOS, offX, offY);

                if (const auto clamped = intersectBoxes(iconBox, tileBox); clamped && (clamped->w < iconBox.w || clamped->h < iconBox.h))
                    iconBox = anchoredBox(*visibleBox, std::min(side, clamped->w), std::min(side, clamped->h), *PWINDOWICONPOS, offX, offY);

                if (*PWINDOWICONBGENABLE) {
                    CBox bg = {iconBox.x - bgPad, iconBox.y - bgPad, iconBox.w + bgPad * 2, iconBox.h + bgPad * 2};
                    bg.round();
                    Render::GL::g_pHyprOpenGL->renderRect(bg, CHyprColor(*PWINDOWICONBGCOLOR),
                                                          {.round = std::max(0, (int)std::lround((double)*PWINDOWICONBGROUND * pMonitor->m_scale))});
                }

                Render::GL::g_pHyprOpenGL->renderTexture(icon->tex, iconBox, {.a = (float)alpha});
            }
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
