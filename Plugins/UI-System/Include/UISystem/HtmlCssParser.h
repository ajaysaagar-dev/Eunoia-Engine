#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <glm/glm.hpp>

#if __has_include(<imgui.h>)
#include <imgui.h>
#define EUNOIA_HAS_IMGUI 1
#else
#define EUNOIA_HAS_IMGUI 0
#endif

namespace EunoiaUI {

// ============================================================================
// CSS COLOR PARSER
// ============================================================================
struct CssColor {
    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static std::string ToLower(const std::string& str) {
        std::string s = str;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    static glm::vec4 Parse(const std::string& rawColor, float defaultAlpha = 1.0f) {
        std::string colorStr = ToLower(Trim(rawColor));
        if (colorStr.empty() || colorStr == "transparent" || colorStr == "none") {
            return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        // 1. Hex Color #RGB, #RGBA, #RRGGBB, #RRGGBBAA
        if (colorStr.front() == '#') {
            std::string hex = colorStr.substr(1);
            try {
                if (hex.size() == 3) {
                    float r = (float)std::stoi(std::string(2, hex[0]), nullptr, 16) / 255.0f;
                    float g = (float)std::stoi(std::string(2, hex[1]), nullptr, 16) / 255.0f;
                    float b = (float)std::stoi(std::string(2, hex[2]), nullptr, 16) / 255.0f;
                    return glm::vec4(r, g, b, defaultAlpha);
                } else if (hex.size() == 4) {
                    float r = (float)std::stoi(std::string(2, hex[0]), nullptr, 16) / 255.0f;
                    float g = (float)std::stoi(std::string(2, hex[1]), nullptr, 16) / 255.0f;
                    float b = (float)std::stoi(std::string(2, hex[2]), nullptr, 16) / 255.0f;
                    float a = (float)std::stoi(std::string(2, hex[3]), nullptr, 16) / 255.0f;
                    return glm::vec4(r, g, b, a);
                } else if (hex.size() == 6) {
                    float r = (float)std::stoi(hex.substr(0, 2), nullptr, 16) / 255.0f;
                    float g = (float)std::stoi(hex.substr(2, 2), nullptr, 16) / 255.0f;
                    float b = (float)std::stoi(hex.substr(4, 2), nullptr, 16) / 255.0f;
                    return glm::vec4(r, g, b, defaultAlpha);
                } else if (hex.size() >= 8) {
                    float r = (float)std::stoi(hex.substr(0, 2), nullptr, 16) / 255.0f;
                    float g = (float)std::stoi(hex.substr(2, 2), nullptr, 16) / 255.0f;
                    float b = (float)std::stoi(hex.substr(4, 2), nullptr, 16) / 255.0f;
                    float a = (float)std::stoi(hex.substr(6, 2), nullptr, 16) / 255.0f;
                    return glm::vec4(r, g, b, a);
                }
            } catch (...) {}
        }

        // 2. rgb(r, g, b) or rgba(r, g, b, a)
        if (colorStr.rfind("rgb", 0) == 0) {
            size_t openParen = colorStr.find('(');
            size_t closeParen = colorStr.find(')');
            if (openParen != std::string::npos && closeParen != std::string::npos && closeParen > openParen) {
                std::string inner = colorStr.substr(openParen + 1, closeParen - openParen - 1);
                for (char& c : inner) { if (c == ',' || c == '/') c = ' '; }
                std::istringstream iss(inner);
                float r = 0.0f, g = 0.0f, b = 0.0f, a = defaultAlpha;
                std::string sr, sg, sb, sa;
                if (iss >> sr >> sg >> sb) {
                    r = ParseColorChannel(sr);
                    g = ParseColorChannel(sg);
                    b = ParseColorChannel(sb);
                    if (iss >> sa) {
                        a = ParseAlphaChannel(sa);
                    }
                    return glm::vec4(r, g, b, a);
                }
            }
        }

        // 3. hsl(h, s, l) or hsla(h, s, l, a)
        if (colorStr.rfind("hsl", 0) == 0) {
            size_t openParen = colorStr.find('(');
            size_t closeParen = colorStr.find(')');
            if (openParen != std::string::npos && closeParen != std::string::npos && closeParen > openParen) {
                std::string inner = colorStr.substr(openParen + 1, closeParen - openParen - 1);
                for (char& c : inner) { if (c == ',' || c == '/') c = ' '; }
                std::istringstream iss(inner);
                float h = 0.0f, s = 0.0f, l = 0.0f, a = defaultAlpha;
                std::string sh, ss, sl, sa;
                if (iss >> sh >> ss >> sl) {
                    try { h = std::stof(sh); } catch (...) {}
                    try {
                        if (ss.back() == '%') ss.pop_back();
                        s = std::stof(ss) / 100.0f;
                    } catch (...) {}
                    try {
                        if (sl.back() == '%') sl.pop_back();
                        l = std::stof(sl) / 100.0f;
                    } catch (...) {}
                    if (iss >> sa) {
                        a = ParseAlphaChannel(sa);
                    }
                    return HslToRgb(h, s, l, a);
                }
            }
        }

        // 4. Standard CSS Named Colors
        static const std::unordered_map<std::string, glm::vec3> s_namedColors = {
            { "white", { 1.0f, 1.0f, 1.0f } },
            { "black", { 0.0f, 0.0f, 0.0f } },
            { "red", { 1.0f, 0.0f, 0.0f } },
            { "green", { 0.0f, 0.50f, 0.0f } },
            { "blue", { 0.0f, 0.0f, 1.0f } },
            { "yellow", { 1.0f, 1.0f, 0.0f } },
            { "cyan", { 0.0f, 1.0f, 1.0f } },
            { "magenta", { 1.0f, 0.0f, 1.0f } },
            { "gray", { 0.50f, 0.50f, 0.50f } },
            { "grey", { 0.50f, 0.50f, 0.50f } },
            { "darkgray", { 0.25f, 0.25f, 0.25f } },
            { "darkgrey", { 0.25f, 0.25f, 0.25f } },
            { "lightgray", { 0.83f, 0.83f, 0.83f } },
            { "lightgrey", { 0.83f, 0.83f, 0.83f } },
            { "silver", { 0.75f, 0.75f, 0.75f } },
            { "orange", { 1.0f, 0.65f, 0.0f } },
            { "purple", { 0.50f, 0.0f, 0.50f } },
            { "pink", { 1.0f, 0.75f, 0.80f } },
            { "brown", { 0.65f, 0.16f, 0.16f } },
            { "gold", { 1.0f, 0.84f, 0.0f } },
            { "navy", { 0.0f, 0.0f, 0.50f } },
            { "teal", { 0.0f, 0.50f, 0.50f } },
            { "lime", { 0.0f, 1.0f, 0.0f } },
            { "aqua", { 0.0f, 1.0f, 1.0f } },
            { "fuchsia", { 1.0f, 0.0f, 1.0f } },
            { "maroon", { 0.50f, 0.0f, 0.0f } },
            { "olive", { 0.50f, 0.50f, 0.0f } },
            { "indigo", { 0.29f, 0.0f, 0.51f } },
            { "violet", { 0.93f, 0.51f, 0.93f } },
            { "crimson", { 0.86f, 0.08f, 0.24f } },
            { "coral", { 1.0f, 0.50f, 0.31f } },
            { "khaki", { 0.94f, 0.90f, 0.55f } },
            { "plum", { 0.87f, 0.63f, 0.87f } },
            { "azure", { 0.94f, 1.0f, 1.0f } },
            { "ivory", { 1.0f, 1.0f, 0.94f } },
            { "snow", { 1.0f, 0.98f, 0.98f } },
            { "aliceblue", { 0.94f, 0.97f, 1.0f } },
            { "ghostwhite", { 0.97f, 0.97f, 1.0f } },
            { "whitesmoke", { 0.96f, 0.96f, 0.96f } },
            { "seashell", { 1.0f, 0.96f, 0.93f } },
            { "beige", { 0.96f, 0.96f, 0.86f } },
            { "oldlace", { 0.99f, 0.96f, 0.90f } },
            { "floralwhite", { 1.0f, 0.98f, 0.94f } },
            { "cornflowerblue", { 0.39f, 0.58f, 0.93f } },
            { "deepskyblue", { 0.0f, 0.75f, 1.0f } },
            { "dodgerblue", { 0.12f, 0.56f, 1.0f } },
            { "lightblue", { 0.68f, 0.85f, 0.90f } },
            { "midnightblue", { 0.10f, 0.10f, 0.44f } },
            { "royalblue", { 0.25f, 0.41f, 0.88f } },
            { "steelblue", { 0.27f, 0.51f, 0.71f } },
            { "darkcyan", { 0.0f, 0.55f, 0.55f } },
            { "lightcyan", { 0.88f, 1.0f, 1.0f } },
            { "darkgreen", { 0.0f, 0.39f, 0.0f } },
            { "forestgreen", { 0.13f, 0.55f, 0.13f } },
            { "lightgreen", { 0.56f, 0.93f, 0.56f } },
            { "seagreen", { 0.18f, 0.54f, 0.34f } },
            { "springgreen", { 0.0f, 1.0f, 0.50f } },
            { "darkorange", { 1.0f, 0.55f, 0.0f } },
            { "orangered", { 1.0f, 0.27f, 0.0f } },
            { "tomato", { 1.0f, 0.39f, 0.28f } },
            { "darkred", { 0.55f, 0.0f, 0.0f } },
            { "hotpink", { 1.0f, 0.41f, 0.71f } },
            { "deeppink", { 1.0f, 0.08f, 0.58f } },
            { "lightpink", { 1.0f, 0.71f, 0.76f } },
            { "darkviolet", { 0.58f, 0.0f, 0.83f } },
            { "mediumpurple", { 0.58f, 0.44f, 0.86f } },
            { "slate", { 0.12f, 0.16f, 0.23f } },
            { "zinc", { 0.10f, 0.10f, 0.12f } }
        };

        auto it = s_namedColors.find(colorStr);
        if (it != s_namedColors.end()) {
            return glm::vec4(it->second, defaultAlpha);
        }

        return glm::vec4(1.0f, 1.0f, 1.0f, defaultAlpha);
    }

private:
    static float ParseColorChannel(std::string str) {
        str = Trim(str);
        if (str.empty()) return 0.0f;
        try {
            if (str.back() == '%') {
                str.pop_back();
                return std::clamp(std::stof(str) / 100.0f, 0.0f, 1.0f);
            }
            float val = std::stof(str);
            if (val > 1.0f) return std::clamp(val / 255.0f, 0.0f, 1.0f);
            return std::clamp(val, 0.0f, 1.0f);
        } catch (...) {
            return 0.0f;
        }
    }

    static float ParseAlphaChannel(std::string str) {
        str = Trim(str);
        if (str.empty()) return 1.0f;
        try {
            if (str.back() == '%') {
                str.pop_back();
                return std::clamp(std::stof(str) / 100.0f, 0.0f, 1.0f);
            }
            float val = std::stof(str);
            if (val > 1.0f) return std::clamp(val / 255.0f, 0.0f, 1.0f);
            return std::clamp(val, 0.0f, 1.0f);
        } catch (...) {
            return 1.0f;
        }
    }

    static glm::vec4 HslToRgb(float h, float s, float l, float a) {
        while (h < 0.0f) h += 360.0f;
        while (h >= 360.0f) h -= 360.0f;
        float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
        float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
        float m = l - c / 2.0f;
        float r = 0, g = 0, b = 0;
        if (h < 60) { r = c; g = x; b = 0; }
        else if (h < 120) { r = x; g = c; b = 0; }
        else if (h < 180) { r = 0; g = c; b = x; }
        else if (h < 240) { r = 0; g = x; b = c; }
        else if (h < 300) { r = x; g = 0; b = c; }
        else { r = c; g = 0; b = x; }
        return glm::vec4(r + m, g + m, b + m, a);
    }
};

// ============================================================================
// CSS STYLE ATTRIBUTES & PARSER
// ============================================================================
struct CssStyle {
    // Colors & Borders
    glm::vec4 backgroundColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    glm::vec4 borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    float borderThickness = 0.0f;
    float cornerRounding = 0.0f;

    // Typography
    glm::vec4 textColor{ 0.95f, 0.95f, 0.95f, 1.0f };
    float fontSize = 16.0f;
    int fontWeight = 400; // 400=normal, 600=semibold, 700=bold
    int textAlignment = 0; // 0=left, 1=center, 2=right, 3=justify
    int textTransform = 0; // 0=none, 1=uppercase, 2=lowercase, 3=capitalize
    float letterSpacing = 0.0f;
    float lineHeight = 1.2f;

    // Box Model: Padding & Margin
    float paddingLeft = 0.0f, paddingRight = 0.0f, paddingTop = 0.0f, paddingBottom = 0.0f;
    float marginLeft = 0.0f, marginRight = 0.0f, marginTop = 0.0f, marginBottom = 0.0f;
    bool isMarginAutoX = false;
    bool isMarginAutoY = false;

    // Dimensions
    float width = -1.0f, height = -1.0f; // -1 = auto
    bool isWidthPercent = false, isHeightPercent = false;
    float widthPercent = 0.0f, heightPercent = 0.0f;
    float minWidth = 0.0f, minHeight = 0.0f;
    float maxWidth = 999999.0f, maxHeight = 999999.0f;

    // Display & Flexbox
    bool isFlex = false;
    bool isFlexCol = false;
    bool isFlexWrap = false;
    int justifyContent = 0; // 0=start, 1=center, 2=end, 3=between, 4=around, 5=evenly
    int alignItems = 0;     // 0=start, 1=center, 2=end, 3=stretch
    float gap = 0.0f;
    float rowGap = 0.0f;
    float colGap = 0.0f;

    // Box Shadow
    bool hasShadow = false;
    glm::vec4 shadowColor{ 0.0f, 0.0f, 0.0f, 0.45f };
    glm::vec2 shadowOffset{ 0.0f, 4.0f };
    float shadowBlur = 8.0f;

    // Positioning
    bool isAbsolute = false;
    bool isFixed = false;
    float top = 0.0f, left = 0.0f, right = 0.0f, bottom = 0.0f;
    bool hasTop = false, hasLeft = false, hasRight = false, hasBottom = false;

    // Interactivity & Cursor
    std::string cursor = "default"; // "pointer", "default", "text"
    std::string overflow = "visible"; // "hidden", "visible", "auto", "scroll"
    float opacity = 1.0f;
    int zIndex = 0;
    bool isButton = false;
    bool isHidden = false;
    glm::vec4 hoverBgColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    bool hasHoverBg = false;

    static float ParseUnit(const std::string& valStr, float refSize = 1080.0f) {
        std::string s = CssColor::Trim(valStr);
        if (s.empty() || s == "auto") return 0.0f;
        try {
            if (s.rfind("px") != std::string::npos) {
                return std::stof(s.substr(0, s.find("px")));
            }
            if (s.rfind("rem") != std::string::npos) {
                return std::stof(s.substr(0, s.find("rem"))) * 16.0f;
            }
            if (s.rfind("em") != std::string::npos) {
                return std::stof(s.substr(0, s.find("em"))) * 16.0f;
            }
            if (s.rfind("pt") != std::string::npos) {
                return std::stof(s.substr(0, s.find("pt"))) * 1.333f;
            }
            if (s.rfind("vw") != std::string::npos) {
                return (std::stof(s.substr(0, s.find("vw"))) / 100.0f) * 1920.0f;
            }
            if (s.rfind("vh") != std::string::npos) {
                return (std::stof(s.substr(0, s.find("vh"))) / 100.0f) * 1080.0f;
            }
            if (s.back() == '%') {
                return (std::stof(s.substr(0, s.size() - 1)) / 100.0f) * refSize;
            }
            return std::stof(s);
        } catch (...) {
            return 0.0f;
        }
    }

    void ApplyProperty(const std::string& rawProp, const std::string& rawVal) {
        std::string prop = CssColor::ToLower(CssColor::Trim(rawProp));
        std::string val = CssColor::Trim(rawVal);
        if (prop.empty() || val.empty()) return;

        // Position
        if (prop == "position") {
            std::string v = CssColor::ToLower(val);
            if (v == "absolute") { isAbsolute = true; isFixed = false; }
            else if (v == "fixed") { isAbsolute = true; isFixed = true; }
            else if (v == "relative" || v == "static") { isAbsolute = false; isFixed = false; }
        } else if (prop == "top") {
            top = ParseUnit(val, 1080.0f);
            hasTop = true;
        } else if (prop == "bottom") {
            bottom = ParseUnit(val, 1080.0f);
            hasBottom = true;
        } else if (prop == "left") {
            left = ParseUnit(val, 1920.0f);
            hasLeft = true;
        } else if (prop == "right") {
            right = ParseUnit(val, 1920.0f);
            hasRight = true;
        }

        // Dimensions
        else if (prop == "width") {
            std::string v = CssColor::ToLower(val);
            if (v == "auto") { width = -1.0f; isWidthPercent = false; }
            else if (v == "100%" || v == "100vw") { isWidthPercent = true; widthPercent = 1.0f; }
            else if (v.back() == '%') {
                try { isWidthPercent = true; widthPercent = std::stof(v.substr(0, v.size() - 1)) / 100.0f; } catch (...) {}
            } else {
                width = ParseUnit(val, 1920.0f);
                isWidthPercent = false;
            }
        } else if (prop == "height") {
            std::string v = CssColor::ToLower(val);
            if (v == "auto") { height = -1.0f; isHeightPercent = false; }
            else if (v == "100%" || v == "100vh") { isHeightPercent = true; heightPercent = 1.0f; }
            else if (v.back() == '%') {
                try { isHeightPercent = true; heightPercent = std::stof(v.substr(0, v.size() - 1)) / 100.0f; } catch (...) {}
            } else {
                height = ParseUnit(val, 1080.0f);
                isHeightPercent = false;
            }
        } else if (prop == "min-width") {
            minWidth = ParseUnit(val, 1920.0f);
        } else if (prop == "max-width") {
            maxWidth = ParseUnit(val, 1920.0f);
        } else if (prop == "min-height") {
            minHeight = ParseUnit(val, 1080.0f);
        } else if (prop == "max-height") {
            maxHeight = ParseUnit(val, 1080.0f);
        }

        // Colors & Background
        else if (prop == "color") {
            textColor = CssColor::Parse(val);
        } else if (prop == "background" || prop == "background-color") {
            backgroundColor = CssColor::Parse(val);
        }

        // Typography
        else if (prop == "font-size") {
            fontSize = ParseUnit(val, 16.0f);
            if (fontSize <= 0.0f) fontSize = 16.0f;
        } else if (prop == "font-weight") {
            std::string v = CssColor::ToLower(val);
            if (v == "bold" || v == "700" || v == "800" || v == "900") fontWeight = 700;
            else if (v == "semibold" || v == "600") fontWeight = 600;
            else if (v == "medium" || v == "500") fontWeight = 500;
            else if (v == "normal" || v == "400") fontWeight = 400;
            else if (v == "light" || v == "300") fontWeight = 300;
        } else if (prop == "text-align") {
            std::string v = CssColor::ToLower(val);
            if (v == "center") textAlignment = 1;
            else if (v == "right") textAlignment = 2;
            else if (v == "justify") textAlignment = 3;
            else textAlignment = 0;
        } else if (prop == "text-transform") {
            std::string v = CssColor::ToLower(val);
            if (v == "uppercase") textTransform = 1;
            else if (v == "lowercase") textTransform = 2;
            else if (v == "capitalize") textTransform = 3;
            else textTransform = 0;
        } else if (prop == "letter-spacing") {
            letterSpacing = ParseUnit(val, 16.0f);
        } else if (prop == "line-height") {
            try {
                if (val.back() == '%') {
                    lineHeight = std::stof(val.substr(0, val.size() - 1)) / 100.0f;
                } else if (val.find("px") != std::string::npos) {
                    lineHeight = std::stof(val.substr(0, val.find("px"))) / std::max(1.0f, fontSize);
                } else {
                    lineHeight = std::stof(val);
                }
            } catch (...) {}
        }

        // Padding
        else if (prop == "padding") {
            ParseFourSides(val, paddingTop, paddingRight, paddingBottom, paddingLeft);
        } else if (prop == "padding-left") { paddingLeft = ParseUnit(val); }
        else if (prop == "padding-right") { paddingRight = ParseUnit(val); }
        else if (prop == "padding-top") { paddingTop = ParseUnit(val); }
        else if (prop == "padding-bottom") { paddingBottom = ParseUnit(val); }

        // Margin
        else if (prop == "margin") {
            ParseMarginFourSides(val, marginTop, marginRight, marginBottom, marginLeft, isMarginAutoX, isMarginAutoY);
        } else if (prop == "margin-left") {
            if (CssColor::ToLower(val) == "auto") isMarginAutoX = true;
            else marginLeft = ParseUnit(val);
        } else if (prop == "margin-right") {
            if (CssColor::ToLower(val) == "auto") isMarginAutoX = true;
            else marginRight = ParseUnit(val);
        } else if (prop == "margin-top") {
            if (CssColor::ToLower(val) == "auto") isMarginAutoY = true;
            else marginTop = ParseUnit(val);
        } else if (prop == "margin-bottom") {
            if (CssColor::ToLower(val) == "auto") isMarginAutoY = true;
            else marginBottom = ParseUnit(val);
        }

        // Borders & Radius
        else if (prop == "border") {
            ParseBorderShorthand(val);
        } else if (prop == "border-width") {
            borderThickness = ParseUnit(val);
        } else if (prop == "border-color") {
            borderColor = CssColor::Parse(val);
            if (borderThickness == 0.0f) borderThickness = 1.0f;
        } else if (prop == "border-radius") {
            cornerRounding = ParseUnit(val);
        }

        // Display & Flexbox
        else if (prop == "display") {
            std::string v = CssColor::ToLower(val);
            if (v == "flex") isFlex = true;
            else if (v == "none") isHidden = true;
            else isFlex = false;
        } else if (prop == "flex-direction") {
            std::string v = CssColor::ToLower(val);
            if (v == "column" || v == "column-reverse") isFlexCol = true;
            else isFlexCol = false;
        } else if (prop == "justify-content") {
            std::string v = CssColor::ToLower(val);
            if (v == "center") justifyContent = 1;
            else if (v == "flex-end" || v == "end") justifyContent = 2;
            else if (v == "space-between") justifyContent = 3;
            else if (v == "space-around") justifyContent = 4;
            else if (v == "space-evenly") justifyContent = 5;
            else justifyContent = 0;
        } else if (prop == "align-items") {
            std::string v = CssColor::ToLower(val);
            if (v == "center") alignItems = 1;
            else if (v == "flex-end" || v == "end") alignItems = 2;
            else if (v == "stretch") alignItems = 3;
            else alignItems = 0;
        } else if (prop == "gap") {
            gap = ParseUnit(val);
        } else if (prop == "row-gap") {
            rowGap = ParseUnit(val);
        } else if (prop == "column-gap") {
            colGap = ParseUnit(val);
        }

        // Shadow & Overflow & Cursor
        else if (prop == "box-shadow") {
            ParseBoxShadow(val);
        } else if (prop == "overflow" || prop == "overflow-y" || prop == "overflow-x") {
            overflow = CssColor::ToLower(val);
        } else if (prop == "cursor") {
            cursor = CssColor::ToLower(val);
        }

        // Opacity & z-index
        else if (prop == "opacity") {
            try { opacity = std::clamp(std::stof(val), 0.0f, 1.0f); } catch (...) {}
        } else if (prop == "z-index") {
            try { zIndex = std::stoi(val); } catch (...) {}
        }
    }

    void ApplyCssBlock(const std::string& cssDeclarations) {
        std::istringstream iss(cssDeclarations);
        std::string statement;
        while (std::getline(iss, statement, ';')) {
            size_t colon = statement.find(':');
            if (colon != std::string::npos) {
                std::string p = statement.substr(0, colon);
                std::string v = statement.substr(colon + 1);
                ApplyProperty(p, v);
            }
        }
    }

private:
    static void ParseFourSides(const std::string& str, float& top, float& right, float& bottom, float& left) {
        std::istringstream iss(str);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) tokens.push_back(token);

        if (tokens.size() == 1) {
            top = right = bottom = left = ParseUnit(tokens[0]);
        } else if (tokens.size() == 2) {
            top = bottom = ParseUnit(tokens[0]);
            right = left = ParseUnit(tokens[1]);
        } else if (tokens.size() == 3) {
            top = ParseUnit(tokens[0]);
            right = left = ParseUnit(tokens[1]);
            bottom = ParseUnit(tokens[2]);
        } else if (tokens.size() >= 4) {
            top = ParseUnit(tokens[0]);
            right = ParseUnit(tokens[1]);
            bottom = ParseUnit(tokens[2]);
            left = ParseUnit(tokens[3]);
        }
    }

    static void ParseMarginFourSides(const std::string& str, float& top, float& right, float& bottom, float& left, bool& autoX, bool& autoY) {
        std::istringstream iss(str);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) tokens.push_back(token);

        if (tokens.size() == 1) {
            if (CssColor::ToLower(tokens[0]) == "auto") { autoX = autoY = true; }
            else { top = right = bottom = left = ParseUnit(tokens[0]); }
        } else if (tokens.size() == 2) {
            if (CssColor::ToLower(tokens[0]) == "auto") autoY = true; else top = bottom = ParseUnit(tokens[0]);
            if (CssColor::ToLower(tokens[1]) == "auto") autoX = true; else right = left = ParseUnit(tokens[1]);
        } else if (tokens.size() >= 4) {
            if (CssColor::ToLower(tokens[0]) == "auto") autoY = true; else top = ParseUnit(tokens[0]);
            if (CssColor::ToLower(tokens[1]) == "auto") autoX = true; else right = ParseUnit(tokens[1]);
            if (CssColor::ToLower(tokens[2]) == "auto") autoY = true; else bottom = ParseUnit(tokens[2]);
            if (CssColor::ToLower(tokens[3]) == "auto") autoX = true; else left = ParseUnit(tokens[3]);
        }
    }

