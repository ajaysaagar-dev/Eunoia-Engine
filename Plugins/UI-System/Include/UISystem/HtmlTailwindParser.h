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
#include <glm/glm.hpp>

#if __has_include(<imgui.h>)
#include <imgui.h>
#define EUNOIA_HAS_IMGUI 1
#else
#define EUNOIA_HAS_IMGUI 0
#endif

namespace EunoiaUI {

struct TailwindColor {
    static glm::vec4 Parse(const std::string& colorName, float defaultAlpha = 1.0f) {
        if (colorName.empty() || colorName == "transparent") return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        if (colorName == "white") return glm::vec4(1.0f, 1.0f, 1.0f, defaultAlpha);
        if (colorName == "black") return glm::vec4(0.0f, 0.0f, 0.0f, defaultAlpha);

        // Check for hex arbitrary value e.g. [#1e293b]
        if (colorName.front() == '[' && colorName.back() == ']') {
            std::string inner = colorName.substr(1, colorName.size() - 2);
            if (inner.front() == '#') {
                std::string hex = inner.substr(1);
                if (hex.size() == 6) {
                    float r = (float)std::stoi(hex.substr(0, 2), nullptr, 16) / 255.0f;
                    float g = (float)std::stoi(hex.substr(2, 2), nullptr, 16) / 255.0f;
                    float b = (float)std::stoi(hex.substr(4, 2), nullptr, 16) / 255.0f;
                    return glm::vec4(r, g, b, defaultAlpha);
                } else if (hex.size() == 3) {
                    float r = (float)std::stoi(std::string(2, hex[0]), nullptr, 16) / 255.0f;
                    float g = (float)std::stoi(std::string(2, hex[1]), nullptr, 16) / 255.0f;
                    float b = (float)std::stoi(std::string(2, hex[2]), nullptr, 16) / 255.0f;
                    return glm::vec4(r, g, b, defaultAlpha);
                }
            }
        }

        // Split base and shade/alpha e.g. "cyan-500/50" or "slate-900"
        std::string name = colorName;
        float alpha = defaultAlpha;
        size_t slashPos = name.find('/');
        if (slashPos != std::string::npos) {
            std::string aStr = name.substr(slashPos + 1);
            name = name.substr(0, slashPos);
            try {
                alpha = std::clamp(std::stof(aStr) / 100.0f, 0.0f, 1.0f);
            } catch (...) {}
        }

        // Palette definitions (RGB 0.0 - 1.0)
        static const std::unordered_map<std::string, glm::vec3> s_palette = {
            // Slate
            { "slate-50",  { 0.97f, 0.98f, 0.99f } }, { "slate-100", { 0.94f, 0.96f, 0.98f } },
            { "slate-200", { 0.89f, 0.91f, 0.94f } }, { "slate-300", { 0.80f, 0.84f, 0.88f } },
            { "slate-400", { 0.58f, 0.64f, 0.72f } }, { "slate-500", { 0.39f, 0.45f, 0.55f } },
            { "slate-600", { 0.28f, 0.34f, 0.43f } }, { "slate-700", { 0.20f, 0.25f, 0.33f } },
            { "slate-800", { 0.12f, 0.16f, 0.23f } }, { "slate-900", { 0.06f, 0.09f, 0.16f } },
            { "slate-950", { 0.01f, 0.03f, 0.07f } },
            // Gray
            { "gray-50",  { 0.98f, 0.98f, 0.98f } }, { "gray-100", { 0.95f, 0.96f, 0.96f } },
            { "gray-200", { 0.90f, 0.91f, 0.92f } }, { "gray-300", { 0.82f, 0.84f, 0.86f } },
            { "gray-400", { 0.61f, 0.64f, 0.69f } }, { "gray-500", { 0.42f, 0.45f, 0.50f } },
            { "gray-600", { 0.30f, 0.33f, 0.39f } }, { "gray-700", { 0.22f, 0.25f, 0.31f } },
            { "gray-800", { 0.12f, 0.14f, 0.18f } }, { "gray-900", { 0.07f, 0.09f, 0.13f } },
            { "gray-950", { 0.01f, 0.02f, 0.05f } },
            // Zinc
            { "zinc-800", { 0.15f, 0.15f, 0.17f } }, { "zinc-900", { 0.09f, 0.09f, 0.11f } },
            { "zinc-950", { 0.04f, 0.04f, 0.05f } },
            // Cyan
            { "cyan-50",  { 0.93f, 0.99f, 1.00f } }, { "cyan-100", { 0.81f, 0.96f, 0.99f } },
            { "cyan-200", { 0.65f, 0.92f, 0.98f } }, { "cyan-300", { 0.41f, 0.84f, 0.96f } },
            { "cyan-400", { 0.13f, 0.74f, 0.93f } }, { "cyan-500", { 0.02f, 0.61f, 0.81f } },
            { "cyan-600", { 0.03f, 0.49f, 0.68f } }, { "cyan-700", { 0.06f, 0.39f, 0.55f } },
            { "cyan-800", { 0.08f, 0.33f, 0.45f } }, { "cyan-900", { 0.09f, 0.27f, 0.37f } },
            // Sky & Blue
            { "sky-400",  { 0.22f, 0.74f, 0.97f } }, { "sky-500",  { 0.06f, 0.62f, 0.93f } },
            { "blue-400", { 0.38f, 0.65f, 0.98f } }, { "blue-500", { 0.23f, 0.51f, 0.96f } },
            { "blue-600", { 0.15f, 0.39f, 0.92f } }, { "blue-700", { 0.11f, 0.31f, 0.85f } },
            // Emerald & Green
            { "emerald-400", { 0.20f, 0.83f, 0.55f } }, { "emerald-500", { 0.06f, 0.73f, 0.45f } },
            { "emerald-600", { 0.02f, 0.60f, 0.36f } },
            { "green-400",   { 0.29f, 0.87f, 0.40f } }, { "green-500",   { 0.13f, 0.77f, 0.37f } },
            // Amber & Yellow & Red
            { "amber-400", { 0.98f, 0.75f, 0.14f } }, { "amber-500", { 0.96f, 0.62f, 0.04f } },
            { "yellow-400",{ 0.98f, 0.80f, 0.08f } }, { "yellow-500",{ 0.92f, 0.70f, 0.03f } },
            { "red-400",   { 0.97f, 0.44f, 0.44f } }, { "red-500",   { 0.94f, 0.27f, 0.24f } },
            { "red-600",   { 0.86f, 0.15f, 0.15f } }, { "red-700",   { 0.73f, 0.11f, 0.11f } },
            // Purple & Indigo
            { "purple-400", { 0.75f, 0.52f, 0.99f } }, { "purple-500", { 0.66f, 0.33f, 0.98f } },
            { "purple-600", { 0.58f, 0.19f, 0.92f } },
            { "indigo-500", { 0.39f, 0.40f, 0.95f } }, { "indigo-600", { 0.31f, 0.27f, 0.90f } },
            // Rose & Pink
            { "rose-500", { 0.95f, 0.17f, 0.35f } }, { "pink-500", { 0.93f, 0.28f, 0.60f } }
        };

        auto it = s_palette.find(name);
        if (it != s_palette.end()) {
            return glm::vec4(it->second, alpha);
        }

        // Generic fallback
        if (name.find("slate") != std::string::npos || name.find("gray") != std::string::npos || name.find("zinc") != std::string::npos) {
            return glm::vec4(0.12f, 0.15f, 0.20f, alpha);
        }
        if (name.find("cyan") != std::string::npos || name.find("sky") != std::string::npos) {
            return glm::vec4(0.06f, 0.65f, 0.85f, alpha);
        }
        if (name.find("blue") != std::string::npos) {
            return glm::vec4(0.20f, 0.50f, 0.95f, alpha);
        }
        if (name.find("green") != std::string::npos || name.find("emerald") != std::string::npos) {
            return glm::vec4(0.10f, 0.75f, 0.40f, alpha);
        }
        if (name.find("red") != std::string::npos || name.find("rose") != std::string::npos) {
            return glm::vec4(0.90f, 0.25f, 0.25f, alpha);
        }

        return glm::vec4(1.0f, 1.0f, 1.0f, alpha);
    }
};

struct TailwindStyle {
    glm::vec4 backgroundColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    glm::vec4 borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    float borderThickness = 0.0f;
    float cornerRounding = 0.0f;

