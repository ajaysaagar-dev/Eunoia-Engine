#pragma once

#include "UITypes.h"
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace EunoiaUI {

class UserInterface {
public:
    std::string name = "New User Interface";
    std::string filePath = "";
    glm::vec2 canvasResolution{ 1920.0f, 1080.0f };
    std::vector<UIElement> elements;

    UserInterface() = default;
    explicit UserInterface(const std::string& inName) : name(inName) {}

    UIElement* AddElement(UIElementType type, const std::string& elemName = "") {
        static uint64_t s_elemCounter = 1;
        UIElement elem;
        elem.id = "elem_" + std::to_string(s_elemCounter++) + "_" + std::to_string(elements.size() + 1);
        elem.type = type;

        if (!elemName.empty()) {
            elem.name = elemName;
        } else {
            elem.name = std::string(UIElementTypeToString(type)) + "_" + std::to_string(elements.size() + 1);
        }

        if (type == UIElementType::Border) {
            elem.position = glm::vec2(100.0f + (elements.size() % 10) * 20.0f, 100.0f + (elements.size() % 10) * 20.0f);
            elem.size = glm::vec2(250.0f, 120.0f);
            elem.backgroundColor = glm::vec4(0.10f, 0.10f, 0.12f, 0.85f);
            elem.borderColor = glm::vec4(0.25f, 0.60f, 1.0f, 1.0f);
            elem.borderThickness = 2.0f;
            elem.cornerRounding = 6.0f;
            elem.filled = true;
        } else if (type == UIElementType::Text) {
            elem.position = glm::vec2(120.0f + (elements.size() % 10) * 20.0f, 120.0f + (elements.size() % 10) * 20.0f);
            elem.size = glm::vec2(200.0f, 40.0f);
            elem.text = "Sample Text";
            elem.fontSize = 20.0f;
            elem.textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            elem.textAlignment = 0;
            elem.shadow = true;
        }

        elem.zOrder = (int)elements.size();
        elements.push_back(elem);
        return &elements.back();
    }

    UIElement* FindElement(const std::string& id) {
        for (auto& el : elements) {
            if (el.id == id) return &el;
        }
        return nullptr;
    }

    const UIElement* FindElement(const std::string& id) const {
        for (const auto& el : elements) {
            if (el.id == id) return &el;
        }
        return nullptr;
    }

    bool RemoveElement(const std::string& id) {
        for (auto it = elements.begin(); it != elements.end(); ++it) {
            if (it->id == id) {
                elements.erase(it);
                return true;
            }
        }
        return false;
    }

    bool SaveToFile(const std::string& path) {
        nlohmann::json root;
        root["name"] = name;
        root["canvasResolution"] = { canvasResolution.x, canvasResolution.y };

        nlohmann::json elemArray = nlohmann::json::array();
        for (const auto& el : elements) {
            nlohmann::json e;
            e["id"] = el.id;
            e["name"] = el.name;
            e["type"] = UIElementTypeToString(el.type);
            e["position"] = { el.position.x, el.position.y };
            e["size"] = { el.size.x, el.size.y };
            e["anchor"] = { el.anchor.x, el.anchor.y };
            e["pivot"] = { el.pivot.x, el.pivot.y };
            e["zOrder"] = el.zOrder;
            e["visible"] = el.visible;
            e["parentId"] = el.parentId;

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

            // Image / button properties
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
                    if (e.contains("anchor") && e["anchor"].size() >= 2) {
                        el.anchor.x = e["anchor"][0].get<float>();
                        el.anchor.y = e["anchor"][1].get<float>();
                    }
                    if (e.contains("pivot") && e["pivot"].size() >= 2) {
                        el.pivot.x = e["pivot"][0].get<float>();
                        el.pivot.y = e["pivot"][1].get<float>();
                    }
                    if (e.contains("zOrder")) el.zOrder = e["zOrder"].get<int>();
                    if (e.contains("visible")) el.visible = e["visible"].get<bool>();
                    if (e.contains("parentId")) el.parentId = e["parentId"].get<std::string>();

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
            return true;
        } catch (...) {
            return false;
        }
    }
};

} // namespace EunoiaUI

// Global type alias for script convenience
using UserInterface = EunoiaUI::UserInterface;