    void ParseBorderShorthand(const std::string& val) {
        std::istringstream iss(val);
        std::string token;
        while (iss >> token) {
            std::string tLower = CssColor::ToLower(token);
            if (tLower == "none" || tLower == "hidden") {
                borderThickness = 0.0f;
            } else if (tLower == "solid" || tLower == "dashed" || tLower == "dotted") {
                if (borderThickness == 0.0f) borderThickness = 1.0f;
            } else if (std::isdigit(token[0]) || token[0] == '.') {
                borderThickness = ParseUnit(token);
            } else {
                borderColor = CssColor::Parse(token);
            }
        }
        if (borderThickness == 0.0f) borderThickness = 1.0f;
    }

    void ParseBoxShadow(const std::string& val) {
        std::string v = CssColor::Trim(val);
        if (v == "none" || v.empty()) {
            hasShadow = false;
            return;
        }
        hasShadow = true;

        std::string colorPart;
        std::string dimPart;
        size_t rgbaPos = v.find("rgb");
        if (rgbaPos != std::string::npos) {
            colorPart = v.substr(rgbaPos);
            dimPart = v.substr(0, rgbaPos);
        } else if (v.find('#') != std::string::npos) {
            size_t hashPos = v.find('#');
            colorPart = v.substr(hashPos);
            dimPart = v.substr(0, hashPos);
        } else {
            dimPart = v;
        }

        std::istringstream iss(dimPart);
        std::vector<std::string> tokens;
        std::string t;
        while (iss >> t) tokens.push_back(t);

        if (tokens.size() >= 2) {
            shadowOffset.x = ParseUnit(tokens[0]);
            shadowOffset.y = ParseUnit(tokens[1]);
        }
        if (tokens.size() >= 3) {
            shadowBlur = ParseUnit(tokens[2]);
        }
        if (!colorPart.empty()) {
            shadowColor = CssColor::Parse(colorPart);
        }
    }
};

// ============================================================================
// CSS STYLESHEET (RULES MAP WITH PSEUDO-CLASSES)
// ============================================================================
class CssStyleSheet {
public:
    std::unordered_map<std::string, std::string> rules;       // selector -> css string
    std::unordered_map<std::string, std::string> hoverRules;  // selector without ':hover' -> css string
    std::unordered_map<std::string, std::string> activeRules; // selector without ':active' -> css string

