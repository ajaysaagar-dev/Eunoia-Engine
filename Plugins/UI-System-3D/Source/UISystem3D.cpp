// ============================================================================
// Eunoia Engine — UI-System-3D Plugin Implementation
// 3D World-Space and Screen-Space User Interface rendering & management
// ============================================================================

#define GLM_ENABLE_EXPERIMENTAL
#include "UISystem3D/UISystem3D.h"
#include "UISystem3D/UserInterface3D.h"
#include <EngineScene/GameObject.h>
#include <EngineScene/Scene.h>
#include <EngineRenderer/Camera.h>
#include <UISystem/UISystem.h>
#include <UISystem/HtmlCssParser.h>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace EunoiaUI {

UISystem3DManager& UISystem3DManager::Get() {
    static UISystem3DManager instance;
    return instance;
}

void UISystem3DManager::ClearAll() {
    m_loaded3DUIs.clear();
}

UserInterface* UISystem3DManager::GetOrLoadUIAsset(const std::string& path) {
    if (path.empty()) return nullptr;
    return UISystemManager::Get().GetOrLoadUI(path);
}

std::shared_ptr<UserInterface3D> UISystem3DManager::GetOrLoad3DUI(const std::string& path) {
    if (path.empty()) return nullptr;

    auto it = m_loaded3DUIs.find(path);
    if (it != m_loaded3DUIs.end()) {
        return it->second;
    }

    auto ui3d = std::make_shared<UserInterface3D>("UserInterface3D", path);
    if (std::filesystem::exists(path)) {
        ui3d->LoadFromFile(path);
    }
    m_loaded3DUIs[path] = ui3d;
    return ui3d;
}

UserInterface* UISystem3DManager::CreateNewUI3D(const std::string& name, const std::string& path) {
    auto ui = std::make_shared<UserInterface>(name);
    ui->filePath = path;
    ui->canvasResolution = glm::vec2(1920.0f, 1080.0f);
    ui->elements.clear();

    // Create the HTML file with modern 3D UI styling
    std::string hPath = path;
    size_t dotPos = hPath.rfind('.');
    if (dotPos != std::string::npos) {
        hPath = hPath.substr(0, dotPos) + ".html";
    } else {
        hPath += ".html";
    }
    ui->htmlPath = hPath;
    ui->htmlSource = HtmlCssParser::GetDefaultHtml(name);
    ui->SaveToHtml(hPath);

    ui->SaveToFile(path);
    UserInterface* ptr = ui.get();
    UISystemManager::Get().GetLoadedUIs(); // ensures UI system initialized
    return ptr;
}