    glm::vec4 textColor{ 0.95f, 0.95f, 0.95f, 1.0f };
    float fontSize = 16.0f;
    int fontWeight = 400; // 400=normal, 600=semibold, 700=bold
    int textAlignment = 0; // 0=left, 1=center, 2=right
    int textTransform = 0; // 0=none, 1=uppercase, 2=lowercase, 3=capitalize
    float letterSpacing = 0.0f;

    float paddingLeft = 0.0f, paddingRight = 0.0f, paddingTop = 0.0f, paddingBottom = 0.0f;
    float marginLeft = 0.0f, marginRight = 0.0f, marginTop = 0.0f, marginBottom = 0.0f;

    float width = -1.0f, height = -1.0f; // -1 = auto
    bool isWidthPercent = false, isHeightPercent = false;
    float widthPercent = 0.0f, heightPercent = 0.0f;
    float minWidth = 0.0f, minHeight = 0.0f;
    float maxWidth = 999999.0f, maxHeight = 999999.0f;

    bool isFlex = false;
    bool isFlexCol = false;
    bool isFlexWrap = false;
    int justifyContent = 0; // 0=start, 1=center, 2=end, 3=between, 4=around, 5=evenly
    int alignItems = 0;     // 0=start, 1=center, 2=end, 3=stretch
    float gap = 0.0f;
    float spaceX = 0.0f;
    float spaceY = 0.0f;