    void ParseCss(const std::string& cssSource) {
        size_t pos = 0;
        size_t len = cssSource.size();

        while (pos < len) {
            // Skip comments /* ... */
            if (pos + 1 < len && cssSource[pos] == '/' && cssSource[pos + 1] == '*') {
                size_t endComment = cssSource.find("*/", pos + 2);
                if (endComment != std::string::npos) pos = endComment + 2;
                else break;
                continue;
            }

            size_t openBrace = cssSource.find('{', pos);
            if (openBrace == std::string::npos) break;

            std::string selectorsStr = cssSource.substr(pos, openBrace - pos);
            size_t closeBrace = cssSource.find('}', openBrace);
            if (closeBrace == std::string::npos) break;

            std::string declarations = cssSource.substr(openBrace + 1, closeBrace - openBrace - 1);
            pos = closeBrace + 1;

            // Split multiple selectors e.g. "h1, .title, #header"
            std::istringstream selStream(selectorsStr);
            std::string singleSel;
            while (std::getline(selStream, singleSel, ',')) {
                singleSel = CssColor::Trim(singleSel);
                if (singleSel.empty()) continue;

                if (singleSel.rfind(":hover") != std::string::npos) {
                    std::string base = singleSel.substr(0, singleSel.find(":hover"));
                    hoverRules[base] += " " + declarations;
                } else if (singleSel.rfind(":active") != std::string::npos) {
                    std::string base = singleSel.substr(0, singleSel.find(":active"));
                    activeRules[base] += " " + declarations;
                } else {
                    rules[singleSel] += " " + declarations;
                }
            }
        }
    }

