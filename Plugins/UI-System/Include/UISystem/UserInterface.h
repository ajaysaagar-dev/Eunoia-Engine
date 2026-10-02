#pragma once

#include "UITypes.h"
#include "HtmlCssParser.h"
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_set>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace EunoiaUI {

class UserInterface {
public:
    std::string name = "New User Interface";
    std::string filePath = "";
    std::string htmlPath = "";
    std::string htmlSource = "";
    std::shared_ptr<HtmlNode> htmlRoot = nullptr;
    std::filesystem::file_time_type lastHtmlWriteTime{};

    glm::vec2 canvasResolution{ 1920.0f, 1080.0f };
    std::vector<UIElement> elements;
    bool isVisible = true;

    UserInterface() = default;
    explicit UserInterface(const std::string& inName) : name(inName) {}

    // Runtime Visibility API
    void Show() { isVisible = true; }
    void Hide() { isVisible = false; }
    void SetVisible(bool v) { isVisible = v; }
    bool IsVisible() const { return isVisible; }

    UIElement* AddElement(UIElementType type, const std::string& elemName = "", const std::string& parentId = "") {
        static uint64_t s_elemCounter = 1;
        UIElement elem;
        elem.id = "elem_" + std::to_string(s_elemCounter++) + "_" + std::to_string(elements.size() + 1);
        elem.type = type;

        if (!elemName.empty()) {
            elem.name = elemName;
        } else {
            elem.name = std::string(UIElementTypeToString(type)) + "_" + std::to_string(elements.size() + 1);
        }

        elem.anchorPreset = UIAnchorPreset::TopLeft;
        AnchorPresetToMinMax(elem.anchorPreset, elem.anchorMin, elem.anchorMax, elem.pivot);

        if (type == UIElementType::Border) {
            elem.position = glm::vec2(50.0f, 50.0f);
            elem.size = glm::vec2(300.0f, 150.0f);
            elem.backgroundColor = glm::vec4(0.10f, 0.12f, 0.16f, 0.90f);
            elem.borderColor = glm::vec4(0.25f, 0.60f, 1.0f, 1.0f);
            elem.borderThickness = 2.0f;
            elem.cornerRounding = 8.0f;
            elem.filled = true;
        } else if (type == UIElementType::Text) {
            elem.position = glm::vec2(20.0f, 20.0f);
            elem.size = glm::vec2(200.0f, 40.0f);
            elem.text = "New Text";
            elem.fontSize = 20.0f;
            elem.textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            elem.textAlignment = 0;
            elem.shadow = true;
        }

        elem.zOrder = (int)elements.size();
        elem.parentId = parentId;

        elements.push_back(elem);

        if (!parentId.empty()) {
            UIElement* parent = FindElement(parentId);
            if (parent) {
                parent->childIds.push_back(elements.back().id);
                parent->isExpanded = true;
            }
        }

        return &elements.back();
    }

    UIElement* FindElement(const std::string& idOrName) {
        for (auto& el : elements) {
            if (el.id == idOrName || el.name == idOrName) return &el;
        }
        return nullptr;
    }

    const UIElement* FindElement(const std::string& idOrName) const {
        for (const auto& el : elements) {
            if (el.id == idOrName || el.name == idOrName) return &el;
        }
        return nullptr;
    }

    bool IsDescendantOf(const std::string& potentialChildId, const std::string& potentialAncestorId) const {
        if (potentialChildId.empty() || potentialAncestorId.empty()) return false;
        if (potentialChildId == potentialAncestorId) return true;

        const UIElement* child = FindElement(potentialChildId);
        while (child && !child->parentId.empty()) {
            if (child->parentId == potentialAncestorId) return true;
            child = FindElement(child->parentId);
        }
        return false;
    }

    bool SetParent(const std::string& childId, const std::string& newParentId) {
        if (childId.empty() || childId == newParentId) return false;
        if (!newParentId.empty() && IsDescendantOf(newParentId, childId)) {
            // Cannot parent to own descendant
            return false;
        }

        UIElement* child = FindElement(childId);
        if (!child) return false;

        // Remove from old parent
        if (!child->parentId.empty()) {
            UIElement* oldParent = FindElement(child->parentId);
            if (oldParent) {
                auto it = std::find(oldParent->childIds.begin(), oldParent->childIds.end(), childId);
                if (it != oldParent->childIds.end()) oldParent->childIds.erase(it);
            }
        }

        // Assign new parent
        child->parentId = newParentId;
        if (!newParentId.empty()) {
            UIElement* newParent = FindElement(newParentId);
            if (newParent) {
                if (std::find(newParent->childIds.begin(), newParent->childIds.end(), childId) == newParent->childIds.end()) {
                    newParent->childIds.push_back(childId);
                }
                newParent->isExpanded = true;
            }
        }
        return true;
    }

    bool RemoveElement(const std::string& id) {
        // Collect all descendant ids
        std::unordered_set<std::string> toRemove;
        auto collectDescendants = [&](auto& self, const std::string& curId) -> void {
            toRemove.insert(curId);
            const UIElement* el = FindElement(curId);
            if (el) {
                for (const auto& ch : el->childIds) {
                    self(self, ch);
                }
            }
        };
        collectDescendants(collectDescendants, id);

        // Remove from parent's child list
        UIElement* target = FindElement(id);
        if (target && !target->parentId.empty()) {
            UIElement* parent = FindElement(target->parentId);
            if (parent) {
                auto it = std::find(parent->childIds.begin(), parent->childIds.end(), id);
                if (it != parent->childIds.end()) parent->childIds.erase(it);
            }
        }

        // Erase elements
        elements.erase(std::remove_if(elements.begin(), elements.end(), [&](const UIElement& el) {
            return toRemove.find(el.id) != toRemove.end();
        }), elements.end());

        return true;
    }

    // Calculates world / canvas-relative rectangle (X, Y, Width, Height) taking hierarchy into account
    glm::vec4 GetElementWorldRect(const UIElement& el) const {
        if (el.parentId.empty()) {
            glm::vec4 canvasRect(0.0f, 0.0f, canvasResolution.x, canvasResolution.y);
            return ComputeElementRect(canvasRect, el);
        }
        const UIElement* parent = FindElement(el.parentId);
        if (!parent) {
            glm::vec4 canvasRect(0.0f, 0.0f, canvasResolution.x, canvasResolution.y);
            return ComputeElementRect(canvasRect, el);
        }
        glm::vec4 parentRect = GetElementWorldRect(*parent);
        return ComputeElementRect(parentRect, el);
    }

    // Script helper APIs
    bool SetText(const std::string& elemNameOrId, const std::string& newText) {
        UIElement* el = FindElement(elemNameOrId);
        if (el && el->type == UIElementType::Text) {
            el->text = newText;
            return true;
        }
        return false;
    }

    bool SetTextColor(const std::string& elemNameOrId, const glm::vec4& color) {
        UIElement* el = FindElement(elemNameOrId);
        if (el && el->type == UIElementType::Text) {
            el->textColor = color;
            return true;
        }
        return false;
    }

    bool SetBorderColor(const std::string& elemNameOrId, const glm::vec4& color) {
        UIElement* el = FindElement(elemNameOrId);
        if (el && el->type == UIElementType::Border) {
            el->borderColor = color;
            return true;
        }
        return false;
    }

    bool SetBackgroundColor(const std::string& elemNameOrId, const glm::vec4& color) {
        UIElement* el = FindElement(elemNameOrId);
        if (el && el->type == UIElementType::Border) {
            el->backgroundColor = color;
            return true;
        }
        return false;
    }

    std::string GetHtmlPath() const {
        if (!htmlPath.empty()) return htmlPath;
        if (!filePath.empty()) {
            std::filesystem::path p = filePath;
            p.replace_extension(".html");
            return p.string();
        }
        return "";
    }

    CssStyleSheet htmlSheet;

    bool LoadFromHtml(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        htmlSource = ss.str();
        htmlPath = path;
        htmlSheet = CssStyleSheet();
        htmlRoot = HtmlCssParser::Parse(htmlSource, &htmlSheet);
        if (htmlRoot) {
            htmlRoot->ComputeLayout(glm::vec4(0.0f, 0.0f, canvasResolution.x, canvasResolution.y));
        }
        std::error_code ec;
        lastHtmlWriteTime = std::filesystem::last_write_time(path, ec);
        return true;
    }

    bool SaveToHtml(const std::string& path = "") {
        std::string target = path.empty() ? GetHtmlPath() : path;
        if (target.empty()) return false;
        if (htmlSource.empty()) {
            htmlSource = HtmlCssParser::GetDefaultHtml(name);
        }
        std::ofstream out(target);
        if (!out.is_open()) return false;
        out << htmlSource;
        htmlPath = target;
        std::error_code ec;
        lastHtmlWriteTime = std::filesystem::last_write_time(target, ec);
        return true;
    }

    bool CheckAndReloadHtml() {
        std::string target = GetHtmlPath();
        if (target.empty()) return false;
        std::error_code ec;
        if (!std::filesystem::exists(target, ec)) return false;
        auto curTime = std::filesystem::last_write_time(target, ec);
        if (ec) return false;
        if (curTime != lastHtmlWriteTime) {
            return LoadFromHtml(target);
        }
        return false;
    }

    void ProcessInput(const glm::vec2& mousePos, bool isMouseDown, bool isMousePressed, bool isMouseReleased, float wheelDelta, const glm::vec2& canvasOrigin, float scale) {
        if (htmlRoot) {
            htmlRoot->ProcessInput(mousePos, isMouseDown, isMousePressed, isMouseReleased, wheelDelta, canvasOrigin, scale);
        }
    }

    HtmlNode* FindHtmlNode(const std::string& idOrTagOrClass) {
        if (!htmlRoot) return nullptr;
        std::function<HtmlNode*(HtmlNode*)> search = [&](HtmlNode* node) -> HtmlNode* {
            if (!node) return nullptr;
            if (node->id == idOrTagOrClass || node->tag == idOrTagOrClass) return node;
            for (const auto& cls : node->classes) {
                if (cls == idOrTagOrClass || ("." + cls) == idOrTagOrClass) return node;
            }
            for (auto& ch : node->children) {
                HtmlNode* res = search(ch.get());
                if (res) return res;
            }
            return nullptr;
        };
        return search(htmlRoot.get());
    }

    void RenderHtml(void* imDrawListPtr, const glm::vec2& canvasOrigin, float scale) const {
        if (htmlRoot) {
            htmlRoot->Render(imDrawListPtr, canvasOrigin, scale, &htmlSheet);
        }
    }

    bool SaveToFile(const std::string& path) {
        nlohmann::json root;
        root["name"] = name;
        root["canvasResolution"] = { canvasResolution.x, canvasResolution.y };
        root["isVisible"] = isVisible;

        std::string hPath = GetHtmlPath();
        if (hPath.empty()) {
            std::filesystem::path p = path;
            p.replace_extension(".html");
            hPath = p.string();
        }
        root["htmlPath"] = hPath;

        nlohmann::json elemArray = nlohmann::json::array();
        for (const auto& el : elements) {
            nlohmann::json e;
            e["id"] = el.id;
            e["name"] = el.name;
            e["type"] = UIElementTypeToString(el.type);
            e["position"] = { el.position.x, el.position.y };
            e["size"] = { el.size.x, el.size.y };
            e["anchorMin"] = { el.anchorMin.x, el.anchorMin.y };
            e["anchorMax"] = { el.anchorMax.x, el.anchorMax.y };
            e["pivot"] = { el.pivot.x, el.pivot.y };
            e["anchorPreset"] = (int)el.anchorPreset;
            e["zOrder"] = el.zOrder;
            e["visible"] = el.visible;
            e["parentId"] = el.parentId;
            e["childIds"] = el.childIds;
            e["isExpanded"] = el.isExpanded;

            // Border properties
            e["backgroundColor"] = { el.backgroundColor.r, el.backgroundColor.g, el.backgroundColor.b, el.backgroundColor.a };
            e["borderColor"] = { el.borderColor.r, el.borderColor.g, el.borderColor.b, el.borderColor.a };
            e["borderThickness"] = el.borderThickness;
            e["cornerRounding"] = el.cornerRounding;
            e["filled"] = el.filled;

            // Text properties
            e["text"] = el.text;
            e["fontSize"] = el.fontSize;
            e["textColor"] = { el.textColor.r, el.textColor.g, el.textColor.b, el.textColor.a };
            e["textAlignment"] = el.textAlignment;
            e["wordWrap"] = el.wordWrap;
            e["shadow"] = el.shadow;
            e["shadowColor"] = { el.shadowColor.r, el.shadowColor.g, el.shadowColor.b, el.shadowColor.a };
            e["shadowOffset"] = { el.shadowOffset.x, el.shadowOffset.y };

            // Interactive properties
            e["hoverColor"] = { el.hoverColor.r, el.hoverColor.g, el.hoverColor.b, el.hoverColor.a };
            e["pressedColor"] = { el.pressedColor.r, el.pressedColor.g, el.pressedColor.b, el.pressedColor.a };
            e["texturePath"] = el.texturePath;

            elemArray.push_back(e);
        }
        root["elements"] = elemArray;

        std::ofstream out(path);
        if (!out.is_open()) return false;
        out << std::setw(4) << root << std::endl;
        filePath = path;

        // Automatically ensure corresponding HTML file is saved as well
        SaveToHtml(hPath);

        return true;
    }

    bool LoadFromFile(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        try {
            nlohmann::json root;
            in >> root;

            if (root.contains("name")) name = root["name"].get<std::string>();
            if (root.contains("canvasResolution") && root["canvasResolution"].is_array() && root["canvasResolution"].size() >= 2) {
                canvasResolution.x = root["canvasResolution"][0].get<float>();
                canvasResolution.y = root["canvasResolution"][1].get<float>();
            }
            if (root.contains("isVisible")) isVisible = root["isVisible"].get<bool>();
            if (root.contains("htmlPath")) htmlPath = root["htmlPath"].get<std::string>();

            elements.clear();
            if (root.contains("elements") && root["elements"].is_array()) {
                for (const auto& e : root["elements"]) {
                    UIElement el;
                    if (e.contains("id")) el.id = e["id"].get<std::string>();
                    if (e.contains("name")) el.name = e["name"].get<std::string>();
                    if (e.contains("type")) el.type = StringToUIElementType(e["type"].get<std::string>());
                    if (e.contains("position") && e["position"].size() >= 2) {
                        el.position.x = e["position"][0].get<float>();
                        el.position.y = e["position"][1].get<float>();
                    }
                    if (e.contains("size") && e["size"].size() >= 2) {
                        el.size.x = e["size"][0].get<float>();
                        el.size.y = e["size"][1].get<float>();
                    }
                    if (e.contains("anchorMin") && e["anchorMin"].size() >= 2) {
                        el.anchorMin.x = e["anchorMin"][0].get<float>();
                        el.anchorMin.y = e["anchorMin"][1].get<float>();
                    } else if (e.contains("anchor") && e["anchor"].size() >= 2) {
                        el.anchorMin.x = e["anchor"][0].get<float>();
                        el.anchorMin.y = e["anchor"][1].get<float>();
                        el.anchorMax = el.anchorMin;
                    }
                    if (e.contains("anchorMax") && e["anchorMax"].size() >= 2) {
                        el.anchorMax.x = e["anchorMax"][0].get<float>();
                        el.anchorMax.y = e["anchorMax"][1].get<float>();
                    }
                    if (e.contains("pivot") && e["pivot"].size() >= 2) {
                        el.pivot.x = e["pivot"][0].get<float>();
                        el.pivot.y = e["pivot"][1].get<float>();
                    }
                    if (e.contains("anchorPreset")) {
                        el.anchorPreset = (UIAnchorPreset)e["anchorPreset"].get<int>();
                    } else {
                        el.anchorPreset = AnchorMinMaxToPreset(el.anchorMin, el.anchorMax);
                    }
                    if (e.contains("zOrder")) el.zOrder = e["zOrder"].get<int>();
                    if (e.contains("visible")) el.visible = e["visible"].get<bool>();
                    if (e.contains("parentId")) el.parentId = e["parentId"].get<std::string>();
                    if (e.contains("childIds") && e["childIds"].is_array()) {
                        for (const auto& ch : e["childIds"]) el.childIds.push_back(ch.get<std::string>());
                    }
                    if (e.contains("isExpanded")) el.isExpanded = e["isExpanded"].get<bool>();

                    if (e.contains("backgroundColor") && e["backgroundColor"].size() >= 4) {
                        el.backgroundColor = glm::vec4(e["backgroundColor"][0], e["backgroundColor"][1], e["backgroundColor"][2], e["backgroundColor"][3]);
                    }
                    if (e.contains("borderColor") && e["borderColor"].size() >= 4) {
                        el.borderColor = glm::vec4(e["borderColor"][0], e["borderColor"][1], e["borderColor"][2], e["borderColor"][3]);
                    }
                    if (e.contains("borderThickness")) el.borderThickness = e["borderThickness"].get<float>();
                    if (e.contains("cornerRounding")) el.cornerRounding = e["cornerRounding"].get<float>();
                    if (e.contains("filled")) el.filled = e["filled"].get<bool>();

                    if (e.contains("text")) el.text = e["text"].get<std::string>();
                    if (e.contains("fontSize")) el.fontSize = e["fontSize"].get<float>();
                    if (e.contains("textColor") && e["textColor"].size() >= 4) {
                        el.textColor = glm::vec4(e["textColor"][0], e["textColor"][1], e["textColor"][2], e["textColor"][3]);
                    }
                    if (e.contains("textAlignment")) el.textAlignment = e["textAlignment"].get<int>();
                    if (e.contains("wordWrap")) el.wordWrap = e["wordWrap"].get<bool>();
                    if (e.contains("shadow")) el.shadow = e["shadow"].get<bool>();
                    if (e.contains("shadowColor") && e["shadowColor"].size() >= 4) {
                        el.shadowColor = glm::vec4(e["shadowColor"][0], e["shadowColor"][1], e["shadowColor"][2], e["shadowColor"][3]);
                    }
                    if (e.contains("shadowOffset") && e["shadowOffset"].size() >= 2) {
                        el.shadowOffset.x = e["shadowOffset"][0].get<float>();
                        el.shadowOffset.y = e["shadowOffset"][1].get<float>();
                    }
                    if (e.contains("texturePath")) el.texturePath = e["texturePath"].get<std::string>();

                    elements.push_back(el);
                }
            }
            filePath = path;

            // Load HTML file
            std::string hPath = GetHtmlPath();
            std::error_code ec;
            if (std::filesystem::exists(hPath, ec)) {
                LoadFromHtml(hPath);
            } else {
                SaveToHtml(hPath);
                LoadFromHtml(hPath);
            }

            return true;
        } catch (...) {
            return false;
        }
    }
};

} // namespace EunoiaUI

using UserInterface = EunoiaUI::UserInterface;