    bool hasShadow = false;
    glm::vec4 shadowColor{ 0.0f, 0.0f, 0.0f, 0.45f };
    glm::vec2 shadowOffset{ 0.0f, 6.0f };
    float shadowBlur = 12.0f;

    float opacity = 1.0f;
    bool isAbsolute = false;
    float top = 0.0f, left = 0.0f, right = 0.0f, bottom = 0.0f;
    bool hasTop = false, hasLeft = false, hasRight = false, hasBottom = false;

    bool isButton = false;
    bool isCard = false;
    bool isCentered = false;
    glm::vec4 hoverBgColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    bool hasHoverBg = false;

    void ApplyClass(const std::string& cls) {
        if (cls.empty()) return;

        // Display & Flexbox
        if (cls == "flex") { isFlex = true; }
        else if (cls == "flex-col") { isFlex = true; isFlexCol = true; }
        else if (cls == "flex-row") { isFlex = true; isFlexCol = false; }
        else if (cls == "flex-wrap") { isFlexWrap = true; }
        else if (cls == "items-center") { alignItems = 1; }
        else if (cls == "items-start") { alignItems = 0; }
        else if (cls == "items-end") { alignItems = 2; }
        else if (cls == "items-stretch") { alignItems = 3; }
        else if (cls == "justify-center") { justifyContent = 1; }
        else if (cls == "justify-start") { justifyContent = 0; }
        else if (cls == "justify-end") { justifyContent = 2; }
        else if (cls == "justify-between") { justifyContent = 3; }
        else if (cls == "justify-around") { justifyContent = 4; }
        else if (cls == "justify-evenly") { justifyContent = 5; }

        // Gaps & Spacing
        else if (cls.rfind("gap-", 0) == 0) {
            float val = ParseSpacingVal(cls.substr(4));
            gap = val;
        } else if (cls.rfind("space-x-", 0) == 0) {
            spaceX = ParseSpacingVal(cls.substr(8));
        } else if (cls.rfind("space-y-", 0) == 0) {
            spaceY = ParseSpacingVal(cls.substr(8));
        }

        // Padding
        else if (cls.rfind("p-", 0) == 0) {
            float v = ParseSpacingVal(cls.substr(2));
            paddingLeft = paddingRight = paddingTop = paddingBottom = v;
        } else if (cls.rfind("px-", 0) == 0) {
            float v = ParseSpacingVal(cls.substr(3));
            paddingLeft = paddingRight = v;
        } else if (cls.rfind("py-", 0) == 0) {
            float v = ParseSpacingVal(cls.substr(3));
            paddingTop = paddingBottom = v;
        } else if (cls.rfind("pt-", 0) == 0) { paddingTop = ParseSpacingVal(cls.substr(3)); }
        else if (cls.rfind("pr-", 0) == 0) { paddingRight = ParseSpacingVal(cls.substr(3)); }
        else if (cls.rfind("pb-", 0) == 0) { paddingBottom = ParseSpacingVal(cls.substr(3)); }
        else if (cls.rfind("pl-", 0) == 0) { paddingLeft = ParseSpacingVal(cls.substr(3)); }

        // Margin
        else if (cls.rfind("m-", 0) == 0) {
            float v = ParseSpacingVal(cls.substr(2));
            marginLeft = marginRight = marginTop = marginBottom = v;
        } else if (cls.rfind("mx-auto", 0) == 0) {
            isCentered = true;
        } else if (cls.rfind("mx-", 0) == 0) {
            float v = ParseSpacingVal(cls.substr(3));
            marginLeft = marginRight = v;
        } else if (cls.rfind("my-", 0) == 0) {
            float v = ParseSpacingVal(cls.substr(3));
            marginTop = marginBottom = v;
        } else if (cls.rfind("mt-", 0) == 0) { marginTop = ParseSpacingVal(cls.substr(3)); }
        else if (cls.rfind("mr-", 0) == 0) { marginRight = ParseSpacingVal(cls.substr(3)); }
        else if (cls.rfind("mb-", 0) == 0) { marginBottom = ParseSpacingVal(cls.substr(3)); }
        else if (cls.rfind("ml-", 0) == 0) { marginLeft = ParseSpacingVal(cls.substr(3)); }

        // Width
        else if (cls == "w-full" || cls == "w-screen") { isWidthPercent = true; widthPercent = 1.0f; }
        else if (cls == "w-auto") { width = -1.0f; isWidthPercent = false; }
        else if (cls == "w-1/2") { isWidthPercent = true; widthPercent = 0.50f; }
        else if (cls == "w-1/3") { isWidthPercent = true; widthPercent = 0.333f; }
        else if (cls == "w-2/3") { isWidthPercent = true; widthPercent = 0.666f; }
        else if (cls == "w-1/4") { isWidthPercent = true; widthPercent = 0.25f; }
        else if (cls == "w-3/4") { isWidthPercent = true; widthPercent = 0.75f; }
        else if (cls.rfind("w-", 0) == 0) {
            width = ParseSpacingVal(cls.substr(2));
            isWidthPercent = false;
        } else if (cls == "max-w-xs") { maxWidth = 320.0f; }
        else if (cls == "max-w-sm") { maxWidth = 384.0f; }
        else if (cls == "max-w-md") { maxWidth = 448.0f; }
        else if (cls == "max-w-lg") { maxWidth = 512.0f; }
        else if (cls == "max-w-xl") { maxWidth = 576.0f; }
        else if (cls == "max-w-2xl") { maxWidth = 672.0f; }
        else if (cls == "max-w-full") { maxWidth = 999999.0f; }

        // Height
        else if (cls == "h-full" || cls == "h-screen") { isHeightPercent = true; heightPercent = 1.0f; }
        else if (cls == "h-auto") { height = -1.0f; isHeightPercent = false; }
        else if (cls == "h-1/2") { isHeightPercent = true; heightPercent = 0.50f; }
        else if (cls.rfind("h-", 0) == 0) {
            height = ParseSpacingVal(cls.substr(2));
            isHeightPercent = false;
        }

        // Background Color
        else if (cls.rfind("bg-", 0) == 0) {
            backgroundColor = TailwindColor::Parse(cls.substr(3));
        } else if (cls.rfind("hover:bg-", 0) == 0) {
            hoverBgColor = TailwindColor::Parse(cls.substr(9));
            hasHoverBg = true;
        }

        // Text Color & Typography
        else if (cls.rfind("text-", 0) == 0) {
            std::string sub = cls.substr(5);
            if (sub == "xs") { fontSize = 12.0f; }
            else if (sub == "sm") { fontSize = 14.0f; }
            else if (sub == "base") { fontSize = 16.0f; }
            else if (sub == "lg") { fontSize = 18.0f; }
            else if (sub == "xl") { fontSize = 20.0f; }
            else if (sub == "2xl") { fontSize = 24.0f; }
            else if (sub == "3xl") { fontSize = 30.0f; }
            else if (sub == "4xl") { fontSize = 36.0f; }
            else if (sub == "5xl") { fontSize = 48.0f; }
            else if (sub == "left") { textAlignment = 0; }
            else if (sub == "center") { textAlignment = 1; }
            else if (sub == "right") { textAlignment = 2; }
            else {
                textColor = TailwindColor::Parse(sub);
            }
        } else if (cls == "font-bold") { fontWeight = 700; }
        else if (cls == "font-semibold") { fontWeight = 600; }
        else if (cls == "font-medium") { fontWeight = 500; }
        else if (cls == "font-light" || cls == "font-thin") { fontWeight = 300; }
        else if (cls == "uppercase") { textTransform = 1; }
        else if (cls == "lowercase") { textTransform = 2; }
        else if (cls == "capitalize") { textTransform = 3; }
        else if (cls == "tracking-wide") { letterSpacing = 0.5f; }
        else if (cls == "tracking-wider") { letterSpacing = 1.0f; }
        else if (cls == "tracking-widest") { letterSpacing = 2.0f; }

        // Borders & Rounding
        else if (cls == "border") { borderThickness = 1.0f; if (borderColor.a == 0.0f) borderColor = glm::vec4(0.3f, 0.4f, 0.5f, 0.5f); }
        else if (cls == "border-0") { borderThickness = 0.0f; }
        else if (cls == "border-2") { borderThickness = 2.0f; }
        else if (cls == "border-4") { borderThickness = 4.0f; }
        else if (cls == "border-8") { borderThickness = 8.0f; }
        else if (cls.rfind("border-", 0) == 0) {
            borderColor = TailwindColor::Parse(cls.substr(7));
            if (borderThickness == 0.0f) borderThickness = 1.0f;
        }
        else if (cls == "rounded-none") { cornerRounding = 0.0f; }
        else if (cls == "rounded-sm") { cornerRounding = 2.0f; }
        else if (cls == "rounded") { cornerRounding = 4.0f; }
        else if (cls == "rounded-md") { cornerRounding = 6.0f; }
        else if (cls == "rounded-lg") { cornerRounding = 8.0f; }
        else if (cls == "rounded-xl") { cornerRounding = 12.0f; }
        else if (cls == "rounded-2xl") { cornerRounding = 16.0f; }
        else if (cls == "rounded-3xl") { cornerRounding = 24.0f; }
        else if (cls == "rounded-full") { cornerRounding = 9999.0f; }

        // Shadows
        else if (cls.rfind("shadow", 0) == 0) {
            hasShadow = true;
            if (cls == "shadow-sm") { shadowOffset = { 0, 2 }; shadowBlur = 4; shadowColor.a = 0.25f; }
            else if (cls == "shadow") { shadowOffset = { 0, 4 }; shadowBlur = 6; shadowColor.a = 0.35f; }
            else if (cls == "shadow-md") { shadowOffset = { 0, 6 }; shadowBlur = 10; shadowColor.a = 0.45f; }
            else if (cls == "shadow-lg") { shadowOffset = { 0, 10 }; shadowBlur = 16; shadowColor.a = 0.55f; }
            else if (cls == "shadow-xl") { shadowOffset = { 0, 16 }; shadowBlur = 24; shadowColor.a = 0.65f; }
            else if (cls == "shadow-2xl") { shadowOffset = { 0, 24 }; shadowBlur = 36; shadowColor.a = 0.75f; }
            else if (cls == "shadow-none") { hasShadow = false; }
        }

        // Positioning
        else if (cls == "absolute") { isAbsolute = true; }
        else if (cls == "relative") { isAbsolute = false; }
        else if (cls == "top-0") { top = 0.0f; hasTop = true; }
        else if (cls == "bottom-0") { bottom = 0.0f; hasBottom = true; }
        else if (cls == "left-0") { left = 0.0f; hasLeft = true; }
        else if (cls == "right-0") { right = 0.0f; hasRight = true; }
        else if (cls == "inset-0") { top = bottom = left = right = 0.0f; hasTop = hasBottom = hasLeft = hasRight = true; }
    }

private:
    static float ParseSpacingVal(const std::string& str) {
        if (str.empty()) return 0.0f;
        try {
            // Tailwind spacing scale: 1 = 4px, 2 = 8px, 4 = 16px, etc.
            float num = std::stof(str);
            return num * 4.0f;
        } catch (...) {
            return 0.0f;
        }
    }
};

class HtmlNode {
public:
    std::string tag = "div";
    std::string id = "";
    std::string classList = "";
    std::string textContent = "";
    std::unordered_map<std::string, std::string> attributes;
    TailwindStyle style;
    std::vector<std::shared_ptr<HtmlNode>> children;
    HtmlNode* parent = nullptr;
    glm::vec4 computedRect{ 0.0f, 0.0f, 0.0f, 0.0f }; // x, y, width, height