    void ApplyToNode(const std::string& tag, const std::string& id, const std::vector<std::string>& classList, CssStyle& outStyle) const {
        auto itUniv = rules.find("*");
        if (itUniv != rules.end()) outStyle.ApplyCssBlock(itUniv->second);

        std::string tagLower = CssColor::ToLower(tag);
        auto itTag = rules.find(tagLower);
        if (itTag != rules.end()) outStyle.ApplyCssBlock(itTag->second);

        for (const auto& cls : classList) {
            std::string clsQuery = "." + cls;
            auto itCls = rules.find(clsQuery);
            if (itCls != rules.end()) outStyle.ApplyCssBlock(itCls->second);
        }

        if (!id.empty()) {
            std::string idQuery = "#" + id;
            auto itId = rules.find(idQuery);
            if (itId != rules.end()) outStyle.ApplyCssBlock(itId->second);
        }
    }

    void ApplyHoverToNode(const std::string& tag, const std::string& id, const std::vector<std::string>& classList, CssStyle& outStyle) const {
        std::string tagLower = CssColor::ToLower(tag);
        auto itTag = hoverRules.find(tagLower);
        if (itTag != hoverRules.end()) outStyle.ApplyCssBlock(itTag->second);

        for (const auto& cls : classList) {
            std::string clsQuery = "." + cls;
            auto itCls = hoverRules.find(clsQuery);
            if (itCls != hoverRules.end()) outStyle.ApplyCssBlock(itCls->second);
        }

        if (!id.empty()) {
            std::string idQuery = "#" + id;
            auto itId = hoverRules.find(idQuery);
            if (itId != hoverRules.end()) outStyle.ApplyCssBlock(itId->second);
        }
    }

