#pragma once

#include "UserInterface.h"
#include "UISystem.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <algorithm>
#include <cmath>

namespace EunoiaUI {

class UIEditorWindow {
public:
    UserInterface ui;
    std::string filePath;
    bool isOpen = true;
    bool isDirty = false;

    // Canvas navigation
    float zoom = 0.65f;
    ImVec2 pan = { 40.0f, 40.0f };
    bool isPanning = false;
    ImVec2 panStart = { 0.0f, 0.0f };
    ImVec2 panInitial = { 0.0f, 0.0f };

    // Selection & Manipulation
    std::string selectedElemId = ""; // empty means Canvas (Root) is selected
    bool isDraggingElement = false;
    ImVec2 dragOffset = { 0.0f, 0.0f };
    int resizeHandle = 0; // 0=none, 1=TL, 2=TR, 3=BR, 4=BL

    UIEditorWindow(const std::string& path, const std::string& name = "") {
        filePath = path;
        if (!path.empty()) {
            if (!ui.LoadFromFile(path)) {
                ui.name = name.empty() ? "New UI" : name;
            }
        } else {
            ui.name = name.empty() ? "New UI" : name;
        }
    }

    void Render() {
        if (!isOpen) return;

        std::string winTitle = "🎨 UI Editor - " + ui.name + (isDirty ? " *" : "") + "###UIEditor_" + filePath;
        ImGui::SetNextWindowSize(ImVec2(1080, 720), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin(winTitle.c_str(), &isOpen, ImGuiWindowFlags_MenuBar)) {
            ImGui::End();
            return;
        }

        // Menu Bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("💾 Save", "Ctrl+S")) {
                    Save();
                }
                if (ImGui::MenuItem("❌ Close")) {
                    isOpen = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Add")) {
                if (ImGui::MenuItem("🔲 Border")) {
                    AddElement(UIElementType::Border, "", selectedElemId);
                }
                if (ImGui::MenuItem("🔤 Text")) {
                    AddElement(UIElementType::Text, "", selectedElemId);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Top Toolbar
        RenderToolbar();

        // 3-Pane Layout: Left (Palette & Hierarchy) | Center (Canvas) | Right (Inspector)
        float leftWidth = 260.0f;
        float rightWidth = 300.0f;
        float centerWidth = ImGui::GetContentRegionAvail().x - leftWidth - rightWidth - 16.0f;
        if (centerWidth < 200.0f) centerWidth = 200.0f;
        float paneHeight = ImGui::GetContentRegionAvail().y;

        // --- LEFT PANE (Palette + Hierarchy Tree) ---
        ImGui::BeginChild("UILeftPane", ImVec2(leftWidth, paneHeight), true);
        RenderLeftPaletteAndHierarchy();
        ImGui::EndChild();

        ImGui::SameLine();

        // --- CENTER PANE (Visual Interactive Canvas) ---
        ImGui::BeginChild("UICenterCanvas", ImVec2(centerWidth, paneHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderCanvas();
        ImGui::EndChild();

        ImGui::SameLine();

        // --- RIGHT PANE (Inspector / Details) ---
        ImGui::BeginChild("UIRightInspector", ImVec2(rightWidth, paneHeight), true);
        RenderRightInspector();
        ImGui::EndChild();

        ImGui::End();
    }

    void Save() {
        if (!filePath.empty()) {
            if (ui.SaveToFile(filePath)) {
                isDirty = false;
            }
        }
    }

    UIElement* AddElement(UIElementType type, const std::string& name = "", const std::string& parentId = "") {
        UIElement* elem = ui.AddElement(type, name, parentId);
        if (elem) {
            selectedElemId = elem->id;
            isDirty = true;
        }
        return elem;
    }

private:
    void RenderToolbar() {
        if (ImGui::Button("💾 Save")) {
            Save();
        }
        ImGui::SameLine();
        if (ImGui::Button("➕ Border")) {
            AddElement(UIElementType::Border, "", selectedElemId);
        }
        ImGui::SameLine();
        if (ImGui::Button("➕ Text")) {
            AddElement(UIElementType::Text, "", selectedElemId);
        }
        ImGui::SameLine();
        if (!selectedElemId.empty()) {
            if (ImGui::Button("🗑 Delete")) {
                ui.RemoveElement(selectedElemId);
                selectedElemId = "";
                isDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("📋 Duplicate")) {
                UIElement* orig = ui.FindElement(selectedElemId);
                if (orig) {
                    UIElement* dup = ui.AddElement(orig->type, orig->name + "_Copy", orig->parentId);
                    if (dup) {
                        *dup = *orig;
                        dup->id = "elem_" + std::to_string(rand() % 100000) + "_" + std::to_string(ui.elements.size());
                        dup->name = orig->name + "_Copy";
                        dup->position += glm::vec2(20.0f, 20.0f);
                        dup->childIds.clear();
                        selectedElemId = dup->id;
                        isDirty = true;
                    }
                }
            }
            ImGui::SameLine();
        }

        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Zoom controls
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::SliderFloat("Zoom", &zoom, 0.2f, 2.0f, "%.2fx")) {
            if (zoom < 0.2f) zoom = 0.2f;
        }
        ImGui::SameLine();
        if (ImGui::Button("100%")) {
            zoom = 1.0f;
            pan = ImVec2(40.0f, 40.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Fit")) {
            zoom = 0.5f;
            pan = ImVec2(20.0f, 20.0f);
        }
        ImGui::Separator();
    }

    void RenderLeftPaletteAndHierarchy() {
        // --- PALETTE ---
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "PALETTE (Drag to Canvas)");
        ImGui::Separator();

        // Draggable Border item
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.32f, 0.45f, 1.0f));
        ImGui::Button("🔲 Border", ImVec2(-1, 30));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            int typeVal = (int)UIElementType::Border;
            ImGui::SetDragDropPayload("UI_PALETTE_ITEM", &typeVal, sizeof(int));
            ImGui::Text("Drop Border onto Canvas or Hierarchy");
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            AddElement(UIElementType::Border, "", selectedElemId);
        }