    void ParseClasses(const std::string& classes) {
        classList = classes;
        std::istringstream iss(classes);
        std::string cls;
        while (iss >> cls) {
            style.ApplyClass(cls);
        }
    }

    void ComputeLayout(const glm::vec4& parentRect) {
        float pW = parentRect.z;
        float pH = parentRect.w;

        // Compute element width
        float elemW = 0.0f;
        if (style.isWidthPercent) {
            elemW = pW * style.widthPercent;
        } else if (style.width > 0.0f) {
            elemW = style.width;
        } else {
            // Auto width: expand to parent available width for block/flex container
            if (tag == "div" || tag == "section" || tag == "main" || tag == "header" || tag == "footer" || style.isFlex) {
                elemW = pW;
            } else if (tag == "button") {
                elemW = std::max(120.0f, (float)textContent.length() * (style.fontSize * 0.60f) + style.paddingLeft + style.paddingRight + 24.0f);
            } else {
                elemW = (float)textContent.length() * (style.fontSize * 0.58f) + style.paddingLeft + style.paddingRight;
            }
        }
        elemW = std::clamp(elemW, style.minWidth, style.maxWidth);

        // Compute element height
        float elemH = 0.0f;
        if (style.isHeightPercent) {
            elemH = pH * style.heightPercent;
        } else if (style.height > 0.0f) {
            elemH = style.height;
        } else {
            if (tag == "h1") elemH = style.fontSize * 1.5f + style.paddingTop + style.paddingBottom;
            else if (tag == "h2") elemH = style.fontSize * 1.4f + style.paddingTop + style.paddingBottom;
            else if (tag == "p" || tag == "span" || tag == "label") elemH = style.fontSize * 1.3f + style.paddingTop + style.paddingBottom;
            else if (tag == "button") elemH = style.fontSize + style.paddingTop + style.paddingBottom + 16.0f;
            else if (tag == "hr") elemH = 2.0f + style.marginTop + style.marginBottom;
            else elemH = 40.0f; // container base
        }

        // Position
        float elemX = parentRect.x + style.marginLeft;
        float elemY = parentRect.y + style.marginTop;

        if (style.isCentered) {
            elemX = parentRect.x + (pW - elemW) * 0.5f;
        }

        computedRect = glm::vec4(elemX, elemY, elemW, elemH);

        if (children.empty()) return;

        // Content area inside padding
        glm::vec4 contentRect(
            elemX + style.paddingLeft,
            elemY + style.paddingTop,
            std::max(0.0f, elemW - style.paddingLeft - style.paddingRight),
            std::max(0.0f, elemH - style.paddingTop - style.paddingBottom)
        );

        // Compute children sizes first
        std::vector<glm::vec2> childSizes;
        float totalChildrenW = 0.0f;
        float totalChildrenH = 0.0f;
        for (const auto& ch : children) {
            ch->ComputeLayout(contentRect);
            childSizes.push_back(glm::vec2(ch->computedRect.z, ch->computedRect.w));
            totalChildrenW += ch->computedRect.z;
            totalChildrenH += ch->computedRect.w;
        }

        // Flex layout distribution
        if (style.isFlex) {
            if (style.isFlexCol) {
                float curY = contentRect.y;
                float spacing = (style.gap > 0.0f) ? style.gap : style.spaceY;
                float totalContentH = totalChildrenH + spacing * std::max(0, (int)children.size() - 1);

                if (style.justifyContent == 1) { // Center
                    curY = contentRect.y + (contentRect.w - totalContentH) * 0.5f;
                } else if (style.justifyContent == 2) { // End
                    curY = contentRect.y + contentRect.w - totalContentH;
                }

                for (size_t i = 0; i < children.size(); ++i) {
                    auto& ch = children[i];
                    float chX = contentRect.x + ch->style.marginLeft;
                    if (style.alignItems == 1) { // Center
                        chX = contentRect.x + (contentRect.z - ch->computedRect.z) * 0.5f;
                    } else if (style.alignItems == 2) { // End
                        chX = contentRect.x + contentRect.z - ch->computedRect.z - ch->style.marginRight;
                    } else if (style.alignItems == 3) { // Stretch
                        ch->computedRect.z = contentRect.z;
                        chX = contentRect.x;
                    }

                    ch->computedRect.x = chX;
                    ch->computedRect.y = curY;
                    ch->ComputeLayout(ch->computedRect);

                    curY += ch->computedRect.w + spacing + ch->style.marginBottom;
                }

                // Auto-expand container height to fit children if auto height
                if (style.height <= 0.0f && !style.isHeightPercent) {
                    computedRect.w = std::max(computedRect.w, (curY - elemY) + style.paddingBottom);
                }
            } else {
                // Flex Row
                float curX = contentRect.x;
                float spacing = (style.gap > 0.0f) ? style.gap : style.spaceX;
                float totalContentW = totalChildrenW + spacing * std::max(0, (int)children.size() - 1);

                if (style.justifyContent == 1) { // Center
                    curX = contentRect.x + (contentRect.z - totalContentW) * 0.5f;
                } else if (style.justifyContent == 2) { // End
                    curX = contentRect.x + contentRect.z - totalContentW;
                } else if (style.justifyContent == 3 && children.size() > 1) { // Between
                    spacing = (contentRect.z - totalChildrenW) / (float)(children.size() - 1);
                }

                for (size_t i = 0; i < children.size(); ++i) {
                    auto& ch = children[i];
                    float chY = contentRect.y + ch->style.marginTop;
                    if (style.alignItems == 1) { // Center
                        chY = contentRect.y + (contentRect.w - ch->computedRect.w) * 0.5f;
                    } else if (style.alignItems == 2) { // End
                        chY = contentRect.y + contentRect.w - ch->computedRect.w - ch->style.marginBottom;
                    } else if (style.alignItems == 3) { // Stretch
                        ch->computedRect.w = contentRect.w;
                        chY = contentRect.y;
                    }

                    ch->computedRect.x = curX;
                    ch->computedRect.y = chY;
                    ch->ComputeLayout(ch->computedRect);

                    curX += ch->computedRect.z + spacing + ch->style.marginRight;
                }
            }
        } else {
            // Normal Block Flow
            float curY = contentRect.y;
            for (auto& ch : children) {
                ch->computedRect.y = curY;
                ch->ComputeLayout(ch->computedRect);
                curY += ch->computedRect.w + ch->style.marginBottom;
            }
            if (style.height <= 0.0f && !style.isHeightPercent) {
                computedRect.w = std::max(computedRect.w, (curY - elemY) + style.paddingBottom);
            }
        }
    }