    void ApplyActiveToNode(const std::string& tag, const std::string& id, const std::vector<std::string>& classList, CssStyle& outStyle) const {
        std::string tagLower = CssColor::ToLower(tag);
        auto itTag = activeRules.find(tagLower);
        if (itTag != activeRules.end()) outStyle.ApplyCssBlock(itTag->second);

        for (const auto& cls : classList) {
            std::string clsQuery = "." + cls;
            auto itCls = activeRules.find(clsQuery);
            if (itCls != activeRules.end()) outStyle.ApplyCssBlock(itCls->second);
        }

        if (!id.empty()) {
            std::string idQuery = "#" + id;
            auto itId = activeRules.find(idQuery);
            if (itId != activeRules.end()) outStyle.ApplyCssBlock(itId->second);
        }
    }
};

// ============================================================================
// HTML DOM NODE WITH FULL INTERACTIVITY & CSS ENGINE
// ============================================================================
class HtmlNode {
public:
    std::string tag = "div";
    std::string id = "";
    std::string classListStr = "";
    std::vector<std::string> classes;
    std::string textContent = "";
    std::unordered_map<std::string, std::string> attributes;
    CssStyle style;
    std::vector<std::shared_ptr<HtmlNode>> children;
    HtmlNode* parent = nullptr;
    glm::vec4 computedRect{ 0.0f, 0.0f, 0.0f, 0.0f }; // x, y, width, height

    // Interactivity State
    bool isHovered = false;
    bool isPressed = false;
    bool isClicked = false;
    float scrollY = 0.0f;
    float maxScrollY = 0.0f;
    float totalContentHeight = 0.0f;
    std::function<void()> onClick = nullptr;

    void ParseClasses(const std::string& clsStr) {
        classListStr = clsStr;
        classes.clear();
        std::istringstream iss(clsStr);
        std::string cls;
        while (iss >> cls) {
            classes.push_back(cls);
        }
    }

    void ApplyCascade(const CssStyleSheet& styleSheet, const std::string& inlineStyle) {
        if (tag == "h1") { style.fontSize = 32.0f; style.fontWeight = 700; }
        else if (tag == "h2") { style.fontSize = 24.0f; style.fontWeight = 700; }
        else if (tag == "h3") { style.fontSize = 20.0f; style.fontWeight = 600; }
        else if (tag == "p" || tag == "span" || tag == "label") { style.fontSize = 16.0f; }
        else if (tag == "button" || tag == "a") {
            style.isButton = true;
            style.cursor = "pointer";
            if (tag == "button") {
                style.backgroundColor = glm::vec4(0.08f, 0.45f, 0.75f, 1.0f);
                style.textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                style.cornerRounding = 8.0f;
                style.paddingTop = style.paddingBottom = 8.0f;
                style.paddingLeft = style.paddingRight = 16.0f;
            }
        }

        styleSheet.ApplyToNode(tag, id, classes, style);

        if (!inlineStyle.empty()) {
            style.ApplyCssBlock(inlineStyle);
        }
    }