        // Draggable Text item
        ImGui::Button("🔤 Text", ImVec2(-1, 30));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            int typeVal = (int)UIElementType::Text;
            ImGui::SetDragDropPayload("UI_PALETTE_ITEM", &typeVal, sizeof(int));
            ImGui::Text("Drop Text onto Canvas or Hierarchy");
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            AddElement(UIElementType::Text, "", selectedElemId);
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- HIERARCHY TREE ---
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "HIERARCHY TREE");
        ImGui::Separator();

        ImGui::BeginChild("HierarchyTreeScroll", ImVec2(0, 0), false);

        // Canvas (Root) Node
        bool isCanvasSelected = selectedElemId.empty();
        ImGuiTreeNodeFlags canvasFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen | (isCanvasSelected ? ImGuiTreeNodeFlags_Selected : 0);
        bool canvasOpen = ImGui::TreeNodeEx("CanvasRootNode", canvasFlags, "🖥 Canvas (Root) [%.0fx%.0f]", ui.canvasResolution.x, ui.canvasResolution.y);
        
        if (ImGui::IsItemClicked()) {
            selectedElemId = ""; // Select Canvas root
        }

        // Drop target onto Canvas Root
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_HIERARCHY_ITEM")) {
                std::string draggedId((const char*)payload->Data, payload->DataSize);
                ui.SetParent(draggedId, ""); // Unparent to root
                isDirty = true;
            } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_PALETTE_ITEM")) {
                int typeVal = *(const int*)payload->Data;
                AddElement((UIElementType)typeVal, "", "");
            }
            ImGui::EndDragDropTarget();
        }

        if (canvasOpen) {
            // Render root-level elements (those without parent)
            for (auto& elem : ui.elements) {
                if (elem.parentId.empty() || !ui.FindElement(elem.parentId)) {
                    DrawHierarchyNode(elem);
                }
            }
            ImGui::TreePop();
        }

        // Empty area drop target to unparent
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y > 20.0f) {
            ImGui::Dummy(ImVec2(avail.x, avail.y));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_HIERARCHY_ITEM")) {
                    std::string draggedId((const char*)payload->Data, payload->DataSize);
                    ui.SetParent(draggedId, "");
                    isDirty = true;
                } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_PALETTE_ITEM")) {
                    int typeVal = *(const int*)payload->Data;
                    AddElement((UIElementType)typeVal, "", "");
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::EndChild();
    }

    void DrawHierarchyNode(UIElement& elem) {
        ImGui::PushID(elem.id.c_str());

        // Visibility eye button
        if (ImGui::Button(elem.visible ? "👁" : "🚫", ImVec2(22, 20))) {
            elem.visible = !elem.visible;
            isDirty = true;
        }
        ImGui::SameLine();

        bool hasChildren = !elem.childIds.empty();
        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasChildren) nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (selectedElemId == elem.id) nodeFlags |= ImGuiTreeNodeFlags_Selected;
        if (elem.isExpanded) nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

        const char* icon = (elem.type == UIElementType::Border) ? "🔲" : "🔤";
        std::string label = std::string(icon) + " " + elem.name;

        bool nodeOpen = ImGui::TreeNodeEx(elem.id.c_str(), nodeFlags, "%s", label.c_str());

        if (ImGui::IsItemClicked()) {
            selectedElemId = elem.id;
        }

        // Drag Source (to re-parent or move)
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("UI_HIERARCHY_ITEM", elem.id.c_str(), elem.id.size() + 1);
            ImGui::Text("Move %s", elem.name.c_str());
            ImGui::EndDragDropSource();
        }

        // Drag Target (to become parent of dragged item)
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_HIERARCHY_ITEM")) {
                std::string draggedId((const char*)payload->Data, payload->DataSize);
                if (draggedId != elem.id) {
                    ui.SetParent(draggedId, elem.id);
                    isDirty = true;
                }
            } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_PALETTE_ITEM")) {
                int typeVal = *(const int*)payload->Data;
                AddElement((UIElementType)typeVal, "", elem.id);
            }
            ImGui::EndDragDropTarget();
        }

        // Right-Click Context Menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("➕ Add Child Border")) {
                AddElement(UIElementType::Border, "", elem.id);
            }
            if (ImGui::MenuItem("➕ Add Child Text")) {
                AddElement(UIElementType::Text, "", elem.id);
            }
            ImGui::Separator();
            if (!elem.parentId.empty()) {
                if (ImGui::MenuItem("📤 Unparent (Move to Root)")) {
                    ui.SetParent(elem.id, "");
                    isDirty = true;
                }
            }
            if (ImGui::MenuItem("📋 Duplicate")) {
                UIElement* dup = ui.AddElement(elem.type, elem.name + "_Copy", elem.parentId);
                if (dup) {
                    *dup = elem;
                    dup->id = "elem_" + std::to_string(rand() % 100000);
                    dup->name = elem.name + "_Copy";
                    dup->position += glm::vec2(20.0f, 20.0f);
                    dup->childIds.clear();
                    selectedElemId = dup->id;
                    isDirty = true;
                }
            }
            if (ImGui::MenuItem("🗑 Delete")) {
                ui.RemoveElement(elem.id);
                if (selectedElemId == elem.id) selectedElemId = "";
                isDirty = true;
            }
            ImGui::EndPopup();
        }

        if (hasChildren && nodeOpen) {
            for (const auto& chId : elem.childIds) {
                UIElement* child = ui.FindElement(chId);
                if (child) {
                    DrawHierarchyNode(*child);
                }
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void RenderCanvas() {
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Canvas clipping
        drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        // Canvas Background Grid
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 20, 24, 255));
        float gridStep = 40.0f * zoom;
        if (gridStep > 8.0f) {
            for (float x = std::fmod(pan.x, gridStep); x < canvasSize.x; x += gridStep) {
                drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y), IM_COL32(30, 30, 38, 255));
            }
            for (float y = std::fmod(pan.y, gridStep); y < canvasSize.y; y += gridStep) {
                drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y), IM_COL32(30, 30, 38, 255));
            }
        }

        // Virtual Screen Boundary (e.g. 1920x1080) — Transparent interior with crisp border
        ImVec2 screenOrigin = ImVec2(canvasPos.x + pan.x, canvasPos.y + pan.y);
        ImVec2 screenEnd = ImVec2(screenOrigin.x + ui.canvasResolution.x * zoom, screenOrigin.y + ui.canvasResolution.y * zoom);
        drawList->AddRectFilled(screenOrigin, screenEnd, IM_COL32(12, 12, 16, 220));
        drawList->AddRect(screenOrigin, screenEnd, IM_COL32(70, 70, 90, 255), 0.0f, 0, 1.5f);

        // Resolution Tag
        char resBuf[64];
        snprintf(resBuf, sizeof(resBuf), "🖥 Canvas: %.0f x %.0f", ui.canvasResolution.x, ui.canvasResolution.y);
        drawList->AddText(ImVec2(screenOrigin.x + 8.0f, screenOrigin.y + 6.0f), IM_COL32(140, 140, 165, 255), resBuf);

        // Handle Pan with Middle Mouse / Right Mouse Drag
        ImGuiIO& io = ImGui::GetIO();
        bool isHovered = ImGui::IsWindowHovered();

        if (isHovered && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (ImGui::IsMouseDown(ImGuiMouseButton_Right) && !isDraggingElement))) {
            if (!isPanning) {
                isPanning = true;
                panStart = io.MousePos;
                panInitial = pan;
            } else {
                pan = ImVec2(panInitial.x + (io.MousePos.x - panStart.x), panInitial.y + (io.MousePos.y - panStart.y));
            }
        } else {
            isPanning = false;
        }

        // Mouse Wheel Zoom
        if (isHovered && io.MouseWheel != 0.0f) {
            float zoomDelta = io.MouseWheel * 0.08f;
            float newZoom = std::clamp(zoom + zoomDelta, 0.2f, 2.5f);
            ImVec2 mouseRel = ImVec2(io.MousePos.x - canvasPos.x, io.MousePos.y - canvasPos.y);
            pan.x -= (mouseRel.x - pan.x) * (newZoom / zoom - 1.0f);
            pan.y -= (mouseRel.y - pan.y) * (newZoom / zoom - 1.0f);
            zoom = newZoom;
        }

        // Render UI Elements on Canvas using hierarchical world rects
        UIElement* selectedElem = nullptr;
        for (auto& el : ui.elements) {
            if (!el.visible) continue;
            if (el.id == selectedElemId) selectedElem = &el;

            glm::vec4 wRect = ui.GetElementWorldRect(el);
            ImVec2 elemPos(screenOrigin.x + wRect.x * zoom, screenOrigin.y + wRect.y * zoom);
            ImVec2 elemSize(wRect.z * zoom, wRect.w * zoom);
            ImVec2 elemEnd(elemPos.x + elemSize.x, elemPos.y + elemSize.y);

            if (el.type == UIElementType::Border) {
                ImU32 bgCol = IM_COL32((int)(el.backgroundColor.r * 255), (int)(el.backgroundColor.g * 255), (int)(el.backgroundColor.b * 255), (int)(el.backgroundColor.a * 255));
                ImU32 bdCol = IM_COL32((int)(el.borderColor.r * 255), (int)(el.borderColor.g * 255), (int)(el.borderColor.b * 255), (int)(el.borderColor.a * 255));
                if (el.filled) {
                    drawList->AddRectFilled(elemPos, elemEnd, bgCol, el.cornerRounding * zoom);
                }
                if (el.borderThickness > 0.0f) {
                    drawList->AddRect(elemPos, elemEnd, bdCol, el.cornerRounding * zoom, 0, el.borderThickness * zoom);
                }
            } else if (el.type == UIElementType::Text) {
                ImU32 txtCol = IM_COL32((int)(el.textColor.r * 255), (int)(el.textColor.g * 255), (int)(el.textColor.b * 255), (int)(el.textColor.a * 255));
                ImU32 shdCol = IM_COL32((int)(el.shadowColor.r * 255), (int)(el.shadowColor.g * 255), (int)(el.shadowColor.b * 255), (int)(el.shadowColor.a * 255));

                float fontSize = el.fontSize * zoom;
                ImVec2 textSize = ImGui::CalcTextSize(el.text.c_str());
                ImVec2 textPos = elemPos;
                if (el.textAlignment == 1) {
                    textPos.x = elemPos.x + (elemSize.x - textSize.x) * 0.5f;
                } else if (el.textAlignment == 2) {
                    textPos.x = elemEnd.x - textSize.x;
                }

                if (el.shadow) {
                    ImVec2 shdPos(textPos.x + el.shadowOffset.x * zoom, textPos.y + el.shadowOffset.y * zoom);
                    drawList->AddText(nullptr, fontSize, shdPos, shdCol, el.text.c_str());
                }
                drawList->AddText(nullptr, fontSize, textPos, txtCol, el.text.c_str());
            }
        }

        // Selection Outline & Gizmo for Selected Element
        if (selectedElem) {
            glm::vec4 wRect = ui.GetElementWorldRect(*selectedElem);
            ImVec2 elemPos(screenOrigin.x + wRect.x * zoom, screenOrigin.y + wRect.y * zoom);
            ImVec2 elemSize(wRect.z * zoom, wRect.w * zoom);
            ImVec2 elemEnd(elemPos.x + elemSize.x, elemPos.y + elemSize.y);

            // Selection box
            drawList->AddRect(elemPos, elemEnd, IM_COL32(50, 160, 255, 255), 0.0f, 0, 2.0f);

            // Corner resize handles
            float handleSize = 6.0f;
            drawList->AddRectFilled(ImVec2(elemEnd.x - handleSize, elemEnd.y - handleSize), ImVec2(elemEnd.x + handleSize, elemEnd.y + handleSize), IM_COL32(255, 255, 255, 255));
            drawList->AddRect(ImVec2(elemEnd.x - handleSize, elemEnd.y - handleSize), ImVec2(elemEnd.x + handleSize, elemEnd.y + handleSize), IM_COL32(50, 160, 255, 255), 0.0f, 0, 1.5f);

            // Parent rect & Anchor visualization
            glm::vec4 parentRect(0.0f, 0.0f, ui.canvasResolution.x, ui.canvasResolution.y);
            if (!selectedElem->parentId.empty()) {
                const UIElement* p = ui.FindElement(selectedElem->parentId);
                if (p) parentRect = ui.GetElementWorldRect(*p);
            }
            ImVec2 pMin(screenOrigin.x + parentRect.x * zoom, screenOrigin.y + parentRect.y * zoom);
            ImVec2 pSize(parentRect.z * zoom, parentRect.w * zoom);

            ImVec2 anchorMinPos(pMin.x + selectedElem->anchorMin.x * pSize.x, pMin.y + selectedElem->anchorMin.y * pSize.y);
            ImVec2 anchorMaxPos(pMin.x + selectedElem->anchorMax.x * pSize.x, pMin.y + selectedElem->anchorMax.y * pSize.y);

            drawList->AddCircleFilled(anchorMinPos, 4.0f, IM_COL32(255, 200, 50, 255));
            if (anchorMinPos.x != anchorMaxPos.x || anchorMinPos.y != anchorMaxPos.y) {
                drawList->AddCircleFilled(anchorMaxPos, 4.0f, IM_COL32(255, 200, 50, 255));
                drawList->AddLine(anchorMinPos, anchorMaxPos, IM_COL32(255, 200, 50, 180), 1.0f);
            }
        }

        // Handle Element Selection & Dragging
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            bool clickedElem = false;
            // Iterate in reverse for topmost element
            for (int i = (int)ui.elements.size() - 1; i >= 0; --i) {
                auto& el = ui.elements[i];
                if (!el.visible) continue;

                glm::vec4 wRect = ui.GetElementWorldRect(el);
                ImVec2 elemPos(screenOrigin.x + wRect.x * zoom, screenOrigin.y + wRect.y * zoom);
                ImVec2 elemEnd(elemPos.x + wRect.z * zoom, elemPos.y + wRect.w * zoom);

                // Check resize handle click if already selected
                if (el.id == selectedElemId) {
                    float handleSize = 8.0f;
                    if (io.MousePos.x >= elemEnd.x - handleSize && io.MousePos.x <= elemEnd.x + handleSize &&
                        io.MousePos.y >= elemEnd.y - handleSize && io.MousePos.y <= elemEnd.y + handleSize) {
                        resizeHandle = 3; // BR resize
                        isDraggingElement = true;
                        clickedElem = true;
                        break;
                    }
                }

                if (io.MousePos.x >= elemPos.x && io.MousePos.x <= elemEnd.x &&
                    io.MousePos.y >= elemPos.y && io.MousePos.y <= elemEnd.y) {
                    selectedElemId = el.id;
                    isDraggingElement = true;
                    resizeHandle = 0;
                    dragOffset = ImVec2((io.MousePos.x - elemPos.x) / zoom, (io.MousePos.y - elemPos.y) / zoom);
                    clickedElem = true;
                    break;
                }
            }
            if (!clickedElem) {
                // Clicked on empty canvas background -> select Canvas Root
                selectedElemId = "";
            }
        }

        // Process Dragging / Resizing
        if (isDraggingElement && selectedElem && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            glm::vec4 parentRect(0.0f, 0.0f, ui.canvasResolution.x, ui.canvasResolution.y);
            if (!selectedElem->parentId.empty()) {
                const UIElement* p = ui.FindElement(selectedElem->parentId);
                if (p) parentRect = ui.GetElementWorldRect(*p);
            }

            if (resizeHandle == 3) {
                // Resize bottom-right
                glm::vec4 curRect = ui.GetElementWorldRect(*selectedElem);
                float mouseCanvasX = (io.MousePos.x - screenOrigin.x) / zoom;
                float mouseCanvasY = (io.MousePos.y - screenOrigin.y) / zoom;
                float newW = mouseCanvasX - curRect.x;
                float newH = mouseCanvasY - curRect.y;
                if (newW > 10.0f) selectedElem->size.x = std::round(newW);
                if (newH > 10.0f) selectedElem->size.y = std::round(newH);
                isDirty = true;
            } else {
                // Move element in parent coordinate space
                float mouseCanvasX = (io.MousePos.x - screenOrigin.x) / zoom;
                float mouseCanvasY = (io.MousePos.y - screenOrigin.y) / zoom;

                float targetElemLeft = mouseCanvasX - dragOffset.x;
                float targetElemTop = mouseCanvasY - dragOffset.y;

                float anchorX = parentRect.x + selectedElem->anchorMin.x * parentRect.z;
                float anchorY = parentRect.y + selectedElem->anchorMin.y * parentRect.w;

                selectedElem->position.x = std::round(targetElemLeft - anchorX + selectedElem->pivot.x * selectedElem->size.x);
                selectedElem->position.y = std::round(targetElemTop - anchorY + selectedElem->pivot.y * selectedElem->size.y);
                isDirty = true;
            }
        } else {
            isDraggingElement = false;
            resizeHandle = 0;
        }

        // Accept Drag and Drop from Palette onto Canvas
        if (ImGui::BeginDragDropTargetCustom(ImRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y)), ImGui::GetID("UICenterCanvas"))) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_PALETTE_ITEM")) {
                int typeVal = *(const int*)payload->Data;
                UIElementType type = (UIElementType)typeVal;
                UIElement* newElem = ui.AddElement(type);
                if (newElem) {
                    float dropX = (io.MousePos.x - screenOrigin.x) / zoom;
                    float dropY = (io.MousePos.y - screenOrigin.y) / zoom;
                    newElem->position = glm::vec2(std::round(dropX), std::round(dropY));
                    selectedElemId = newElem->id;
                    isDirty = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        drawList->PopClipRect();
    }

    void RenderRightInspector() {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "DETAILS / INSPECTOR");
        ImGui::Separator();

        if (selectedElemId.empty()) {
            // --- CANVAS ROOT PROPERTIES ---
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "🖥 CANVAS (ROOT CONTAINER)");
            ImGui::Spacing();

            char nameBuf[128];
            strncpy(nameBuf, ui.name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            if (ImGui::InputText("UI Asset Name", nameBuf, sizeof(nameBuf))) {
                ui.name = nameBuf;
                isDirty = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "CANVAS RESOLUTION");

            // Preset Combo
            const char* resPresets[] = {
                "1920 x 1080 (Full HD / 16:9)",
                "1280 x 720 (HD / 16:9)",
                "2560 x 1440 (2K QHD / 16:9)",
                "3840 x 2160 (4K UHD / 16:9)",
                "1080 x 1920 (Portrait / 9:16)",
                "Custom"
            };

            int currentResIdx = 5; // Custom
            if (ui.canvasResolution.x == 1920.0f && ui.canvasResolution.y == 1080.0f) currentResIdx = 0;
            else if (ui.canvasResolution.x == 1280.0f && ui.canvasResolution.y == 720.0f) currentResIdx = 1;
            else if (ui.canvasResolution.x == 2560.0f && ui.canvasResolution.y == 1440.0f) currentResIdx = 2;
            else if (ui.canvasResolution.x == 3840.0f && ui.canvasResolution.y == 2160.0f) currentResIdx = 3;
            else if (ui.canvasResolution.x == 1080.0f && ui.canvasResolution.y == 1920.0f) currentResIdx = 4;

            if (ImGui::Combo("Preset", &currentResIdx, resPresets, 6)) {
                switch (currentResIdx) {
                    case 0: ui.canvasResolution = { 1920.0f, 1080.0f }; break;
                    case 1: ui.canvasResolution = { 1280.0f, 720.0f }; break;
                    case 2: ui.canvasResolution = { 2560.0f, 1440.0f }; break;
                    case 3: ui.canvasResolution = { 3840.0f, 2160.0f }; break;
                    case 4: ui.canvasResolution = { 1080.0f, 1920.0f }; break;
                }
                isDirty = true;
            }

            if (ImGui::DragFloat2("Resolution (W, H)", &ui.canvasResolution.x, 10.0f, 320.0f, 7680.0f, "%.0f px")) {
                if (ui.canvasResolution.x < 100.0f) ui.canvasResolution.x = 100.0f;
                if (ui.canvasResolution.y < 100.0f) ui.canvasResolution.y = 100.0f;
                isDirty = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Total Elements: %d", (int)ui.elements.size());
            return;
        }

        UIElement* elem = ui.FindElement(selectedElemId);
        if (!elem) {
            selectedElemId = "";
            return;
        }

        // Element Name & Type
        char nameBuf[128];
        strncpy(nameBuf, elem->name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            elem->name = nameBuf;
            isDirty = true;
        }

        ImGui::Text("Type: %s", UIElementTypeToString(elem->type));
        if (ImGui::Checkbox("Visible", &elem->visible)) isDirty = true;
        if (ImGui::DragInt("Z-Order", &elem->zOrder, 1, -100, 100)) isDirty = true;

        // Parent Information
        std::string parentName = "(Root Canvas)";
        if (!elem->parentId.empty()) {
            UIElement* p = ui.FindElement(elem->parentId);
            if (p) parentName = p->name + " (" + elem->parentId + ")";
        }
        ImGui::Text("Parent: %s", parentName.c_str());
        if (!elem->parentId.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Unparent")) {
                ui.SetParent(elem->id, "");
                isDirty = true;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "ANCHORS & LAYOUT");

        // Anchor Presets Selector (All 16 presets)
        const char* anchorPresetNames[] = {
            "Top-Left",
            "Top-Center",
            "Top-Right",
            "Center-Left",
            "Center",
            "Center-Right",
            "Bottom-Left",
            "Bottom-Center",
            "Bottom-Right",
            "Stretch Horizontal (Top)",
            "Stretch Horizontal (Middle)",
            "Stretch Horizontal (Bottom)",
            "Stretch Vertical (Left)",
            "Stretch Vertical (Center)",
            "Stretch Vertical (Right)",
            "Stretch Full (Both)",
            "Custom"
        };

        int presetIdx = (int)elem->anchorPreset;
        if (ImGui::Combo("Anchor Preset", &presetIdx, anchorPresetNames, 17)) {
            elem->anchorPreset = (UIAnchorPreset)presetIdx;
            if (elem->anchorPreset != UIAnchorPreset::Custom) {
                AnchorPresetToMinMax(elem->anchorPreset, elem->anchorMin, elem->anchorMax, elem->pivot);
            }
            isDirty = true;
        }

        bool isHorizStretch = (std::abs(elem->anchorMin.x - elem->anchorMax.x) > 0.001f);
        bool isVertStretch = (std::abs(elem->anchorMin.y - elem->anchorMax.y) > 0.001f);

        // Position & Size controls (hierarchy parent-relative)
        if (!isHorizStretch && !isVertStretch) {
            // Point anchor
            if (ImGui::DragFloat2("Position (X, Y)", &elem->position.x, 1.0f)) isDirty = true;
            if (ImGui::DragFloat2("Size (W, H)", &elem->size.x, 1.0f, 1.0f, 4000.0f)) isDirty = true;
        } else {
            // Stretch mode
            if (isHorizStretch) {
                if (ImGui::DragFloat("Left Offset", &elem->position.x, 1.0f)) isDirty = true;
                if (ImGui::DragFloat("Right Offset", &elem->size.x, 1.0f)) isDirty = true;
            } else {
                if (ImGui::DragFloat("Position X", &elem->position.x, 1.0f)) isDirty = true;
                if (ImGui::DragFloat("Width", &elem->size.x, 1.0f, 1.0f, 4000.0f)) isDirty = true;
            }

            if (isVertStretch) {
                if (ImGui::DragFloat("Top Offset", &elem->position.y, 1.0f)) isDirty = true;
                if (ImGui::DragFloat("Bottom Offset", &elem->size.y, 1.0f)) isDirty = true;
            } else {
                if (ImGui::DragFloat("Position Y", &elem->position.y, 1.0f)) isDirty = true;
                if (ImGui::DragFloat("Height", &elem->size.y, 1.0f, 1.0f, 4000.0f)) isDirty = true;
            }
        }

        if (ImGui::DragFloat2("Pivot (Norm)", &elem->pivot.x, 0.01f, 0.0f, 1.0f)) isDirty = true;
        if (ImGui::DragFloat2("Anchor Min", &elem->anchorMin.x, 0.01f, 0.0f, 1.0f)) {
            elem->anchorPreset = UIAnchorPreset::Custom;
            isDirty = true;
        }
        if (ImGui::DragFloat2("Anchor Max", &elem->anchorMax.x, 0.01f, 0.0f, 1.0f)) {
            elem->anchorPreset = UIAnchorPreset::Custom;
            isDirty = true;
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Type-Specific Properties
        if (elem->type == UIElementType::Border) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "BORDER PROPERTIES");
            if (ImGui::ColorEdit4("Background Color", &elem->backgroundColor.r)) isDirty = true;
            if (ImGui::ColorEdit4("Border Color", &elem->borderColor.r)) isDirty = true;
            if (ImGui::DragFloat("Border Thickness", &elem->borderThickness, 0.2f, 0.0f, 30.0f, "%.1f px")) isDirty = true;
            if (ImGui::DragFloat("Corner Rounding", &elem->cornerRounding, 0.5f, 0.0f, 50.0f, "%.1f px")) isDirty = true;
            if (ImGui::Checkbox("Filled", &elem->filled)) isDirty = true;
        } else if (elem->type == UIElementType::Text) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "TEXT PROPERTIES");

            char textBuf[1024];
            strncpy(textBuf, elem->text.c_str(), sizeof(textBuf) - 1);
            textBuf[sizeof(textBuf) - 1] = '\0';
            if (ImGui::InputTextMultiline("Text Content", textBuf, sizeof(textBuf), ImVec2(-1, 60))) {
                elem->text = textBuf;
                isDirty = true;
            }

            if (ImGui::SliderFloat("Font Size", &elem->fontSize, 8.0f, 72.0f, "%.1f px")) isDirty = true;
            if (ImGui::ColorEdit4("Text Color", &elem->textColor.r)) isDirty = true;

            const char* alignments[] = { "Left", "Center", "Right" };
            if (ImGui::Combo("Alignment", &elem->textAlignment, alignments, 3)) isDirty = true;

            if (ImGui::Checkbox("Drop Shadow", &elem->shadow)) isDirty = true;
            if (elem->shadow) {
                if (ImGui::ColorEdit4("Shadow Color", &elem->shadowColor.r)) isDirty = true;
                if (ImGui::DragFloat2("Shadow Offset", &elem->shadowOffset.x, 0.5f, -20.0f, 20.0f, "%.1f px")) isDirty = true;
            }
        }
    }
};

class UIEditorManager {
public:
    static UIEditorManager& Get() {
        static UIEditorManager instance;
        return instance;
    }

    void Open(const std::string& filePath, const std::string& name = "") {
        for (auto& editor : m_editors) {
            if (editor->filePath == filePath) {
                editor->isOpen = true;
                return;
            }
        }
        m_editors.push_back(std::make_unique<UIEditorWindow>(filePath, name));
    }

    void RenderAll() {
        for (auto it = m_editors.begin(); it != m_editors.end();) {
            if ((*it)->isOpen) {
                (*it)->Render();
                ++it;
            } else {
                it = m_editors.erase(it);
            }
        }
    }

    bool HasOpenEditors() const {
        return !m_editors.empty();
    }

private:
    UIEditorManager() = default;
    std::vector<std::unique_ptr<UIEditorWindow>> m_editors;
};

} // namespace EunoiaUI