    void Render(void* imDrawListPtr, const glm::vec2& canvasOrigin, float scale) const {
#if EUNOIA_HAS_IMGUI
        ImDrawList* dl = reinterpret_cast<ImDrawList*>(imDrawListPtr);
        if (!dl) return;

        glm::vec2 pMin = canvasOrigin + glm::vec2(computedRect.x, computedRect.y) * scale;
        glm::vec2 pMax = pMin + glm::vec2(computedRect.z, computedRect.w) * scale;
        ImVec2 imMin(pMin.x, pMin.y);
        ImVec2 imMax(pMax.x, pMax.y);

        float rounding = style.cornerRounding * scale;
        if (rounding > 999.0f) rounding = std::min(computedRect.z, computedRect.w) * 0.5f * scale;

        // 1. Drop Shadow
        if (style.hasShadow && style.shadowColor.a > 0.0f) {
            ImVec2 shdMin(imMin.x + style.shadowOffset.x * scale, imMin.y + style.shadowOffset.y * scale);
            ImVec2 shdMax(imMax.x + style.shadowOffset.x * scale, imMax.y + style.shadowOffset.y * scale);
            ImU32 shdCol = IM_COL32((int)(style.shadowColor.r * 255), (int)(style.shadowColor.g * 255), (int)(style.shadowColor.b * 255), (int)(style.shadowColor.a * 255));
            dl->AddRectFilled(shdMin, shdMax, shdCol, rounding);
        }

        // 2. Background
        if (style.backgroundColor.a > 0.0f) {
            ImU32 bgCol = IM_COL32((int)(style.backgroundColor.r * 255), (int)(style.backgroundColor.g * 255), (int)(style.backgroundColor.b * 255), (int)(style.backgroundColor.a * 255));
            dl->AddRectFilled(imMin, imMax, bgCol, rounding);
        }

        // 3. Border
        if (style.borderThickness > 0.0f && style.borderColor.a > 0.0f) {
            ImU32 bdCol = IM_COL32((int)(style.borderColor.r * 255), (int)(style.borderColor.g * 255), (int)(style.borderColor.b * 255), (int)(style.borderColor.a * 255));
            dl->AddRect(imMin, imMax, bdCol, rounding, 0, style.borderThickness * scale);
        }

        // 4. Text / Content
        if (!textContent.empty()) {
            std::string displayText = textContent;
            if (style.textTransform == 1) { // Uppercase
                std::transform(displayText.begin(), displayText.end(), displayText.begin(), ::toupper);
            } else if (style.textTransform == 2) { // Lowercase
                std::transform(displayText.begin(), displayText.end(), displayText.begin(), ::tolower);
            }

            float fontSize = style.fontSize * scale;
            ImU32 txtCol = IM_COL32((int)(style.textColor.r * 255), (int)(style.textColor.g * 255), (int)(style.textColor.b * 255), (int)(style.textColor.a * 255));

            ImVec2 txtSize = ImGui::CalcTextSize(displayText.c_str());
            float scaledTxtW = txtSize.x * (fontSize / 18.0f);
            float scaledTxtH = txtSize.y * (fontSize / 18.0f);

            ImVec2 txtPos = imMin;
            txtPos.x += style.paddingLeft * scale;
            txtPos.y += style.paddingTop * scale;

            if (style.textAlignment == 1 || tag == "button") { // Center
                txtPos.x = imMin.x + ((imMax.x - imMin.x) - scaledTxtW) * 0.5f;
            } else if (style.textAlignment == 2) { // Right
                txtPos.x = imMax.x - style.paddingRight * scale - scaledTxtW;
            }

            if (tag == "button") {
                txtPos.y = imMin.y + ((imMax.y - imMin.y) - scaledTxtH) * 0.5f;
            }

            dl->AddText(nullptr, fontSize, txtPos, txtCol, displayText.c_str());
        }

        // 5. Render Children
        for (const auto& ch : children) {
            ch->Render(imDrawListPtr, canvasOrigin, scale);
        }
#endif
    }
};

class HtmlTailwindParser {
public:
    static std::shared_ptr<HtmlNode> Parse(const std::string& htmlSource) {
        auto root = std::make_shared<HtmlNode>();
        root->tag = "root";
        root->style.isFlex = true;
        root->style.isFlexCol = true;
        root->style.isWidthPercent = true;
        root->style.widthPercent = 1.0f;
        root->style.isHeightPercent = true;
        root->style.heightPercent = 1.0f;

        if (htmlSource.empty()) return root;

        size_t pos = 0;
        HtmlNode* currentParent = root.get();
        std::vector<HtmlNode*> nodeStack = { root.get() };

        while (pos < htmlSource.size()) {
            size_t openTag = htmlSource.find('<', pos);
            if (openTag == std::string::npos) {
                // Remaining text
                std::string text = CleanText(htmlSource.substr(pos));
                if (!text.empty() && currentParent && currentParent != root.get()) {
                    if (!currentParent->textContent.empty()) currentParent->textContent += " ";
                    currentParent->textContent += text;
                }
                break;
            }

            // Capture text before open tag
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

            // Handle comments <!-- ... -->
            if (tagContent.rfind("!--", 0) == 0) {
                size_t endComment = htmlSource.find("-->", openTag);
                if (endComment != std::string::npos) {
                    pos = endComment + 3;
                }
                continue;
            }

            // Handle doctype, head, script, style, meta, link (skip or ignore contents)
            std::string tagLower = tagContent;
            std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(), ::tolower);

            if (tagLower.rfind("!doctype", 0) == 0 || tagLower.rfind("meta", 0) == 0 ||
                tagLower.rfind("link", 0) == 0 || tagLower.rfind("title", 0) == 0) {
                continue;
            }
            if (tagLower.rfind("head", 0) == 0) {
                size_t closeHead = htmlSource.find("</head>", pos);
                if (closeHead != std::string::npos) pos = closeHead + 7;
                continue;
            }
            if (tagLower.rfind("script", 0) == 0) {
                size_t closeScript = htmlSource.find("</script>", pos);
                if (closeScript != std::string::npos) pos = closeScript + 9;
                continue;
            }
            if (tagLower.rfind("style", 0) == 0) {
                size_t closeStyle = htmlSource.find("</style>", pos);
                if (closeStyle != std::string::npos) pos = closeStyle + 8;
                continue;
            }

            // Closing tag </tag>
            if (tagContent.front() == '/') {
                std::string closeTagName = tagContent.substr(1);
                size_t space = closeTagName.find(' ');
                if (space != std::string::npos) closeTagName = closeTagName.substr(0, space);
                std::transform(closeTagName.begin(), closeTagName.end(), closeTagName.begin(), ::tolower);

                if (closeTagName == "html" || closeTagName == "body") {
                    continue;
                }

                if (nodeStack.size() > 1) {
                    nodeStack.pop_back();
                    currentParent = nodeStack.back();
                }
                continue;
            }

            // Self-closing or void tag (e.g. <hr />, <img />, <input />)
            bool selfClosing = (tagContent.back() == '/');
            if (selfClosing) tagContent.pop_back();

            // Extract tag name and attributes
            std::istringstream tagStream(tagContent);
            std::string tagName;
            tagStream >> tagName;
            std::transform(tagName.begin(), tagName.end(), tagName.begin(), ::tolower);

            if (tagName == "html" || tagName == "body") {
                // If body has classes, apply to root
                std::string classStr = ExtractAttribute(tagContent, "class");
                if (!classStr.empty()) {
                    root->ParseClasses(classStr);
                }
                continue;
            }

            auto node = std::make_shared<HtmlNode>();
            node->tag = tagName;
            node->parent = currentParent;

            // Extract class and id attributes
            std::string classAttr = ExtractAttribute(tagContent, "class");
            node->ParseClasses(classAttr);

            std::string idAttr = ExtractAttribute(tagAttributeToString(tagContent), "id");
            node->id = idAttr;

            if (tagName == "button") node->style.isButton = true;

            currentParent->children.push_back(node);

            // If not self-closing and not void tags, push to stack
            if (!selfClosing && tagName != "hr" && tagName != "img" && tagName != "input" && tagName != "br") {
                nodeStack.push_back(node.get());
                currentParent = node.get();
            }
        }