    void ComputeLayout(const glm::vec4& parentRect) {
        if (style.isHidden) {
            computedRect = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            return;
        }

        float pW = parentRect.z;
        float pH = parentRect.w;

        // Compute Width
        float elemW = 0.0f;
        if (style.isWidthPercent) {
            elemW = pW * style.widthPercent;
        } else if (style.width > 0.0f) {
            elemW = style.width;
        } else {
            if (tag == "div" || tag == "section" || tag == "main" || tag == "header" || tag == "footer" || tag == "body" || style.isFlex) {
                elemW = pW;
            } else if (tag == "button") {
                elemW = std::max(100.0f, (float)textContent.length() * (style.fontSize * 0.60f) + style.paddingLeft + style.paddingRight + 20.0f);
            } else {
                elemW = (float)textContent.length() * (style.fontSize * 0.58f) + style.paddingLeft + style.paddingRight;
            }
        }
        elemW = std::clamp(elemW, style.minWidth, style.maxWidth);

        // Compute Height
        float elemH = 0.0f;
        if (style.isHeightPercent) {
            elemH = pH * style.heightPercent;
        } else if (style.height > 0.0f) {
            elemH = style.height;
        } else {
            if (tag == "h1") elemH = style.fontSize * 1.35f + style.paddingTop + style.paddingBottom;
            else if (tag == "h2") elemH = style.fontSize * 1.30f + style.paddingTop + style.paddingBottom;
            else if (tag == "p" || tag == "span" || tag == "label") elemH = style.fontSize * 1.25f + style.paddingTop + style.paddingBottom;
            else if (tag == "button") elemH = style.fontSize + style.paddingTop + style.paddingBottom + 12.0f;
            else if (tag == "hr") elemH = 2.0f + style.marginTop + style.marginBottom;
            else if (tag == "body") elemH = pH;
            else elemH = 32.0f;
        }
        elemH = std::clamp(elemH, style.minHeight, style.maxHeight);

        // Compute Position
        float elemX = parentRect.x + style.marginLeft;
        float elemY = parentRect.y + style.marginTop;

        if (style.isAbsolute) {
            if (style.hasLeft) {
                elemX = parentRect.x + style.left;
                if (style.hasRight) {
                    elemW = std::max(0.0f, parentRect.z - style.left - style.right);
                }
            } else if (style.hasRight) {
                elemX = parentRect.x + parentRect.z - style.right - elemW;
            }

            if (style.hasTop) {
                elemY = parentRect.y + style.top;
                if (style.hasBottom) {
                    elemH = std::max(0.0f, parentRect.w - style.top - style.bottom);
                }
            } else if (style.hasBottom) {
                elemY = parentRect.y + parentRect.w - style.bottom - elemH;
            }
        } else {
            if (style.isMarginAutoX) {
                elemX = parentRect.x + std::max(0.0f, (pW - elemW) * 0.5f);
            }
            if (style.isMarginAutoY) {
                elemY = parentRect.y + std::max(0.0f, (pH - elemH) * 0.5f);
            }
        }

        computedRect = glm::vec4(elemX, elemY, elemW, elemH);

        if (children.empty()) return;

        glm::vec4 contentRect(
            elemX + style.paddingLeft,
            elemY + style.paddingTop - scrollY,
            std::max(0.0f, elemW - style.paddingLeft - style.paddingRight),
            std::max(0.0f, elemH - style.paddingTop - style.paddingBottom)
        );

        float totalChildrenW = 0.0f;
        float totalChildrenH = 0.0f;
        for (const auto& ch : children) {
            ch->ComputeLayout(contentRect);
            if (!ch->style.isAbsolute) {
                totalChildrenW += ch->computedRect.z;
                totalChildrenH += ch->computedRect.w;
            }
        }

        if (style.isFlex) {
            float effGap = (style.gap > 0.0f) ? style.gap : 0.0f;

            if (style.isFlexCol) {
                float cGap = (style.rowGap > 0.0f) ? style.rowGap : effGap;
                float curY = contentRect.y;
                int nonAbsCount = 0;
                for (auto& ch : children) if (!ch->style.isAbsolute) nonAbsCount++;
                float totalContentH = totalChildrenH + cGap * std::max(0, nonAbsCount - 1);

                if (style.justifyContent == 1) { // Center
                    curY = contentRect.y + std::max(0.0f, (contentRect.w - totalContentH) * 0.5f);
                } else if (style.justifyContent == 2) { // End
                    curY = contentRect.y + std::max(0.0f, contentRect.w - totalContentH);
                }

                for (auto& ch : children) {
                    if (ch->style.isAbsolute) {
                        ch->ComputeLayout(contentRect);
                        continue;
                    }

                    float chX = contentRect.x + ch->style.marginLeft;
                    if (style.alignItems == 1) {
                        chX = contentRect.x + (contentRect.z - ch->computedRect.z) * 0.5f;
                    } else if (style.alignItems == 2) {
                        chX = contentRect.x + contentRect.z - ch->computedRect.z - ch->style.marginRight;
                    } else if (style.alignItems == 3) {
                        ch->computedRect.z = contentRect.z;
                        chX = contentRect.x;
                    }

                    ch->computedRect.x = chX;
                    ch->computedRect.y = curY;
                    ch->ComputeLayout(ch->computedRect);

                    curY += ch->computedRect.w + cGap + ch->style.marginBottom;
                }

                totalContentHeight = (curY - contentRect.y);
                if (style.height <= 0.0f && !style.isHeightPercent) {
                    computedRect.w = std::max(computedRect.w, (curY - elemY) + style.paddingBottom);
                }
            } else {
                float rGap = (style.colGap > 0.0f) ? style.colGap : effGap;
                float curX = contentRect.x;
                int nonAbsCount = 0;
                for (auto& ch : children) if (!ch->style.isAbsolute) nonAbsCount++;
                float totalContentW = totalChildrenW + rGap * std::max(0, nonAbsCount - 1);

                if (style.justifyContent == 1) {
                    curX = contentRect.x + std::max(0.0f, (contentRect.z - totalContentW) * 0.5f);
                } else if (style.justifyContent == 2) {
                    curX = contentRect.x + std::max(0.0f, contentRect.z - totalContentW);
                } else if (style.justifyContent == 3 && nonAbsCount > 1) {
                    rGap = (contentRect.z - totalChildrenW) / (float)(nonAbsCount - 1);
                }

                float maxRowH = 0.0f;
                for (auto& ch : children) {
                    if (ch->style.isAbsolute) {
                        ch->ComputeLayout(contentRect);
                        continue;
                    }

                    float chY = contentRect.y + ch->style.marginTop;
                    if (style.alignItems == 1) {
                        chY = contentRect.y + (contentRect.w - ch->computedRect.w) * 0.5f;
                    } else if (style.alignItems == 2) {
                        chY = contentRect.y + contentRect.w - ch->computedRect.w - ch->style.marginBottom;
                    } else if (style.alignItems == 3) {
                        ch->computedRect.w = contentRect.w;
                        chY = contentRect.y;
                    }

                    ch->computedRect.x = curX;
                    ch->computedRect.y = chY;
                    ch->ComputeLayout(ch->computedRect);

                    curX += ch->computedRect.z + rGap + ch->style.marginRight;
                    maxRowH = std::max(maxRowH, ch->computedRect.w);
                }
                totalContentHeight = maxRowH;
            }
        } else {
            float curY = contentRect.y;
            for (auto& ch : children) {
                if (ch->style.isAbsolute) {
                    ch->ComputeLayout(contentRect);
                    continue;
                }
                ch->computedRect.y = curY;
                ch->ComputeLayout(ch->computedRect);
                curY += ch->computedRect.w + ch->style.marginBottom;
            }
            totalContentHeight = (curY - contentRect.y);
            if (style.height <= 0.0f && !style.isHeightPercent) {
                computedRect.w = std::max(computedRect.w, (curY - elemY) + style.paddingBottom);
            }
        }

        maxScrollY = std::max(0.0f, totalContentHeight - contentRect.w);
    }

