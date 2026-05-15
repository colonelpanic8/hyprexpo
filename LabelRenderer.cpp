#include "LabelRenderer.hpp"

#include <hyprland/src/render/Renderer.hpp>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>

static SP<Render::ITexture> renderLabelTexture(const std::string& text, const CHyprColor& color, int fontSizePx, const std::string& fontFamily, bool bold, bool italic) {
    if (text.empty() || fontSizePx <= 0)
        return nullptr;

    auto         measureSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    auto         measureCairo   = cairo_create(measureSurface);

    PangoLayout* measureLayout = pango_cairo_create_layout(measureCairo);
    pango_layout_set_text(measureLayout, text.c_str(), -1);
    auto* fontDesc = pango_font_description_from_string(fontFamily.empty() ? "Sans" : fontFamily.c_str());
    pango_font_description_set_size(fontDesc, fontSizePx * PANGO_SCALE);
    pango_font_description_set_weight(fontDesc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(fontDesc, italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    pango_layout_set_font_description(measureLayout, fontDesc);
    pango_font_description_free(fontDesc);

    PangoRectangle inkRect, logicalRect;
    pango_layout_get_extents(measureLayout, &inkRect, &logicalRect);

    const int textW = std::max(1, (int)std::ceil(logicalRect.width / (double)PANGO_SCALE));
    const int textH = std::max(1, (int)std::ceil(logicalRect.height / (double)PANGO_SCALE));

    g_object_unref(measureLayout);
    cairo_destroy(measureCairo);
    cairo_surface_destroy(measureSurface);

    const int pad    = std::max(1, (int)std::round(fontSizePx * 0.15));
    const int width  = textW + pad * 2;
    const int height = textH + pad * 2;

    auto      surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto      cairo   = cairo_create(surface);

    cairo_save(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cairo);
    cairo_restore(cairo);

    PangoLayout* layout = pango_cairo_create_layout(cairo);
    pango_layout_set_text(layout, text.c_str(), -1);
    fontDesc = pango_font_description_from_string(fontFamily.empty() ? "Sans" : fontFamily.c_str());
    pango_font_description_set_size(fontDesc, fontSizePx * PANGO_SCALE);
    pango_font_description_set_weight(fontDesc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(fontDesc, italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    pango_layout_set_font_description(layout, fontDesc);
    pango_font_description_free(fontDesc);

    pango_layout_get_extents(layout, &inkRect, &logicalRect);
    const double xOffset = (width - logicalRect.width / (double)PANGO_SCALE) / 2.0;
    const double yOffset = (height - logicalRect.height / (double)PANGO_SCALE) / 2.0;

    cairo_set_source_rgba(cairo, color.r, color.g, color.b, color.a);
    cairo_move_to(cairo, xOffset, yOffset);
    pango_cairo_show_layout(cairo, layout);

    g_object_unref(layout);
    cairo_surface_flush(surface);

    auto texture = g_pHyprRenderer->createTexture(surface);

    cairo_destroy(cairo);
    cairo_surface_destroy(surface);

    return texture;
}

SP<Render::ITexture> ensureLabelTexture(COverview::SWorkspaceImage::SLabelTexture& cached, const std::string& text, uint64_t color, int fontSizePx, const std::string& fontFamily,
                                        bool bold, bool italic) {
    if (cached.tex && cached.tex->m_texID != 0 && cached.text == text && cached.color == color && cached.fontSizePx == fontSizePx && cached.fontFamily == fontFamily &&
        cached.bold == bold && cached.italic == italic)
        return cached.tex;

    cached.text       = text;
    cached.color      = color;
    cached.fontSizePx = fontSizePx;
    cached.fontFamily = fontFamily;
    cached.bold       = bold;
    cached.italic     = italic;
    cached.tex        = renderLabelTexture(text, CHyprColor(color), fontSizePx, fontFamily, bold, italic);
    cached.sizePx     = cached.tex ? cached.tex->m_size : Vector2D{};
    return cached.tex;
}