void UISystem3DManager::Render3DUI(void* imDrawListPtr, GameObject& obj, const OrbitCamera& camera,
                                  const glm::vec4& vpRect, bool isGameView, bool isSelected) {
    if (!obj.visible || !obj.ui3d.enabled) return;

    ImDrawList* drawList = reinterpret_cast<ImDrawList*>(imDrawListPtr);
    if (!drawList) return;

    float aspect = (vpRect.z > 0.0f && vpRect.w > 0.0f) ? (vpRect.z / vpRect.w) : (16.0f / 9.0f);
    glm::mat4 proj = camera.GetProjectionMatrix(aspect);
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 vpMatrix = proj * view;

    glm::vec3 worldPos = obj.position;
    float halfW = (obj.ui3d.size.x * obj.scale.x) * 0.5f;
    float halfH = (obj.ui3d.size.y * obj.scale.y) * 0.5f;
    if (halfW <= 0.001f) halfW = 1.0f;
    if (halfH <= 0.001f) halfH = 0.5625f;

    glm::vec3 right(1.0f, 0.0f, 0.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 normal(0.0f, 0.0f, 1.0f);

    if (obj.ui3d.space == UI3DSpaceType::Screen) {
        // Screen Space: Billboard facing camera view
        glm::vec3 camPos = camera.GetPosition();
        glm::vec3 fwd = camPos - worldPos;
        if (glm::length2(fwd) > 0.0001f) {
            fwd = glm::normalize(fwd);
        } else {
            fwd = -camera.GetForward();
        }
        glm::vec3 camUp = camera.GetUp();
        right = glm::cross(camUp, fwd);
        if (glm::length2(right) > 0.0001f) {
            right = glm::normalize(right);
        } else {
            right = camera.GetRight();
        }
        up = glm::normalize(glm::cross(fwd, right));
        normal = fwd;
    } else {
        // World Space: Follow actor transform orientation
        glm::mat4 rot(1.0f);
        rot = glm::rotate(rot, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        right  = glm::vec3(rot * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        up     = glm::vec3(rot * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        normal = glm::vec3(rot * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    }

    // 4 Quad Corners in 3D Space
    glm::vec3 cTL = worldPos - right * halfW + up * halfH;
    glm::vec3 cTR = worldPos + right * halfW + up * halfH;
    glm::vec3 cBR = worldPos + right * halfW - up * halfH;
    glm::vec3 cBL = worldPos - right * halfW - up * halfH;

    // Project points to Viewport Screen coordinates
    auto ProjectPoint = [&](const glm::vec3& pt, ImVec2& outScreen) -> bool {
        glm::vec4 clip = vpMatrix * glm::vec4(pt, 1.0f);
        if (clip.w <= 0.01f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
        outScreen.x = vpRect.x + (ndc.x * 0.5f + 0.5f) * vpRect.z;
        outScreen.y = vpRect.y + ((1.0f - ndc.y) * 0.5f) * vpRect.w;
        return true;
    };

    ImVec2 pTL, pTR, pBR, pBL;
    bool vTL = ProjectPoint(cTL, pTL);
    bool vTR = ProjectPoint(cTR, pTR);
    bool vBR = ProjectPoint(cBR, pBR);
    bool vBL = ProjectPoint(cBL, pBL);

    if (!vTL || !vTR || !vBR || !vBL) {
        // Also check center point projection if partially clipped
        ImVec2 pCenter;
        if (!ProjectPoint(worldPos, pCenter)) return;
    }

    // Backface check for single-sided world space UI
    if (!obj.ui3d.twoSided && obj.ui3d.space == UI3DSpaceType::World) {
        glm::vec3 camPos = camera.GetPosition();
        glm::vec3 viewDir = worldPos - camPos;
        if (glm::dot(viewDir, normal) > 0.0f) {
            return; // Backfacing
        }
    }

    // 1. Draw 3D Quad Background (if not transparent)
    if (obj.ui3d.backgroundColor.a > 0.001f) {
        ImU32 bgCol = IM_COL32(
            (int)(obj.ui3d.backgroundColor.r * 255.0f),
            (int)(obj.ui3d.backgroundColor.g * 255.0f),
            (int)(obj.ui3d.backgroundColor.b * 255.0f),
            (int)(obj.ui3d.backgroundColor.a * obj.ui3d.opacity * 255.0f)
        );
        drawList->AddQuadFilled(pTL, pTR, pBR, pBL, bgCol);
    }

    // 2. Render UI Content Elements if UI asset is assigned
    UserInterface* ui = nullptr;
    if (!obj.ui3d.uiAssetPath.empty()) {
        ui = GetOrLoadUIAsset(obj.ui3d.uiAssetPath);
    }

    if (ui && ui->isVisible) {
        float minX = std::min({pTL.x, pTR.x, pBR.x, pBL.x});
        float maxX = std::max({pTL.x, pTR.x, pBR.x, pBL.x});
        float minY = std::min({pTL.y, pTR.y, pBR.y, pBL.y});
        float maxY = std::max({pTL.y, pTR.y, pBR.y, pBL.y});

        glm::vec2 quadMin(minX, minY);
        glm::vec2 quadMax(maxX, maxY);

        // Render UI with clipping within the projected bounding quad
        drawList->PushClipRect(ImVec2(minX, minY), ImVec2(maxX, maxY), true);
        UISystemManager::Get().RenderUI(drawList, quadMin, quadMax, *ui);
        drawList->PopClipRect();
    } else if (!isGameView) {
        // Placeholder preview only in editor if no asset assigned
        ImVec2 center((pTL.x + pTR.x + pBR.x + pBL.x) * 0.25f, (pTL.y + pTR.y + pBR.y + pBL.y) * 0.25f);
        std::string placeholder = "[User Interface 3D]";
        if (!obj.ui3d.uiAssetPath.empty()) {
            placeholder = std::filesystem::path(obj.ui3d.uiAssetPath).stem().string();
        }
        ImVec2 tSize = ImGui::CalcTextSize(placeholder.c_str());
        drawList->AddText(ImVec2(center.x - tSize.x * 0.5f, center.y - tSize.y * 0.5f),
                          IM_COL32(200, 220, 255, (int)(220 * obj.ui3d.opacity)), placeholder.c_str());
    }

    // 3. Draw Quad Border
    if (obj.ui3d.borderWidth > 0.0f && obj.ui3d.borderColor.a > 0.001f) {
        ImU32 borderCol = (isSelected && !isGameView) ? IM_COL32(50, 200, 255, 255) :
                          IM_COL32(
                              (int)(obj.ui3d.borderColor.r * 255.0f),
                              (int)(obj.ui3d.borderColor.g * 255.0f),
                              (int)(obj.ui3d.borderColor.b * 255.0f),
                              (int)(obj.ui3d.borderColor.a * obj.ui3d.opacity * 255.0f)
                          );
        float borderThick = (isSelected && !isGameView) ? (obj.ui3d.borderWidth + 1.5f) : obj.ui3d.borderWidth;
        drawList->AddQuad(pTL, pTR, pBR, pBL, borderCol, borderThick);
    } else if (isSelected && !isGameView) {
        drawList->AddQuad(pTL, pTR, pBR, pBL, IM_COL32(50, 200, 255, 255), 1.5f);
    }

    // 4. Editor Overlay Badge & Controls
    if (!isGameView) {
        ImVec2 topMid((pTL.x + pTR.x) * 0.5f, (pTL.y + pTR.y) * 0.5f);
        std::string spaceTag = (obj.ui3d.space == UI3DSpaceType::Screen) ? "Screen" : "World";
        std::string badgeText = "🖥 " + obj.name + " [" + spaceTag + "]";

        ImVec2 bSize = ImGui::CalcTextSize(badgeText.c_str());
        float bPad = 5.0f;
        ImVec2 bMin(topMid.x - bSize.x * 0.5f - bPad, topMid.y - bSize.y - bPad * 2.0f - 4.0f);
        ImVec2 bMax(topMid.x + bSize.x * 0.5f + bPad, topMid.y - 4.0f);

        drawList->AddRectFilled(bMin, bMax, IM_COL32(20, 26, 36, 220), 4.0f);
        drawList->AddRect(bMin, bMax, isSelected ? IM_COL32(50, 200, 255, 240) : IM_COL32(60, 90, 130, 180), 4.0f);
        drawList->AddText(ImVec2(bMin.x + bPad, bMin.y + bPad),
                          isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 210, 245, 220),
                          badgeText.c_str());

        if (isSelected) {
            // Corner selection handles
            float handleSize = 5.0f;
            auto DrawHandle = [&](const ImVec2& pt) {
                drawList->AddRectFilled(ImVec2(pt.x - handleSize, pt.y - handleSize),
                                       ImVec2(pt.x + handleSize, pt.y + handleSize),
                                       IM_COL32(50, 200, 255, 255), 2.0f);
                drawList->AddRect(ImVec2(pt.x - handleSize, pt.y - handleSize),
                                  ImVec2(pt.x + handleSize, pt.y + handleSize),
                                  IM_COL32(255, 255, 255, 255), 2.0f);
            };
            DrawHandle(pTL);
            DrawHandle(pTR);
            DrawHandle(pBR);
            DrawHandle(pBL);
        }
    }
}

void UISystem3DManager::RenderAllScene3DUI(void* imDrawListPtr, Scene& scene, const OrbitCamera& camera,
                                         const glm::vec4& vpRect, bool isGameView) {
    ImDrawList* drawList = reinterpret_cast<ImDrawList*>(imDrawListPtr);
    if (!drawList) return;

    for (auto& obj : scene.objects) {
        if (!obj.visible) continue;
        if (obj.isUserInterface || obj.type == PrimitiveType::UserInterface || (!obj.ui3d.uiAssetPath.empty() && obj.ui3d.enabled)) {
            bool isSel = (scene.selectedId == obj.id);
            Render3DUI(drawList, obj, camera, vpRect, isGameView, isSel);
        }
    }
}

} // namespace EunoiaUI