    // Interactive Browser-like Input Handling
    void ProcessInput(const glm::vec2& mousePos, bool isMouseDown, bool isMousePressed, bool isMouseReleased, float wheelDelta, const glm::vec2& canvasOrigin, float scale) {
        if (style.isHidden) return;

        glm::vec2 pMin = canvasOrigin + glm::vec2(computedRect.x, computedRect.y) * scale;
        glm::vec2 pMax = pMin + glm::vec2(computedRect.z, computedRect.w) * scale;

        bool inBounds = (mousePos.x >= pMin.x && mousePos.x <= pMax.x && mousePos.y >= pMin.y && mousePos.y <= pMax.y);
        isHovered = inBounds;

        if (isHovered) {
#if EUNOIA_HAS_IMGUI
            if (style.isButton || tag == "button" || tag == "a" || style.cursor == "pointer") {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
#endif
            if (wheelDelta != 0.0f && maxScrollY > 0.0f) {
                scrollY = std::clamp(scrollY - wheelDelta * 40.0f, 0.0f, maxScrollY);
                // Trigger relayout on scroll change
                if (parent) {
                    ComputeLayout(parent->computedRect);
                }
            }

            if (isMousePressed) {
                isPressed = true;
            }

            if (isMouseReleased && isPressed) {
                isClicked = true;
                if (onClick) {
                    onClick();
                }
            }
        }

        if (isMouseReleased) {
            isPressed = false;
        }

        for (auto& ch : children) {
            ch->ProcessInput(mousePos, isMouseDown, isMousePressed, isMouseReleased, wheelDelta, canvasOrigin, scale);
        }
    }

    void Render(void* imDrawListPtr, const glm::vec2& canvasOrigin, float scale, const CssStyleSheet* sheet = nullptr) const {
#if EUNOIA_HAS_IMGUI
        if (style.isHidden) return;
        ImDrawList* dl = reinterpret_cast<ImDrawList*>(imDrawListPtr);
        if (!dl) return;

        glm::vec2 pMin = canvasOrigin + glm::vec2(computedRect.x, computedRect.y) * scale;
        glm::vec2 pMax = pMin + glm::vec2(computedRect.z, computedRect.w) * scale;
        ImVec2 imMin(pMin.x, pMin.y);
        ImVec2 imMax(pMax.x, pMax.y);

        // Effective dynamic styling (supports :hover and :active states)
        CssStyle effStyle = style;
        if (sheet) {
            if (isHovered) {
                sheet->ApplyHoverToNode(tag, id, classes, effStyle);
            }
            if (isPressed) {
                sheet->ApplyActiveToNode(tag, id, classes, effStyle);
            }
        }

        // Automatic button visual feedback if no explicit hover CSS
        if ((effStyle.isButton || tag == "button") && isHovered) {
            if (isPressed) {
                effStyle.backgroundColor *= 0.82f;
            } else {
                effStyle.backgroundColor = glm::min(glm::vec4(1.0f), effStyle.backgroundColor * 1.20f + glm::vec4(0.06f, 0.06f, 0.06f, 0.0f));
            }
        }

        float rounding = effStyle.cornerRounding * scale;
        if (rounding > 999.0f) rounding = std::min(computedRect.z, computedRect.w) * 0.5f * scale;

        // 1. Box Shadow
        if (effStyle.hasShadow && effStyle.shadowColor.a > 0.0f) {
            ImVec2 shdMin(imMin.x + effStyle.shadowOffset.x * scale, imMin.y + effStyle.shadowOffset.y * scale);
            ImVec2 shdMax(imMax.x + effStyle.shadowOffset.x * scale, imMax.y + effStyle.shadowOffset.y * scale);
            ImU32 shdCol = IM_COL32((int)(effStyle.shadowColor.r * 255), (int)(effStyle.shadowColor.g * 255), (int)(effStyle.shadowColor.b * 255), (int)(effStyle.shadowColor.a * effStyle.opacity * 255));
            dl->AddRectFilled(shdMin, shdMax, shdCol, rounding);
        }

        // 2. Background
        if (effStyle.backgroundColor.a > 0.0f) {
            ImU32 bgCol = IM_COL32((int)(effStyle.backgroundColor.r * 255), (int)(effStyle.backgroundColor.g * 255), (int)(effStyle.backgroundColor.b * 255), (int)(effStyle.backgroundColor.a * effStyle.opacity * 255));
            dl->AddRectFilled(imMin, imMax, bgCol, rounding);
        }

        // 3. Border
        if (effStyle.borderThickness > 0.0f && effStyle.borderColor.a > 0.0f) {
            ImU32 bdCol = IM_COL32((int)(effStyle.borderColor.r * 255), (int)(effStyle.borderColor.g * 255), (int)(effStyle.borderColor.b * 255), (int)(effStyle.borderColor.a * effStyle.opacity * 255));
            dl->AddRect(imMin, imMax, bdCol, rounding, 0, effStyle.borderThickness * scale);
        }

        // 4. Text Content
        if (!textContent.empty()) {
            std::string displayText = textContent;
            if (effStyle.textTransform == 1) {
                std::transform(displayText.begin(), displayText.end(), displayText.begin(), ::toupper);
            } else if (effStyle.textTransform == 2) {
                std::transform(displayText.begin(), displayText.end(), displayText.begin(), ::tolower);
            }

            float fontSize = effStyle.fontSize * scale;
            ImU32 txtCol = IM_COL32((int)(effStyle.textColor.r * 255), (int)(effStyle.textColor.g * 255), (int)(effStyle.textColor.b * 255), (int)(effStyle.textColor.a * effStyle.opacity * 255));

            ImVec2 txtSize = ImGui::CalcTextSize(displayText.c_str());
            float scaledTxtW = txtSize.x * (fontSize / 18.0f);
            float scaledTxtH = txtSize.y * (fontSize / 18.0f);

            ImVec2 txtPos = imMin;
            txtPos.x += effStyle.paddingLeft * scale;
            txtPos.y += effStyle.paddingTop * scale;

            if (effStyle.textAlignment == 1 || tag == "button") {
                txtPos.x = imMin.x + ((imMax.x - imMin.x) - scaledTxtW) * 0.5f;
            } else if (effStyle.textAlignment == 2) {
                txtPos.x = imMax.x - effStyle.paddingRight * scale - scaledTxtW;
            }

            if (tag == "button") {
                txtPos.y = imMin.y + ((imMax.y - imMin.y) - scaledTxtH) * 0.5f;
            }

            dl->AddText(nullptr, fontSize, txtPos, txtCol, displayText.c_str());
        }

        // 5. Render Children with clipping for scrollable / overflow containers
        bool needClip = (effStyle.overflow == "hidden" || effStyle.overflow == "auto" || effStyle.overflow == "scroll");
        if (needClip) {
            dl->PushClipRect(imMin, imMax, true);
        }

        for (const auto& ch : children) {
            ch->Render(imDrawListPtr, canvasOrigin, scale, sheet);
        }

        if (needClip) {
            // Sleek web-browser scrollbar indicator
            if (maxScrollY > 0.0f) {
                float scrollTrackH = (imMax.y - imMin.y);
                float thumbH = std::max(24.0f * scale, scrollTrackH * (scrollTrackH / (scrollTrackH + maxScrollY * scale)));
                float thumbY = imMin.y + (scrollY / maxScrollY) * (scrollTrackH - thumbH);
                ImVec2 thumbMin(imMax.x - 6.0f * scale, thumbY);
                ImVec2 thumbMax(imMax.x - 2.0f * scale, thumbY + thumbH);
                dl->AddRectFilled(thumbMin, thumbMax, IM_COL32(180, 200, 230, isHovered ? 140 : 80), 3.0f * scale);
            }
            dl->PopClipRect();
        }
#endif
    }
};

// ============================================================================
// SINGLE HTML + CSS PARSER ENGINE
// ============================================================================
class HtmlCssParser {
public:
    static std::shared_ptr<HtmlNode> Parse(const std::string& htmlSource, CssStyleSheet* outSheet = nullptr) {
        auto root = std::make_shared<HtmlNode>();
        root->tag = "root";
        root->style.isFlex = true;
        root->style.isFlexCol = true;
        root->style.isWidthPercent = true;
        root->style.widthPercent = 1.0f;
        root->style.isHeightPercent = true;
        root->style.heightPercent = 1.0f;

        if (htmlSource.empty()) return root;

        CssStyleSheet localSheet;
        CssStyleSheet& styleSheet = outSheet ? *outSheet : localSheet;

        // Step 1: Extract all <style>...</style> blocks
        size_t stylePos = 0;
        while (true) {
            size_t openStyle = FindTagCaseInsensitive(htmlSource, "<style", stylePos);
            if (openStyle == std::string::npos) break;
            size_t tagClose = htmlSource.find('>', openStyle);
            if (tagClose == std::string::npos) break;
            size_t closeStyle = FindTagCaseInsensitive(htmlSource, "</style>", tagClose);
            if (closeStyle == std::string::npos) break;

            std::string cssBlock = htmlSource.substr(tagClose + 1, closeStyle - tagClose - 1);
            styleSheet.ParseCss(cssBlock);
            stylePos = closeStyle + 8;
        }

        // Step 2: Parse HTML DOM Elements
        size_t pos = 0;
        HtmlNode* currentParent = root.get();
        std::vector<HtmlNode*> nodeStack = { root.get() };

        while (pos < htmlSource.size()) {
            size_t openTag = htmlSource.find('<', pos);
            if (openTag == std::string::npos) {
                std::string text = CleanText(htmlSource.substr(pos));
                if (!text.empty() && currentParent && currentParent != root.get()) {
                    if (!currentParent->textContent.empty()) currentParent->textContent += " ";
                    currentParent->textContent += text;
                }
                break;
            }

            if (openTag > pos) {
                std::string text = CleanText(htmlSource.substr(pos, openTag - pos));
                if (!text.empty() && currentParent && currentParent != root.get()) {
                    if (!currentParent->textContent.empty()) currentParent->textContent += " ";
                    currentParent->textContent += text;
                }
            }

            size_t closeTag = htmlSource.find('>', openTag);
            if (closeTag == std::string::npos) break;

            std::string tagContent = htmlSource.substr(openTag + 1, closeTag - openTag - 1);
            pos = closeTag + 1;

            if (tagContent.rfind("!--", 0) == 0) {
                size_t endComment = htmlSource.find("-->", openTag);
                if (endComment != std::string::npos) {
                    pos = endComment + 3;
                }
                continue;
            }

            std::string tagLower = CssColor::ToLower(tagContent);

            if (tagLower.rfind("!doctype", 0) == 0 || tagLower.rfind("meta", 0) == 0 ||
                tagLower.rfind("link", 0) == 0 || tagLower.rfind("title", 0) == 0) {
                continue;
            }
            if (tagLower.rfind("head", 0) == 0) {
                size_t closeHead = FindTagCaseInsensitive(htmlSource, "</head>", pos);
                if (closeHead != std::string::npos) pos = closeHead + 7;
                continue;
            }
            if (tagLower.rfind("script", 0) == 0) {
                size_t closeScript = FindTagCaseInsensitive(htmlSource, "</script>", pos);
                if (closeScript != std::string::npos) pos = closeScript + 9;
                continue;
            }
            if (tagLower.rfind("style", 0) == 0) {
                size_t closeStyle = FindTagCaseInsensitive(htmlSource, "</style>", pos);
                if (closeStyle != std::string::npos) pos = closeStyle + 8;
                continue;
            }

            if (tagContent.front() == '/') {
                std::string closeTagName = tagContent.substr(1);
                size_t space = closeTagName.find(' ');
                if (space != std::string::npos) closeTagName = closeTagName.substr(0, space);
                closeTagName = CssColor::ToLower(closeTagName);

                if (closeTagName == "html" || closeTagName == "body") {
                    continue;
                }

                if (nodeStack.size() > 1) {
                    nodeStack.pop_back();
                    currentParent = nodeStack.back();
                }
                continue;
            }

            bool selfClosing = (tagContent.back() == '/');
            if (selfClosing) tagContent.pop_back();

            std::istringstream tagStream(tagContent);
            std::string tagName;
            tagStream >> tagName;
            tagName = CssColor::ToLower(tagName);

            if (tagName == "html" || tagName == "body") {
                std::string inlineStyle = ExtractAttribute(tagContent, "style");
                std::string classStr = ExtractAttribute(tagContent, "class");
                std::string idStr = ExtractAttribute(tagContent, "id");
                root->tag = "body";
                root->id = idStr;
                root->ParseClasses(classStr);
                root->ApplyCascade(styleSheet, inlineStyle);
                continue;
            }

            auto node = std::make_shared<HtmlNode>();
            node->tag = tagName;
            node->parent = currentParent;

            std::string classAttr = ExtractAttribute(tagContent, "class");
            node->ParseClasses(classAttr);

            std::string idAttr = ExtractAttribute(tagContent, "id");
            node->id = idAttr;

            std::string styleAttr = ExtractAttribute(tagContent, "style");

            node->ApplyCascade(styleSheet, styleAttr);

            currentParent->children.push_back(node);

            if (!selfClosing && tagName != "hr" && tagName != "img" && tagName != "input" && tagName != "br") {
                nodeStack.push_back(node.get());
                currentParent = node.get();
            }
        }

        return root;
    }

