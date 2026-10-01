#pragma once

#include "UserInterface.h"
#include "UISystem.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>
#include <unordered_map>
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
    std::string selectedElemId = "";
    bool isDraggingElement = false;
    ImVec2 dragOffset = { 0.0f, 0.0f };
    int resizeHandle = 0; // 0=none, 1=TL, 2=TR, 3=BR, 4=BL, 5=R, 6=B

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
        ImGui::SetNextWindowSize(ImVec2(1000, 680), ImGuiCond_FirstUseEver);

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
                    AddElement(UIElementType::Border);
                }
                if (ImGui::MenuItem("🔤 Text")) {
                    AddElement(UIElementType::Text);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Toolbar
        RenderToolbar();

        // 3-Pane Layout: Left (Palette & Hierarchy) | Center (Canvas) | Right (Inspector)
        float leftWidth = 240.0f;
        float rightWidth = 280.0f;
        float centerWidth = ImGui::GetContentRegionAvail().x - leftWidth - rightWidth - 16.0f;
        if (centerWidth < 200.0f) centerWidth = 200.0f;
        float paneHeight = ImGui::GetContentRegionAvail().y;

        // --- LEFT PANE (Palette + Hierarchy) ---
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

    void AddElement(UIElementType type) {
        UIElement* elem = ui.AddElement(type);
        if (elem) {
            selectedElemId = elem->id;
            isDirty = true;
        }
    }

private:
    void RenderToolbar() {
        if (ImGui::Button("💾 Save")) {
            Save();
        }
        ImGui::SameLine();
        if (ImGui::Button("➕ Border")) {
            AddElement(UIElementType::Border);
        }
        ImGui::SameLine();
        if (ImGui::Button("➕ Text")) {
            AddElement(UIElementType::Text);
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
                    UIElement* dup = ui.AddElement(orig->type, orig->name + "_Copy");
                    if (dup) {
                        *dup = *orig;
                        dup->id = "elem_" + std::to_string(rand() % 100000);
                        dup->name = orig->name + "_Copy";
                        dup->position += glm::vec2(20.0f, 20.0f);
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
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "PALETTE (Drag to Canvas)");
        ImGui::Separator();

        // Draggable Border item
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.32f, 0.45f, 1.0f));
        ImGui::Button("🔲 Border", ImVec2(-1, 32));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const char* payloadType = "UI_PALETTE_ITEM";
            int typeVal = (int)UIElementType::Border;
            ImGui::SetDragDropPayload(payloadType, &typeVal, sizeof(int));
            ImGui::Text("Drop Border onto Canvas");
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            AddElement(UIElementType::Border);
        }

        // Draggable Text item
        ImGui::Button("🔤 Text", ImVec2(-1, 32));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const char* payloadType = "UI_PALETTE_ITEM";
            int typeVal = (int)UIElementType::Text;
            ImGui::SetDragDropPayload(payloadType, &typeVal, sizeof(int));
            ImGui::Text("Drop Text onto Canvas");
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            AddElement(UIElementType::Text);
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "HIERARCHY");
        ImGui::Separator();

        ImGui::BeginChild("HierarchyTreeChild", ImVec2(0, 0), false);
        for (int i = (int)ui.elements.size() - 1; i >= 0; --i) {
            auto& elem = ui.elements[i];
            ImGui::PushID(elem.id.c_str());

            // Visibility toggle
            if (ImGui::Button(elem.visible ? "👁" : "🚫", ImVec2(24, 20))) {
                elem.visible = !elem.visible;
                isDirty = true;
            }
            ImGui::SameLine();

            bool isSelected = (selectedElemId == elem.id);
            const char* icon = (elem.type == UIElementType::Border) ? "🔲" : "🔤";
            std::string label = std::string(icon) + " " + elem.name;

            if (ImGui::Selectable(label.c_str(), isSelected)) {
                selectedElemId = elem.id;
            }

            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void RenderCanvas() {
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Canvas clipping
        drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        // Canvas Background Grid
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(24, 24, 28, 255));
        float gridStep = 40.0f * zoom;
        if (gridStep > 8.0f) {
            for (float x = std::fmod(pan.x, gridStep); x < canvasSize.x; x += gridStep) {
                drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y), IM_COL32(35, 35, 42, 255));
            }
            for (float y = std::fmod(pan.y, gridStep); y < canvasSize.y; y += gridStep) {
                drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y), IM_COL32(35, 35, 42, 255));
            }
        }

        // Virtual Screen Boundary (e.g. 1920x1080)
        ImVec2 screenOrigin = ImVec2(canvasPos.x + pan.x, canvasPos.y + pan.y);
        ImVec2 screenEnd = ImVec2(screenOrigin.x + ui.canvasResolution.x * zoom, screenOrigin.y + ui.canvasResolution.y * zoom);
        drawList->AddRectFilled(screenOrigin, screenEnd, IM_COL32(14, 14, 18, 240));
        drawList->AddRect(screenOrigin, screenEnd, IM_COL32(60, 60, 75, 255), 0.0f, 0, 1.5f);

        // Resolution Tag
        char resBuf[64];
        snprintf(resBuf, sizeof(resBuf), "Design Canvas: %.0f x %.0f", ui.canvasResolution.x, ui.canvasResolution.y);
        drawList->AddText(ImVec2(screenOrigin.x + 8.0f, screenOrigin.y + 6.0f), IM_COL32(120, 120, 140, 255), resBuf);

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

        // Render UI Elements on Canvas
        UIElement* selectedElem = nullptr;
        for (auto& el : ui.elements) {
            if (!el.visible) continue;
            if (el.id == selectedElemId) selectedElem = &el;

            // Compute element screen coordinates
            ImVec2 anchorPos = ImVec2(screenOrigin.x + el.anchor.x * ui.canvasResolution.x * zoom,
                                      screenOrigin.y + el.anchor.y * ui.canvasResolution.y * zoom);
            ImVec2 elemPos = ImVec2(anchorPos.x + el.position.x * zoom - el.pivot.x * el.size.x * zoom,
                                    anchorPos.y + el.position.y * zoom - el.pivot.y * el.size.y * zoom);
            ImVec2 elemSize = ImVec2(el.size.x * zoom, el.size.y * zoom);
            ImVec2 elemEnd = ImVec2(elemPos.x + elemSize.x, elemPos.y + elemSize.y);

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

                if (el.shadow) {
                    ImVec2 shdPos = ImVec2(elemPos.x + el.shadowOffset.x * zoom, elemPos.y + el.shadowOffset.y * zoom);
                    drawList->AddText(nullptr, el.fontSize * zoom, shdPos, shdCol, el.text.c_str());
                }
                drawList->AddText(nullptr, el.fontSize * zoom, elemPos, txtCol, el.text.c_str());
            }
        }

        // Selection Outline & Gizmo for Selected Element
        if (selectedElem) {
            ImVec2 anchorPos = ImVec2(screenOrigin.x + selectedElem->anchor.x * ui.canvasResolution.x * zoom,
                                      screenOrigin.y + selectedElem->anchor.y * ui.canvasResolution.y * zoom);
            ImVec2 elemPos = ImVec2(anchorPos.x + selectedElem->position.x * zoom - selectedElem->pivot.x * selectedElem->size.x * zoom,
                                    anchorPos.y + selectedElem->position.y * zoom - selectedElem->pivot.y * selectedElem->size.y * zoom);
            ImVec2 elemSize = ImVec2(selectedElem->size.x * zoom, selectedElem->size.y * zoom);
            ImVec2 elemEnd = ImVec2(elemPos.x + elemSize.x, elemPos.y + elemSize.y);

            // Selection box
            drawList->AddRect(elemPos, elemEnd, IM_COL32(50, 160, 255, 255), 0.0f, 0, 2.0f);

            // Corner resize handles
            float handleSize = 6.0f;
            drawList->AddRectFilled(ImVec2(elemEnd.x - handleSize, elemEnd.y - handleSize), ImVec2(elemEnd.x + handleSize, elemEnd.y + handleSize), IM_COL32(255, 255, 255, 255));
            drawList->AddRect(ImVec2(elemEnd.x - handleSize, elemEnd.y - handleSize), ImVec2(elemEnd.x + handleSize, elemEnd.y + handleSize), IM_COL32(50, 160, 255, 255), 0.0f, 0, 1.5f);

            // Anchor marker
            drawList->AddCircleFilled(anchorPos, 4.0f, IM_COL32(255, 200, 50, 255));
        }

        // Handle Element Selection & Dragging
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            bool clickedElem = false;
            // Iterate in reverse for topmost element
            for (int i = (int)ui.elements.size() - 1; i >= 0; --i) {
                auto& el = ui.elements[i];
                if (!el.visible) continue;

                ImVec2 anchorPos = ImVec2(screenOrigin.x + el.anchor.x * ui.canvasResolution.x * zoom,
                                          screenOrigin.y + el.anchor.y * ui.canvasResolution.y * zoom);
                ImVec2 elemPos = ImVec2(anchorPos.x + el.position.x * zoom - el.pivot.x * el.size.x * zoom,
                                        anchorPos.y + el.position.y * zoom - el.pivot.y * el.size.y * zoom);
                ImVec2 elemEnd = ImVec2(elemPos.x + el.size.x * zoom, elemPos.y + el.size.y * zoom);

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
                // Clicked on empty canvas
                selectedElemId = "";
            }
        }

        // Process Dragging / Resizing
        if (isDraggingElement && selectedElem && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (resizeHandle == 3) {
                // Resize bottom-right
                ImVec2 anchorPos = ImVec2(screenOrigin.x + selectedElem->anchor.x * ui.canvasResolution.x * zoom,
                                          screenOrigin.y + selectedElem->anchor.y * ui.canvasResolution.y * zoom);
                ImVec2 elemPos = ImVec2(anchorPos.x + selectedElem->position.x * zoom - selectedElem->pivot.x * selectedElem->size.x * zoom,
                                        anchorPos.y + selectedElem->position.y * zoom - selectedElem->pivot.y * selectedElem->size.y * zoom);
                float newW = (io.MousePos.x - elemPos.x) / zoom;
                float newH = (io.MousePos.y - elemPos.y) / zoom;
                if (newW > 20.0f) selectedElem->size.x = std::round(newW);
                if (newH > 10.0f) selectedElem->size.y = std::round(newH);
                isDirty = true;
            } else {
                // Move element
                ImVec2 anchorPos = ImVec2(screenOrigin.x + selectedElem->anchor.x * ui.canvasResolution.x * zoom,
                                          screenOrigin.y + selectedElem->anchor.y * ui.canvasResolution.y * zoom);
                float targetCanvasX = (io.MousePos.x - anchorPos.x) / zoom - dragOffset.x + selectedElem->pivot.x * selectedElem->size.x;
                float targetCanvasY = (io.MousePos.y - anchorPos.y) / zoom - dragOffset.y + selectedElem->pivot.y * selectedElem->size.y;
                selectedElem->position.x = std::round(targetCanvasX);
                selectedElem->position.y = std::round(targetCanvasY);
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
                    // Position at drop location relative to screenOrigin
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

        UIElement* elem = ui.FindElement(selectedElemId);
        if (!elem) {
            ImGui::Spacing();
            ImGui::TextDisabled("No element selected.");
            ImGui::TextDisabled("Click an element on canvas or hierarchy to view properties.");
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

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "TRANSFORM & LAYOUT");

        // Position & Size
        if (ImGui::DragFloat2("Position (X, Y)", &elem->position.x, 1.0f)) isDirty = true;
        if (ImGui::DragFloat2("Size (W, H)", &elem->size.x, 1.0f, 1.0f, 4000.0f)) isDirty = true;

        // Anchor Preset Selector
        const char* anchorPresets[] = { "Top-Left", "Top-Center", "Top-Right", "Center-Left", "Center", "Center-Right", "Bottom-Left", "Bottom-Center", "Bottom-Right" };
        int currentPreset = -1;
        for (int p = 0; p < 9; ++p) {
            glm::vec2 pv = AnchorPresetToVec2((UIAnchorPreset)p);
            if (std::abs(elem->anchor.x - pv.x) < 0.01f && std::abs(elem->anchor.y - pv.y) < 0.01f) {
                currentPreset = p;
                break;
            }
        }
        const char* preview = (currentPreset >= 0) ? anchorPresets[currentPreset] : "Custom";
        if (ImGui::BeginCombo("Anchor Preset", preview)) {
            for (int p = 0; p < 9; ++p) {
                if (ImGui::Selectable(anchorPresets[p], currentPreset == p)) {
                    elem->anchor = AnchorPresetToVec2((UIAnchorPreset)p);
                    isDirty = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::DragFloat2("Anchor (Norm)", &elem->anchor.x, 0.01f, 0.0f, 1.0f)) isDirty = true;
        if (ImGui::DragFloat2("Pivot (Norm)", &elem->pivot.x, 0.01f, 0.0f, 1.0f)) isDirty = true;

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