        return root;
    }

    static std::string GetDefaultTailwindHtml(const std::string& title = "New User Interface") {
        return R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" + title + R"(</title>
    <script src="https://cdn.tailwindcss.com"></script>
</head>
<body class="bg-transparent text-white font-sans overflow-hidden">
    <!-- Eunoia UI Fullscreen Canvas Layout -->
    <div class="w-full h-full flex flex-col items-center justify-center p-8 space-y-6">
        
        <!-- Main Interface Card -->
        <div class="bg-slate-900/85 border-2 border-cyan-500/60 rounded-2xl p-8 shadow-2xl backdrop-blur-md max-w-lg w-full flex flex-col items-center space-y-5">
            
            <!-- Header & Badge -->
            <div class="flex items-center justify-between w-full">
                <span class="text-xs font-semibold text-cyan-400 uppercase tracking-widest bg-cyan-950/60 border border-cyan-500/30 rounded-full px-3 py-1">
                    Eunoia UI
                </span>
                <span class="text-xs text-slate-400">Live HTML</span>
            </div>

            <!-- Title & Description -->
            <h1 class="text-3xl font-bold text-white tracking-wide uppercase text-center">
                )" + title + R"(
            </h1>
            <p class="text-sm text-slate-300 text-center leading-relaxed">
                Edit this HTML and Tailwind CSS file in VS Code to customize your game interface in real-time.
            </p>

            <!-- Action Controls -->
            <div class="flex items-center justify-center space-x-4 w-full pt-2">
                <button class="bg-cyan-600 hover:bg-cyan-500 text-white font-semibold text-sm px-6 py-2.5 rounded-xl shadow-lg transition-all duration-200 cursor-pointer">
                    Start Game
                </button>
                <button class="bg-slate-800 hover:bg-slate-700 border border-slate-700 text-slate-200 font-semibold text-sm px-6 py-2.5 rounded-xl shadow-md transition-all duration-200 cursor-pointer">
                    Settings
                </button>
            </div>

        </div>

    </div>
</body>
</html>
)";
    }

private:
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

    static std::string tagAttributeToString(const std::string& str) {
        return str;
    }
};

} // namespace EunoiaUI