    static std::string GetDefaultHtml(const std::string& title = "New User Interface") {
        return R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" + title + R"(</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            background-color: transparent;
            color: #ffffff;
            font-family: sans-serif;
            overflow: hidden;
            width: 100%;
            height: 100%;
            display: flex;
            align-items: center;
            justify-content: center;
        }

        .card {
            background-color: rgba(15, 23, 42, 0.90);
            border: 2px solid rgba(6, 182, 212, 0.60);
            border-radius: 16px;
            padding: 32px;
            box-shadow: 0 20px 25px rgba(0, 0, 0, 0.5);
            max-width: 500px;
            width: 100%;
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 20px;
        }

        .badge {
            font-size: 12px;
            font-weight: 600;
            color: #22d3ee;
            text-transform: uppercase;
            letter-spacing: 2px;
            background-color: rgba(8, 51, 68, 0.6);
            border: 1px solid rgba(6, 182, 212, 0.3);
            border-radius: 9999px;
            padding: 4px 12px;
        }

        .title {
            font-size: 32px;
            font-weight: 700;
            color: #ffffff;
            text-transform: uppercase;
            letter-spacing: 1px;
            text-align: center;
        }

        .description {
            font-size: 14px;
            color: #cbd5e1;
            text-align: center;
            line-height: 1.5;
        }

        .button-group {
            display: flex;
            gap: 16px;
            justify-content: center;
            width: 100%;
            margin-top: 8px;
        }

        .btn-primary {
            background-color: #0891b2;
            color: #ffffff;
            font-weight: 600;
            font-size: 14px;
            padding: 10px 24px;
            border-radius: 12px;
            border: none;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
            cursor: pointer;
        }

        .btn-primary:hover {
            background-color: #06b6d4;
        }

        .btn-secondary {
            background-color: #1e293b;
            color: #e2e8f0;
            font-weight: 600;
            font-size: 14px;
            padding: 10px 24px;
            border-radius: 12px;
            border: 1px solid #334155;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.2);
            cursor: pointer;
        }

        .btn-secondary:hover {
            background-color: #334155;
        }
    </style>
</head>
<body>
    <div class="card">
        <span class="badge">Eunoia UI</span>
        <h1 class="title">)" + title + R"(</h1>
        <p class="description">Edit this single HTML and CSS file in VS Code to customize your game interface in real-time.</p>
        <div class="button-group">
            <button class="btn-primary">Start Game</button>
            <button class="btn-secondary">Settings</button>
        </div>
    </div>
</body>
</html>
)";
    }

private:
    static size_t FindTagCaseInsensitive(const std::string& str, const std::string& tag, size_t startPos = 0) {
        if (tag.empty() || startPos >= str.size()) return std::string::npos;
        std::string lowerStr = str.substr(startPos);
        std::string lowerTag = tag;
        std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), [](unsigned char c) { return std::tolower(c); });
        std::transform(lowerTag.begin(), lowerTag.end(), lowerTag.begin(), [](unsigned char c) { return std::tolower(c); });
        size_t p = lowerStr.find(lowerTag);
        if (p == std::string::npos) return std::string::npos;
        return startPos + p;
    }

    static std::string CleanText(const std::string& raw) {
        std::string res;
        bool inSpace = true;
        for (char c : raw) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!inSpace) {
                    res += ' ';
                    inSpace = true;
                }
            } else {
                res += c;
                inSpace = false;
            }
        }
        while (!res.empty() && std::isspace(static_cast<unsigned char>(res.front()))) res.erase(res.begin());
        while (!res.empty() && std::isspace(static_cast<unsigned char>(res.back()))) res.pop_back();
        return res;
    }

    static std::string ExtractAttribute(const std::string& tagContent, const std::string& attrName) {
        std::string query = attrName + "=";
        size_t p = tagContent.find(query);
        if (p == std::string::npos) return "";

        size_t valStart = p + query.size();
        if (valStart >= tagContent.size()) return "";

        char quote = tagContent[valStart];
        if (quote == '"' || quote == '\'') {
            valStart++;
            size_t valEnd = tagContent.find(quote, valStart);
            if (valEnd != std::string::npos) {
                return tagContent.substr(valStart, valEnd - valStart);
            }
        } else {
            size_t valEnd = tagContent.find_first_of(" >", valStart);
            return tagContent.substr(valStart, valEnd - valStart);
        }
        return "";
    }
};

} // namespace EunoiaUI
